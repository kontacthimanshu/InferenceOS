#include <inferenceos/block.h>

#include <inferenceos/drivers/hyperv/storvsc.h>
#include <inferenceos/drivers/hyperv/input.h>
#include <inferenceos/drivers/hyperv/platform.h>
#include <inferenceos/memory.h>
#include <inferenceos/runtime.h>

enum {
    HYPERV_RING_PAGES = 4,
    HYPERV_OUTBOUND_RING_PAGES = 2,
    HYPERV_BOUNCE_PAGES = 32,
    HYPERV_MAX_LUN = 63
};

struct hyperv_block_platform {
    struct ios_vmbus bus;
    struct ios_vmbus_channel storage_channel;
    struct ios_storvsc storage;
    ios_uptr hypercall_page;
    ios_uptr post_page;
    ios_uptr message_page;
    ios_uptr event_page;
    ios_uptr ring_pages;
    ios_uptr bounce_pages;
    ios_uptr keyboard_ring_pages;
    ios_uptr mouse_ring_pages;
    struct ios_vmbus_channel keyboard_channel;
    struct ios_vmbus_channel mouse_channel;
    struct ios_hyperv_keyboard keyboard;
    struct ios_hyperv_mouse mouse;
    bool keyboard_ready;
    bool mouse_ready;
    bool resources_allocated;
};

static struct hyperv_block_platform platform;
static const char *last_stage = "not_started";
static const char *keyboard_stage = "not_started";
static const char *mouse_stage = "not_started";
static ios_status keyboard_last_status = IOS_OK;
static ios_status mouse_last_status = IOS_OK;

static ios_status allocate_shared_pages(void)
{
    ios_status status;

#define ALLOCATE(member, count)                                                   \
    do {                                                                          \
        status = physical_allocate_pages((count), 1, &platform.member);           \
        if (IOS_FAILED(status)) return status;                                    \
        memset((void *)platform.member, 0, (ios_size)(count) * IOS_HV_PAGE_SIZE); \
    } while (0)

    ALLOCATE(hypercall_page, 1);
    ALLOCATE(post_page, 1);
    ALLOCATE(message_page, 1);
    ALLOCATE(event_page, 1);
    ALLOCATE(ring_pages, HYPERV_RING_PAGES);
    ALLOCATE(bounce_pages, HYPERV_BOUNCE_PAGES);

#undef ALLOCATE
    platform.resources_allocated = true;
    return IOS_OK;
}

