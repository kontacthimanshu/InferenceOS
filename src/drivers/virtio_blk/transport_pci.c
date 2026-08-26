#include <inferenceos/drivers/virtio_blk.h>

#include <inferenceos/arch/io.h>
#include <inferenceos/arch/paging.h>
#include <inferenceos/memory.h>
#include <inferenceos/runtime.h>

enum {
    VIRTIO_PCI_CAPABILITY_ID = 0x09,
    VIRTIO_PCI_CAP_COMMON = 1,
    VIRTIO_PCI_CAP_NOTIFY = 2,
    VIRTIO_PCI_CAP_ISR = 3,
    VIRTIO_PCI_CAP_DEVICE = 4,
    VIRTIO_PCI_COMMON_MINIMUM_SIZE = 56,
    VIRTIO_PCI_NOTIFY_MINIMUM_SIZE = 2,
    VIRTIO_PCI_ISR_MINIMUM_SIZE = 1,
    VIRTIO_BLK_CONFIG_MINIMUM_SIZE = 8,
    VIRTIO_STATUS_ACKNOWLEDGE = 1,
    VIRTIO_STATUS_DRIVER = 2,
    VIRTIO_STATUS_DRIVER_OK = 4,
    VIRTIO_STATUS_FEATURES_OK = 8,
    VIRTIO_STATUS_DEVICE_NEEDS_RESET = 64,
    VIRTIO_STATUS_FAILED = 128,
    VIRTQ_DESC_F_NEXT = 1,
    VIRTQ_DESC_F_WRITE = 2,
    VIRTIO_PCI_QUEUE_INDEX = 0,
    VIRTIO_PCI_DESCRIPTOR_LIMIT = 256,
    VIRTIO_PCI_DESCRIPTORS_PER_REQUEST = 3,
    PCI_COMMAND_OFFSET = 0x04,
    PCI_COMMAND_MEMORY = 1 << 1,
    PCI_COMMAND_BUS_MASTER = 1 << 2
};

enum virtio_common_offset {
    VIRTIO_COMMON_DEVICE_FEATURE_SELECT = 0,
    VIRTIO_COMMON_DEVICE_FEATURE = 4,
    VIRTIO_COMMON_DRIVER_FEATURE_SELECT = 8,
    VIRTIO_COMMON_DRIVER_FEATURE = 12,
    VIRTIO_COMMON_DEVICE_STATUS = 20,
    VIRTIO_COMMON_CONFIG_GENERATION = 21,
    VIRTIO_COMMON_QUEUE_SELECT = 22,
    VIRTIO_COMMON_QUEUE_SIZE = 24,
    VIRTIO_COMMON_QUEUE_ENABLE = 28,
    VIRTIO_COMMON_QUEUE_NOTIFY_OFFSET = 30,
    VIRTIO_COMMON_QUEUE_DESCRIPTOR = 32,
    VIRTIO_COMMON_QUEUE_DRIVER = 40,
    VIRTIO_COMMON_QUEUE_DEVICE = 48
};

struct virtio_pci_region {
    ios_uptr address;
    ios_u32 byte_count;
};

struct virtq_descriptor {
    ios_u64 address;
    ios_u32 length;
    ios_u16 flags;
    ios_u16 next;
};

struct virtq_available {
    ios_u16 flags;
    ios_u16 index;
    ios_u16 ring[VIRTIO_PCI_DESCRIPTOR_LIMIT];
    ios_u16 used_event;
};

struct virtq_used_element {
    ios_u32 identifier;
    ios_u32 length;
};

struct virtq_used {
    ios_u16 flags;
    ios_u16 index;
    struct virtq_used_element ring[VIRTIO_PCI_DESCRIPTOR_LIMIT];
    ios_u16 available_event;
};

struct virtio_blk_request_header {
    ios_u32 type;
    ios_u32 reserved;
    ios_u64 sector;
};

struct virtio_blk_pci_state {
    struct ios_virtio_blk_pci_platform platform;
    struct ios_pci_function function;
    struct virtio_pci_region common;
    struct virtio_pci_region notify;
    struct virtio_pci_region isr;
    struct virtio_pci_region device_configuration;
    struct ios_virtio_blk_device device;
    struct ios_block_device *published_device;
    ios_u32 notify_multiplier;
    ios_u16 descriptor_count;
    ios_u16 last_used_index;
    ios_uptr notify_address;
    struct ios_virtio_blk_request requests[IOS_VIRTIO_BLK_PCI_REQUEST_LIMIT];
    struct virtq_descriptor descriptors[VIRTIO_PCI_DESCRIPTOR_LIMIT] IOS_ALIGNED(16);
    struct virtq_available available IOS_ALIGNED(2);
    struct virtq_used used IOS_ALIGNED(4);
    struct virtio_blk_request_header headers[IOS_VIRTIO_BLK_PCI_REQUEST_LIMIT]
        IOS_ALIGNED(16);
    ios_u8 statuses[IOS_VIRTIO_BLK_PCI_REQUEST_LIMIT] IOS_ALIGNED(16);
};

IOS_STATIC_ASSERT(sizeof(struct virtq_descriptor) == 16, "virtqueue descriptor layout");
IOS_STATIC_ASSERT(sizeof(struct virtio_blk_request_header) == 16, "virtio-blk header layout");

static struct virtio_blk_pci_state pci_state;
static struct ios_pci_function discovered_functions[IOS_PCI_MAX_FUNCTIONS];
static const char *pci_last_stage = "not_started";

const char *virtio_blk_pci_last_stage(void)
{
    return pci_last_stage;
}

static ios_u8 default_mmio_read8(void *context, ios_uptr address)
{
    (void)context;
    return *(volatile ios_u8 *)address;
}

static ios_u16 default_mmio_read16(void *context, ios_uptr address)
{
    (void)context;
    return *(volatile ios_u16 *)address;
}

static ios_u32 default_mmio_read32(void *context, ios_uptr address)
{
    (void)context;
    return *(volatile ios_u32 *)address;
}

static void default_mmio_write8(void *context, ios_uptr address, ios_u8 value)
{
    (void)context;
    *(volatile ios_u8 *)address = value;
}

static void default_mmio_write16(void *context, ios_uptr address, ios_u16 value)
{
    (void)context;
    *(volatile ios_u16 *)address = value;
}

static void default_mmio_write32(void *context, ios_uptr address, ios_u32 value)
{
    (void)context;
    *(volatile ios_u32 *)address = value;
}

static void default_memory_barrier(void *context)
{
    (void)context;
    x86_64_memory_barrier();
}

static void default_relax(void *context)
{
    (void)context;
    x86_64_cpu_relax();
}

static ios_status default_dma_address(
    void *context,
    const void *buffer,
    ios_size byte_count,
    ios_uptr *physical_address
)
{
    const ios_uptr address = (ios_uptr)buffer;
    const ios_uptr physical_limit = UINT64_C(0x0010000000000000);

    (void)context;
    if (buffer == NULL || byte_count == 0 || physical_address == NULL
        || address >= physical_limit || byte_count > physical_limit - address) {
        return IOS_ERROR(IOS_E_BAD_ADDRESS);
    }
    *physical_address = address;
    return IOS_OK;
}

static bool platform_is_valid(const struct ios_virtio_blk_pci_platform *platform)
{
    return platform != NULL && platform->pci.read32 != NULL
        && platform->pci.write32 != NULL && platform->mmio_read8 != NULL
        && platform->mmio_read16 != NULL && platform->mmio_read32 != NULL
        && platform->mmio_write8 != NULL && platform->mmio_write16 != NULL
        && platform->mmio_write32 != NULL && platform->memory_barrier != NULL
        && platform->relax != NULL && platform->dma_address != NULL;
}

static ios_u8 read8(const struct virtio_blk_pci_state *state, ios_uptr address)
{
    return state->platform.mmio_read8(state->platform.context, address);
}

static ios_u16 read16(const struct virtio_blk_pci_state *state, ios_uptr address)
{
    return state->platform.mmio_read16(state->platform.context, address);
}

static ios_u32 read32(const struct virtio_blk_pci_state *state, ios_uptr address)
{
    return state->platform.mmio_read32(state->platform.context, address);
}

static void write8(const struct virtio_blk_pci_state *state, ios_uptr address, ios_u8 value)
{
    state->platform.mmio_write8(state->platform.context, address, value);
}

static void write16(const struct virtio_blk_pci_state *state, ios_uptr address, ios_u16 value)
{
    state->platform.mmio_write16(state->platform.context, address, value);
}

static void write32(const struct virtio_blk_pci_state *state, ios_uptr address, ios_u32 value)
{
    state->platform.mmio_write32(state->platform.context, address, value);
}

static void write64(const struct virtio_blk_pci_state *state, ios_uptr address, ios_u64 value)
{
    write32(state, address, (ios_u32)value);
    write32(state, address + 4U, (ios_u32)(value >> 32));
}

static void memory_barrier(const struct virtio_blk_pci_state *state)
{
    state->platform.memory_barrier(state->platform.context);
}

static ios_status dma_address(
    const struct virtio_blk_pci_state *state,
    const void *buffer,
    ios_size byte_count,
    ios_uptr *address
)
{
    return state->platform.dma_address(
        state->platform.context, buffer, byte_count, address
    );
}