ios_status block_hyperv_initialize_primary(
    struct ios_block_device *device, const ios_u8 boot_partition_guid[16]
)
{
    const struct ios_vmbus_offer *storage_offer;
    struct ios_vmbus_resources resources;
    ios_status status;

    if (device == NULL || boot_partition_guid == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    memset(&platform, 0, sizeof(platform));
    last_stage = "allocate";
    status = allocate_shared_pages();
    if (IOS_FAILED(status)) return status;

    resources = (struct ios_vmbus_resources) {
        .hypercall_page = (void *)platform.hypercall_page,
        .hypercall_page_physical = platform.hypercall_page,
        .post_message = (void *)platform.post_page,
        .post_message_physical = platform.post_page,
        .message_page = (void *)platform.message_page,
        .message_page_physical = platform.message_page,
        .event_page = (void *)platform.event_page,
        .event_page_physical = platform.event_page
    };
    last_stage = "vmbus";
    status = vmbus_initialize(&platform.bus, &resources,
        UINT64_C(0x0001000000000001), IOS_VMBUS_DEFAULT_SPIN_LIMIT);
    if (IOS_FAILED(status)) {
        last_stage = vmbus_last_stage(&platform.bus);
        return status;
    }
    status = vmbus_request_offers(&platform.bus, IOS_VMBUS_DEFAULT_SPIN_LIMIT);
    if (IOS_FAILED(status)) {
        last_stage = vmbus_last_stage(&platform.bus);
        return status;
    }
    storage_offer = vmbus_find_offer(
        &platform.bus, &IOS_HV_GUID_SYNTHETIC_SCSI, 0);
    if (storage_offer == NULL) {
        last_stage = "storage_offer";
        return IOS_ERROR(IOS_E_NOT_FOUND);
    }
    last_stage = "storage_channel";
    status = vmbus_open_channel(&platform.bus, storage_offer,
        &platform.storage_channel, (void *)platform.ring_pages, platform.ring_pages,
        HYPERV_RING_PAGES, HYPERV_OUTBOUND_RING_PAGES,
        IOS_VMBUS_DEFAULT_SPIN_LIMIT);
    if (IOS_FAILED(status)) return status;

    last_stage = "storvsc_protocol";
    status = storvsc_initialize(&platform.storage, &platform.bus,
        &platform.storage_channel, (void *)platform.bounce_pages, platform.bounce_pages,
        HYPERV_BOUNCE_PAGES * IOS_HV_PAGE_SIZE, 0, IOS_VMBUS_DEFAULT_SPIN_LIMIT);
    if (IOS_FAILED(status) && platform.storage.protocol_version == 0) {
        last_stage = storvsc_last_stage(&platform.storage);
        return status;
    }

    last_stage = "disk_safety";
    for (ios_u32 lun = 0; lun <= HYPERV_MAX_LUN; ++lun) {
        struct ios_block_device candidate;
        enum ios_block_disk_classification classification;

        if (lun != 0 || IOS_FAILED(status)) {
            status = storvsc_select_lun(&platform.storage, (ios_u8)lun);
        }
        if (IOS_FAILED(status)) continue;
        status = storvsc_publish(&platform.storage, &candidate);
        if (IOS_FAILED(status)) return status;
        status = block_classify_data_disk(
            &candidate, boot_partition_guid, &classification);
        if (IOS_FAILED(status)) continue;
        if (classification == IOS_BLOCK_DISK_ELIGIBLE_BLANK
            || classification == IOS_BLOCK_DISK_ELIGIBLE_INFERENCE_FS) {
            *device = candidate;
            last_stage = "ready";
            return IOS_OK;
        }
    }
    return IOS_ERROR(IOS_E_NOT_FOUND);
}

const char *block_hyperv_last_stage(void)
{
    return last_stage;
}

ios_status hyperv_platform_input_initialize(struct ios_input_queue *queue)
{
    const struct ios_vmbus_offer *offer;
    ios_status keyboard_status = IOS_ERROR(IOS_E_NOT_FOUND);
    ios_status mouse_status = IOS_ERROR(IOS_E_NOT_FOUND);

    if (queue == NULL || platform.bus.state != IOS_VMBUS_CONNECTED) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    offer = vmbus_find_offer(&platform.bus, &IOS_HV_GUID_SYNTHETIC_KEYBOARD, 0);
    keyboard_stage = offer == NULL ? "offer_missing" : "allocate";
    if (offer != NULL
        && IOS_SUCCEEDED(physical_allocate_pages(
            HYPERV_RING_PAGES, 1, &platform.keyboard_ring_pages))) {
        keyboard_stage = "channel";
        keyboard_status = vmbus_open_channel(&platform.bus, offer,
            &platform.keyboard_channel, (void *)platform.keyboard_ring_pages,
            platform.keyboard_ring_pages, HYPERV_RING_PAGES,
            HYPERV_OUTBOUND_RING_PAGES, IOS_VMBUS_DEFAULT_SPIN_LIMIT);
        if (IOS_SUCCEEDED(keyboard_status)) {
            keyboard_stage = "protocol";
            keyboard_status = hyperv_keyboard_initialize(&platform.keyboard,
                &platform.bus, &platform.keyboard_channel, queue,
                IOS_VMBUS_DEFAULT_SPIN_LIMIT);
        }
        platform.keyboard_ready = IOS_SUCCEEDED(keyboard_status);
        if (platform.keyboard_ready) keyboard_stage = "ready";
    }
    keyboard_last_status = keyboard_status;

    offer = vmbus_find_offer(&platform.bus, &IOS_HV_GUID_SYNTHETIC_MOUSE, 0);
    mouse_stage = offer == NULL ? "offer_missing" : "allocate";
    if (offer != NULL
        && IOS_SUCCEEDED(physical_allocate_pages(
            HYPERV_RING_PAGES, 1, &platform.mouse_ring_pages))) {
        mouse_stage = "channel";
        mouse_status = vmbus_open_channel(&platform.bus, offer,
            &platform.mouse_channel, (void *)platform.mouse_ring_pages,
            platform.mouse_ring_pages, HYPERV_RING_PAGES,
            HYPERV_OUTBOUND_RING_PAGES, IOS_VMBUS_DEFAULT_SPIN_LIMIT);
        if (IOS_SUCCEEDED(mouse_status)) {
            mouse_stage = "protocol";
            mouse_status = hyperv_mouse_initialize(&platform.mouse,
                &platform.bus, &platform.mouse_channel, queue,
                IOS_VMBUS_DEFAULT_SPIN_LIMIT);
        }
        platform.mouse_ready = IOS_SUCCEEDED(mouse_status);
        if (platform.mouse_ready) mouse_stage = "ready";
    }
    mouse_last_status = mouse_status;
    return platform.keyboard_ready || platform.mouse_ready
        ? IOS_OK : (IOS_FAILED(keyboard_status) ? keyboard_status : mouse_status);
}

const char *hyperv_platform_keyboard_stage(void)
{
    return keyboard_stage;
}

ios_status hyperv_platform_keyboard_status(void)
{
    return keyboard_last_status;
}

const char *hyperv_platform_mouse_stage(void)
{
    return mouse_stage;
}

ios_status hyperv_platform_mouse_status(void)
{
    return mouse_last_status;
}

void hyperv_platform_input_poll(ios_u64 timestamp_ticks)
{
    ios_status status;

    if (platform.keyboard_ready) {
        do {
            status = hyperv_keyboard_poll(&platform.keyboard, timestamp_ticks);
        } while (IOS_SUCCEEDED(status));
        if (status != IOS_ERROR(IOS_E_WOULD_BLOCK)
            && status != IOS_ERROR(IOS_E_NOT_SUPPORTED)) {
            platform.keyboard_ready = false;
        }
    }
    if (platform.mouse_ready) {
        do {
            status = hyperv_mouse_poll(&platform.mouse, timestamp_ticks);
        } while (IOS_SUCCEEDED(status));
        if (status != IOS_ERROR(IOS_E_WOULD_BLOCK)
            && status != IOS_ERROR(IOS_E_NOT_SUPPORTED)) {
            platform.mouse_ready = false;
        }
    }
}