static ios_status capability_region(
    struct virtio_blk_pci_state *state,
    ios_u8 configuration_type,
    ios_u8 bar_index,
    ios_u32 offset,
    ios_u32 length,
    struct virtio_pci_region **region
)
{
    const ios_u64 physical_limit = UINT64_C(0x0010000000000000);
    struct ios_pci_bar *bar;
    ios_u64 mapped_address;

    if (configuration_type < VIRTIO_PCI_CAP_COMMON
        || configuration_type > VIRTIO_PCI_CAP_DEVICE) {
        return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    }
    if (bar_index >= IOS_PCI_BAR_COUNT || length == 0) {
        pci_last_stage = "capability_region_identity";
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    bar = &state->function.bars[bar_index];
    if (!bar->memory || bar->address == 0 || bar->byte_count == 0
        || offset > bar->byte_count || length > bar->byte_count - offset
        || bar->address > UINT64_MAX - offset) {
        pci_last_stage = "capability_region_bounds";
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    mapped_address = bar->address + offset;
    if (mapped_address >= physical_limit || length > physical_limit - mapped_address) {
        pci_last_stage = "capability_region_address";
        return IOS_ERROR(IOS_E_BAD_ADDRESS);
    }
    if ((configuration_type == VIRTIO_PCI_CAP_COMMON && (offset & 3U) != 0)
        || (configuration_type == VIRTIO_PCI_CAP_NOTIFY && (offset & 1U) != 0)
        || (configuration_type == VIRTIO_PCI_CAP_DEVICE && (offset & 3U) != 0)) {
        pci_last_stage = "capability_region_alignment";
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    if (configuration_type == VIRTIO_PCI_CAP_COMMON) {
        *region = &state->common;
    } else if (configuration_type == VIRTIO_PCI_CAP_NOTIFY) {
        *region = &state->notify;
    } else if (configuration_type == VIRTIO_PCI_CAP_ISR) {
        *region = &state->isr;
    } else if (configuration_type == VIRTIO_PCI_CAP_DEVICE) {
        *region = &state->device_configuration;
    } else {
        return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    }
    if ((*region)->address != 0) {
        pci_last_stage = "capability_region_duplicate";
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    (*region)->address = (ios_uptr)mapped_address;
    (*region)->byte_count = length;
    return IOS_OK;
}

static ios_status parse_capabilities(
    struct virtio_blk_pci_state *state,
    const struct ios_pci_config_access *access
)
{
    ios_u8 pointer = state->function.capability_pointer;
    ios_u64 visited = 0;
    ios_status status;

    while (pointer != 0) {
        ios_u32 header;
        ios_u32 bar_data;
        ios_u32 offset;
        ios_u32 length;
        ios_u8 capability_id;
        ios_u8 next;
        ios_u8 capability_length;
        ios_u8 configuration_type;
        ios_u8 bar_index;
        struct virtio_pci_region *region;
        const ios_u8 visited_index = (ios_u8)(pointer / 4U);

        if (pointer < 0x40 || pointer > 0xf0 || (pointer & 3U) != 0
            || (visited & (UINT64_C(1) << visited_index)) != 0) {
            pci_last_stage = "capability_pointer";
            return IOS_ERROR(IOS_E_PROTOCOL);
        }
        visited |= UINT64_C(1) << visited_index;
        status = x86_64_pci_config_read32(access, &state->function, pointer, &header);
        if (IOS_FAILED(status)) {
            return status;
        }
        capability_id = (ios_u8)header;
        next = (ios_u8)(header >> 8);
        capability_length = (ios_u8)(header >> 16);
        configuration_type = (ios_u8)(header >> 24);
        if (capability_id == VIRTIO_PCI_CAPABILITY_ID) {
            if (capability_length < 16 || pointer > 0xf0) {
                pci_last_stage = "capability_length";
                return IOS_ERROR(IOS_E_PROTOCOL);
            }
            status = x86_64_pci_config_read32(
                access, &state->function, pointer + 4U, &bar_data
            );
            if (IOS_FAILED(status)) {
                return status;
            }
            status = x86_64_pci_config_read32(
                access, &state->function, pointer + 8U, &offset
            );
            if (IOS_FAILED(status)) {
                return status;
            }
            status = x86_64_pci_config_read32(
                access, &state->function, pointer + 12U, &length
            );
            if (IOS_FAILED(status)) {
                return status;
            }
            bar_index = (ios_u8)bar_data;
            status = capability_region(
                state, configuration_type, bar_index, offset, length, &region
            );
            if (status != IOS_ERROR(IOS_E_NOT_SUPPORTED) && IOS_FAILED(status)) {
                return status;
            }
            if (IOS_SUCCEEDED(status) && configuration_type == VIRTIO_PCI_CAP_NOTIFY) {
                ios_u32 multiplier;

                if (capability_length < 20) {
                    pci_last_stage = "capability_notify_length";
                    return IOS_ERROR(IOS_E_PROTOCOL);
                }
                status = x86_64_pci_config_read32(
                    access, &state->function, pointer + 16U, &multiplier
                );
                if (IOS_FAILED(status)) {
                    return status;
                }
                if (multiplier == 0) {
                    pci_last_stage = "capability_notify_multiplier";
                    return IOS_ERROR(IOS_E_PROTOCOL);
                }
                state->notify_multiplier = multiplier;
            }
        }
        pointer = next;
    }
    if (state->common.address == 0
        || state->common.byte_count < VIRTIO_PCI_COMMON_MINIMUM_SIZE) {
        pci_last_stage = "capabilities_common";
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    if (state->notify.address == 0
        || state->notify.byte_count < VIRTIO_PCI_NOTIFY_MINIMUM_SIZE
        || state->notify_multiplier == 0) {
        pci_last_stage = "capabilities_notify";
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    if (state->isr.address == 0
        || state->isr.byte_count < VIRTIO_PCI_ISR_MINIMUM_SIZE) {
        pci_last_stage = "capabilities_isr";
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    if (state->device_configuration.address == 0
        || state->device_configuration.byte_count < VIRTIO_BLK_CONFIG_MINIMUM_SIZE) {
        pci_last_stage = "capabilities_device";
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    state->function.has_common_configuration = true;
    state->function.has_notify_configuration = true;
    state->function.has_isr_configuration = true;
    state->function.has_device_configuration = true;
    return IOS_OK;
}

static ios_status pci_reset(void *opaque, const struct ios_pci_function *function)
{
    struct virtio_blk_pci_state *state = opaque;
    ios_u8 device_status;

    (void)function;
    write8(state, state->common.address + VIRTIO_COMMON_DEVICE_STATUS, 0);
    for (ios_size spin = 0; spin < IOS_VIRTIO_BLK_PCI_TIMEOUT_SPINS; ++spin) {
        device_status = read8(state, state->common.address + VIRTIO_COMMON_DEVICE_STATUS);
        if (device_status == 0) {
            write8(
                state,
                state->common.address + VIRTIO_COMMON_DEVICE_STATUS,
                VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER
            );
            return IOS_OK;
        }
        state->platform.relax(state->platform.context);
    }
    return IOS_ERROR(IOS_E_TIMEOUT);
}

static ios_status pci_read_features(void *opaque, ios_u64 *features)
{
    struct virtio_blk_pci_state *state = opaque;
    ios_u32 low;
    ios_u32 high;

    if (features == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    write32(state, state->common.address + VIRTIO_COMMON_DEVICE_FEATURE_SELECT, 0);
    low = read32(state, state->common.address + VIRTIO_COMMON_DEVICE_FEATURE);
    write32(state, state->common.address + VIRTIO_COMMON_DEVICE_FEATURE_SELECT, 1);
    high = read32(state, state->common.address + VIRTIO_COMMON_DEVICE_FEATURE);
    *features = ((ios_u64)high << 32) | low;
    return IOS_OK;
}

static ios_status pci_write_features(void *opaque, ios_u64 features)
{
    struct virtio_blk_pci_state *state = opaque;
    ios_u8 status;

    write32(state, state->common.address + VIRTIO_COMMON_DRIVER_FEATURE_SELECT, 0);
    write32(state, state->common.address + VIRTIO_COMMON_DRIVER_FEATURE, (ios_u32)features);
    write32(state, state->common.address + VIRTIO_COMMON_DRIVER_FEATURE_SELECT, 1);
    write32(
        state, state->common.address + VIRTIO_COMMON_DRIVER_FEATURE, (ios_u32)(features >> 32)
    );
    status = read8(state, state->common.address + VIRTIO_COMMON_DEVICE_STATUS);
    status = (ios_u8)(status | VIRTIO_STATUS_FEATURES_OK);
    write8(state, state->common.address + VIRTIO_COMMON_DEVICE_STATUS, status);
    status = read8(state, state->common.address + VIRTIO_COMMON_DEVICE_STATUS);
    return (status & VIRTIO_STATUS_FEATURES_OK) != 0
        ? IOS_OK : IOS_ERROR(IOS_E_NOT_SUPPORTED);
}

static ios_u16 next_power_of_two(ios_u16 value)
{
    ios_u16 result = 1;

    while (result < value && result < VIRTIO_PCI_DESCRIPTOR_LIMIT) {
        result = (ios_u16)(result << 1);
    }
    return result;
}

static ios_status pci_setup_queue(void *opaque, ios_u16 queue_index, ios_u16 request_count)
{
    struct virtio_blk_pci_state *state = opaque;
    ios_uptr descriptor_address;
    ios_uptr available_address;
    ios_uptr used_address;
    ios_u16 maximum;
    ios_u16 descriptor_count;
    ios_u16 notify_offset;
    ios_u64 byte_offset;
    ios_status status;

    if (queue_index != VIRTIO_PCI_QUEUE_INDEX || request_count == 0
        || request_count > IOS_VIRTIO_BLK_PCI_REQUEST_LIMIT) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    descriptor_count = next_power_of_two(
        (ios_u16)(request_count * VIRTIO_PCI_DESCRIPTORS_PER_REQUEST)
    );
    if (descriptor_count < request_count * VIRTIO_PCI_DESCRIPTORS_PER_REQUEST
        || descriptor_count > VIRTIO_PCI_DESCRIPTOR_LIMIT) {
        return IOS_ERROR(IOS_E_NO_SPACE);
    }
    write16(state, state->common.address + VIRTIO_COMMON_QUEUE_SELECT, queue_index);
    maximum = read16(state, state->common.address + VIRTIO_COMMON_QUEUE_SIZE);
    if (maximum < descriptor_count || read16(
            state, state->common.address + VIRTIO_COMMON_QUEUE_ENABLE
        ) != 0) {
        return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    }
    memset(state->descriptors, 0, sizeof(state->descriptors));
    memset(&state->available, 0, sizeof(state->available));
    memset(&state->used, 0, sizeof(state->used));
    memset(state->headers, 0, sizeof(state->headers));
    memset(state->statuses, UINT8_C(0xff), sizeof(state->statuses));
    status = dma_address(
        state, state->descriptors, descriptor_count * sizeof(*state->descriptors),
        &descriptor_address
    );
    if (IOS_FAILED(status)) {
        return status;
    }
    status = dma_address(state, &state->available, sizeof(state->available), &available_address);
    if (IOS_FAILED(status)) {
        return status;
    }
    status = dma_address(state, &state->used, sizeof(state->used), &used_address);
    if (IOS_FAILED(status)) {
        return status;
    }
    write16(state, state->common.address + VIRTIO_COMMON_QUEUE_SIZE, descriptor_count);
    write64(state, state->common.address + VIRTIO_COMMON_QUEUE_DESCRIPTOR, descriptor_address);
    write64(state, state->common.address + VIRTIO_COMMON_QUEUE_DRIVER, available_address);
    write64(state, state->common.address + VIRTIO_COMMON_QUEUE_DEVICE, used_address);
    notify_offset = read16(state, state->common.address + VIRTIO_COMMON_QUEUE_NOTIFY_OFFSET);
    byte_offset = (ios_u64)notify_offset * state->notify_multiplier;
    if (notify_offset != 0 && byte_offset / notify_offset != state->notify_multiplier) {
        return IOS_ERROR(IOS_E_OVERFLOW);
    }
    if (byte_offset > state->notify.byte_count
        || sizeof(ios_u16) > state->notify.byte_count - byte_offset
        || state->notify.address > UINT64_MAX - byte_offset) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    state->notify_address = state->notify.address + (ios_uptr)byte_offset;
    state->descriptor_count = descriptor_count;
    state->last_used_index = 0;
    write16(state, state->common.address + VIRTIO_COMMON_QUEUE_ENABLE, 1);
    return IOS_OK;
}

static ios_status pci_set_driver_ok(void *opaque)
{
    struct virtio_blk_pci_state *state = opaque;
    ios_u8 status = read8(state, state->common.address + VIRTIO_COMMON_DEVICE_STATUS);

    if ((status & VIRTIO_STATUS_FEATURES_OK) == 0) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    status = (ios_u8)(status | VIRTIO_STATUS_DRIVER_OK);
    write8(state, state->common.address + VIRTIO_COMMON_DEVICE_STATUS, status);
    status = read8(state, state->common.address + VIRTIO_COMMON_DEVICE_STATUS);
    return (status & VIRTIO_STATUS_DRIVER_OK) != 0 ? IOS_OK : IOS_ERROR(IOS_E_PROTOCOL);
}

static ios_status pci_read_capacity(void *opaque, ios_u64 *sector_count)
{
    struct virtio_blk_pci_state *state = opaque;

    if (sector_count == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    for (ios_size spin = 0; spin < IOS_VIRTIO_BLK_PCI_TIMEOUT_SPINS; ++spin) {
        const ios_u8 before = read8(
            state, state->common.address + VIRTIO_COMMON_CONFIG_GENERATION
        );
        const ios_u32 low = read32(state, state->device_configuration.address);
        const ios_u32 high = read32(state, state->device_configuration.address + 4U);
        const ios_u8 after = read8(
            state, state->common.address + VIRTIO_COMMON_CONFIG_GENERATION
        );

        if (before == after) {
            *sector_count = ((ios_u64)high << 32) | low;
            return IOS_OK;
        }
        state->platform.relax(state->platform.context);
    }
    return IOS_ERROR(IOS_E_TIMEOUT);
}

static ios_status set_descriptor(
    struct virtio_blk_pci_state *state,
    ios_u16 index,
    const void *buffer,
    ios_size byte_count,
    ios_u16 flags,
    ios_u16 next
)
{
    ios_uptr address;
    ios_status status = dma_address(state, buffer, byte_count, &address);

    if (IOS_FAILED(status)) {
        return status;
    }
    state->descriptors[index].address = address;
    state->descriptors[index].length = (ios_u32)byte_count;
    state->descriptors[index].flags = flags;
    state->descriptors[index].next = next;
    return IOS_OK;
}

static ios_status pci_publish(
    void *opaque,
    ios_u16 request_id,
    const struct ios_virtio_blk_request *request
)
{
    struct virtio_blk_pci_state *state = opaque;
    const ios_u16 header_descriptor = (ios_u16)(request_id * 3U);
    const ios_u16 data_descriptor = (ios_u16)(header_descriptor + 1U);
    const ios_u16 status_descriptor = (ios_u16)(header_descriptor + 2U);
    volatile struct virtq_available *available = &state->available;
    ios_status status;

    if (request == NULL || request_id >= IOS_VIRTIO_BLK_PCI_REQUEST_LIMIT
        || status_descriptor >= state->descriptor_count) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    state->headers[request_id].type = request->type;
    state->headers[request_id].reserved = 0;
    state->headers[request_id].sector = request->sector;
    state->statuses[request_id] = UINT8_C(0xff);
    status = set_descriptor(
        state,
        header_descriptor,
        &state->headers[request_id],
        sizeof(state->headers[request_id]),
        VIRTQ_DESC_F_NEXT,
        request->type == IOS_VIRTIO_BLK_REQUEST_FLUSH
            ? status_descriptor : data_descriptor
    );
    if (IOS_FAILED(status)) {
        return status;
    }
    if (request->type != IOS_VIRTIO_BLK_REQUEST_FLUSH) {
        const ios_u16 flags = (ios_u16)(VIRTQ_DESC_F_NEXT
            | (request->type == IOS_VIRTIO_BLK_REQUEST_READ ? VIRTQ_DESC_F_WRITE : 0));
        status = set_descriptor(
            state,
            data_descriptor,
            request->buffer,
            request->byte_count,
            flags,
            status_descriptor
        );
        if (IOS_FAILED(status)) {
            return status;
        }
    }
    status = set_descriptor(
        state,
        status_descriptor,
        &state->statuses[request_id],
        sizeof(state->statuses[request_id]),
        VIRTQ_DESC_F_WRITE,
        0
    );
    if (IOS_FAILED(status)) {
        return status;
    }
    available->ring[available->index % state->descriptor_count] = header_descriptor;
    memory_barrier(state);
    available->index = (ios_u16)(available->index + 1U);
    memory_barrier(state);
    write16(state, state->notify_address, VIRTIO_PCI_QUEUE_INDEX);
    return IOS_OK;
}

static ios_status pci_service(void *opaque, struct ios_virtio_blk_device *device)
{
    struct virtio_blk_pci_state *state = opaque;
    volatile struct virtq_used *used = &state->used;
    ios_u8 device_status;

    (void)read8(state, state->isr.address);
    device_status = read8(state, state->common.address + VIRTIO_COMMON_DEVICE_STATUS);
    if ((device_status & (VIRTIO_STATUS_DEVICE_NEEDS_RESET | VIRTIO_STATUS_FAILED)) != 0) {
        return IOS_ERROR(IOS_E_IO);
    }
    memory_barrier(state);
    while (state->last_used_index != used->index) {
        const struct virtq_used_element element =
            used->ring[state->last_used_index % state->descriptor_count];
        ios_u16 request_id;
        ios_status status;

        if (element.identifier >= state->descriptor_count
            || element.identifier % VIRTIO_PCI_DESCRIPTORS_PER_REQUEST != 0) {
            return IOS_ERROR(IOS_E_PROTOCOL);
        }
        request_id = (ios_u16)(element.identifier / VIRTIO_PCI_DESCRIPTORS_PER_REQUEST);
        if (request_id >= device->queue_capacity) {
            return IOS_ERROR(IOS_E_PROTOCOL);
        }
        status = virtio_blk_complete(device, request_id, state->statuses[request_id]);
        if (IOS_FAILED(status)) {
            return status;
        }
        state->last_used_index = (ios_u16)(state->last_used_index + 1U);
    }
    return IOS_OK;
}

static void mark_transport_failed(struct virtio_blk_pci_state *state, ios_status status)
{
    ios_u8 device_status = read8(state, state->common.address + VIRTIO_COMMON_DEVICE_STATUS);

    write8(
        state,
        state->common.address + VIRTIO_COMMON_DEVICE_STATUS,
        (ios_u8)(device_status | VIRTIO_STATUS_FAILED)
    );
    virtio_blk_fail(&state->device, status);
    if (state->published_device != NULL) {
        state->published_device->status = IOS_BLOCK_DEVICE_FAILED;
    }
}

static ios_status wait_for_request(
    struct virtio_blk_pci_state *state,
    ios_u16 request_id
)
{
    for (ios_size spin = 0; spin < IOS_VIRTIO_BLK_PCI_TIMEOUT_SPINS; ++spin) {
        ios_status status = virtio_blk_poll(&state->device, request_id, NULL);

        if (status != IOS_ERROR(IOS_E_WOULD_BLOCK)) {
            if (status == IOS_ERROR(IOS_E_PROTOCOL)) {
                mark_transport_failed(state, status);
            }
            return status;
        }
        state->platform.relax(state->platform.context);
    }
    mark_transport_failed(state, IOS_ERROR(IOS_E_TIMEOUT));
    (void)virtio_blk_poll(&state->device, request_id, NULL);
    return IOS_ERROR(IOS_E_TIMEOUT);
}

static ios_status submit_and_wait(
    struct virtio_blk_pci_state *state,
    ios_u32 type,
    ios_u64 sector,
    void *buffer,
    ios_size byte_count
)
{
    ios_u16 request_id;
    ios_status status = virtio_blk_submit(
        &state->device, type, sector, buffer, byte_count, 0, &request_id
    );

    return IOS_FAILED(status) ? status : wait_for_request(state, request_id);
}

static ios_status block_read(
    void *opaque,
    ios_u64 first_sector,
    ios_size sector_count,
    void *buffer
)
{
    struct virtio_blk_pci_state *state = opaque;
    ios_u8 *bytes = buffer;

    if (sector_count > SIZE_MAX / IOS_VIRTIO_BLK_SECTOR_SIZE) {
        return IOS_ERROR(IOS_E_OVERFLOW);
    }
    for (ios_size index = 0; index < sector_count; ++index) {
        ios_status status = submit_and_wait(
            state,
            IOS_VIRTIO_BLK_REQUEST_READ,
            first_sector + index,
            bytes + index * IOS_VIRTIO_BLK_SECTOR_SIZE,
            IOS_VIRTIO_BLK_SECTOR_SIZE
        );
        if (IOS_FAILED(status)) {
            return status;
        }
    }
    return IOS_OK;
}

static ios_status block_write(
    void *opaque,
    ios_u64 first_sector,
    ios_size sector_count,
    const void *buffer
)
{
    struct virtio_blk_pci_state *state = opaque;
    const ios_u8 *bytes = buffer;

    if (sector_count > SIZE_MAX / IOS_VIRTIO_BLK_SECTOR_SIZE) {
        return IOS_ERROR(IOS_E_OVERFLOW);
    }
    for (ios_size index = 0; index < sector_count; ++index) {
        ios_status status = submit_and_wait(
            state,
            IOS_VIRTIO_BLK_REQUEST_WRITE,
            first_sector + index,
            (void *)(ios_uptr)(bytes + index * IOS_VIRTIO_BLK_SECTOR_SIZE),
            IOS_VIRTIO_BLK_SECTOR_SIZE
        );
        if (IOS_FAILED(status)) {
            return status;
        }
    }
    return IOS_OK;
}

static ios_status block_flush(void *opaque)
{
    return submit_and_wait(
        opaque, IOS_VIRTIO_BLK_REQUEST_FLUSH, 0, NULL, 0
    );
}

static ios_status enable_pci_function(
    const struct ios_pci_config_access *access,
    const struct ios_pci_function *function
)
{
    ios_u32 command_status;
    ios_status status = x86_64_pci_config_read32(
        access, function, PCI_COMMAND_OFFSET, &command_status
    );

    if (IOS_FAILED(status)) {
        return status;
    }
    return x86_64_pci_config_write32(
        access,
        function,
        PCI_COMMAND_OFFSET,
        (command_status & UINT32_C(0xffff)) | PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER
    );
}

static ios_status map_default_mmio_bars(struct virtio_blk_pci_state *state)
{
    struct ios_address_space kernel = { x86_64_paging_root() };
    for (ios_size index = 0; index < IOS_PCI_BAR_COUNT; ++index) {
        const struct ios_pci_bar *bar = &state->function.bars[index];
        ios_uptr first;
        ios_uptr end;
        if (!bar->memory || bar->address == 0 || bar->byte_count == 0) continue;
        first = (ios_uptr)(bar->address & ~(ios_u64)(IOS_PAGE_SIZE - 1U));
        if (bar->address > UINT64_MAX - bar->byte_count
            || bar->address + bar->byte_count > UINT64_MAX - (IOS_PAGE_SIZE - 1U)) {
            return IOS_ERROR(IOS_E_OVERFLOW);
        }
        end = (ios_uptr)((bar->address + bar->byte_count + IOS_PAGE_SIZE - 1U)
            & ~(ios_u64)(IOS_PAGE_SIZE - 1U));
        for (ios_uptr page = first; page < end; page += IOS_PAGE_SIZE) {
            ios_uptr physical;
            ios_status status = virtual_translate(&kernel, page, &physical);
            if (IOS_SUCCEEDED(status)) {
                if (physical != page) return IOS_ERROR(IOS_E_INVALID_STATE);
                continue;
            }
            status = virtual_map_pages(
                &kernel, page, page, 1, IOS_VM_WRITE | IOS_VM_GLOBAL
            );
            if (IOS_FAILED(status)) return status;
        }
    }
    return IOS_OK;
}

ios_status virtio_blk_pci_initialize_with_platform(
    struct ios_block_device *block_device,
    const struct ios_virtio_blk_pci_platform *platform
)
{
    struct ios_virtio_blk_transport transport;
    struct ios_block_device_operations operations = {
        .read = block_read,
        .write = block_write,
        .flush = block_flush
    };
    ios_size function_count;
    ios_status status;

    if (block_device == NULL || !platform_is_valid(platform)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    memset(&pci_state, 0, sizeof(pci_state));
    pci_last_stage = "enumerate";
    pci_state.platform = *platform;
    status = x86_64_pci_enumerate_with_access(
        &platform->pci,
        discovered_functions,
        IOS_ARRAY_COUNT(discovered_functions),
        &function_count
    );
    if (IOS_FAILED(status)) {
        return status;
    }
    for (ios_size index = 0; index < function_count; ++index) {
        if (discovered_functions[index].vendor_id == IOS_VIRTIO_PCI_VENDOR_ID
            && discovered_functions[index].device_id == IOS_VIRTIO_BLK_MODERN_DEVICE_ID) {
            pci_state.function = discovered_functions[index];
            break;
        }
    }
    if (pci_state.function.vendor_id == 0) {
        pci_last_stage = "discover";
        return IOS_ERROR(IOS_E_NOT_FOUND);
    }
    pci_last_stage = "probe_bars";
    status = x86_64_pci_probe_bars(&platform->pci, &pci_state.function);
    if (IOS_FAILED(status)) {
        return status;
    }
    if (platform->mmio_read8 == default_mmio_read8
        && platform->mmio_write8 == default_mmio_write8) {
        pci_last_stage = "map_bars";
        status = map_default_mmio_bars(&pci_state);
        if (IOS_FAILED(status)) return status;
    }
    pci_last_stage = "capabilities";
    status = parse_capabilities(&pci_state, &platform->pci);
    if (IOS_FAILED(status)) {
        return status;
    }
    pci_last_stage = "enable";
    status = enable_pci_function(&platform->pci, &pci_state.function);
    if (IOS_FAILED(status)) {
        return status;
    }
    transport = (struct ios_virtio_blk_transport){
        .context = &pci_state,
        .reset = pci_reset,
        .read_features = pci_read_features,
        .write_features = pci_write_features,
        .setup_queue = pci_setup_queue,
        .set_driver_ok = pci_set_driver_ok,
        .read_capacity = pci_read_capacity,
        .publish = pci_publish,
        .service = pci_service
    };
    pci_last_stage = "initialize";
    status = virtio_blk_initialize(
        &pci_state.device,
        &pci_state.function,
        1,
        &transport,
        pci_state.requests,
        IOS_VIRTIO_BLK_PCI_REQUEST_LIMIT
    );
    if (IOS_FAILED(status)) {
        write8(
            &pci_state,
            pci_state.common.address + VIRTIO_COMMON_DEVICE_STATUS,
            VIRTIO_STATUS_FAILED
        );
        return status;
    }
    pci_last_stage = "publish";
    pci_state.published_device = block_device;
    status = block_device_initialize(
        block_device,
        &pci_state,
        &operations,
        IOS_VIRTIO_BLK_SECTOR_SIZE,
        pci_state.device.sector_count,
        IOS_BLOCK_DEVICE_READY
    );
    if (IOS_FAILED(status)) {
        mark_transport_failed(&pci_state, status);
    } else {
        pci_last_stage = "ready";
    }
    return status;
}

ios_status virtio_blk_pci_initialize(struct ios_block_device *block_device)
{
    const struct ios_pci_config_access *pci = x86_64_pci_default_access();
    const struct ios_virtio_blk_pci_platform platform = {
        .pci = *pci,
        .mmio_read8 = default_mmio_read8,
        .mmio_read16 = default_mmio_read16,
        .mmio_read32 = default_mmio_read32,
        .mmio_write8 = default_mmio_write8,
        .mmio_write16 = default_mmio_write16,
        .mmio_write32 = default_mmio_write32,
        .memory_barrier = default_memory_barrier,
        .relax = default_relax,
        .dma_address = default_dma_address,
        .context = NULL
    };

    return virtio_blk_pci_initialize_with_platform(block_device, &platform);
}
