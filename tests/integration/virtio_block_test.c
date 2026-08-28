#include <inferenceos/test.h>
#include <inferenceos/drivers/virtio_blk.h>
#include <inferenceos/fake_block.h>
#include <inferenceos/memory.h>
#include <string.h>

enum { TEST_QUEUE_CAPACITY = 8 };

ios_u64 x86_64_interrupt_save_disable(void)
{
    return 0;
}

void x86_64_interrupt_restore(ios_u64 flags)
{
    (void)flags;
}

void x86_64_port_write32(ios_u16 port, ios_u32 value)
{
    (void)port;
    (void)value;
}

ios_u32 x86_64_port_read32(ios_u16 port)
{
    (void)port;
    return UINT32_MAX;
}

void x86_64_memory_barrier(void)
{
}

void x86_64_cpu_relax(void)
{
}

ios_uptr x86_64_paging_root(void)
{
    return 0;
}

void x86_64_paging_activate(ios_uptr root_address)
{
    (void)root_address;
}

ios_status virtual_translate(
    const struct ios_address_space *address_space,
    ios_uptr virtual_address,
    ios_uptr *physical_address
)
{
    (void)address_space;
    (void)virtual_address;
    (void)physical_address;
    return IOS_ERROR(IOS_E_NOT_FOUND);
}

ios_status virtual_map_pages(
    struct ios_address_space *address_space,
    ios_uptr virtual_address,
    ios_uptr physical_address,
    ios_u64 page_count,
    ios_u32 flags
)
{
    (void)address_space;
    (void)virtual_address;
    (void)physical_address;
    (void)page_count;
    (void)flags;
    return IOS_OK;
}

struct test_transport {
    struct ios_fake_block *backend;
    ios_u64 offered_features;
    ios_u64 accepted_features;
    ios_u16 configured_queue_size;
    ios_u16 published_count;
    bool reset_seen;
    bool driver_ok;
};

static const struct ios_pci_function test_pci_functions[] = {
    { .vendor_id = 0x1234, .device_id = 0x5678 },
    { .bus = 0, .device = 4, .vendor_id = IOS_VIRTIO_PCI_VENDOR_ID,
      .device_id = IOS_VIRTIO_BLK_MODERN_DEVICE_ID,
      .has_common_configuration = true, .has_notify_configuration = true,
      .has_isr_configuration = true, .has_device_configuration = true }
};

static ios_status transport_reset(void *opaque, const struct ios_pci_function *function)
{
    struct test_transport *transport = opaque;
    if (function->device_id != IOS_VIRTIO_BLK_MODERN_DEVICE_ID) return IOS_ERROR(IOS_E_PROTOCOL);
    transport->reset_seen = true;
    return IOS_OK;
}
static ios_status transport_read_features(void *opaque, ios_u64 *features)
{
    *features = ((struct test_transport *)opaque)->offered_features;
    return IOS_OK;
}
static ios_status transport_write_features(void *opaque, ios_u64 features)
{
    ((struct test_transport *)opaque)->accepted_features = features;
    return IOS_OK;
}
static ios_status transport_setup_queue(void *opaque, ios_u16 queue_index, ios_u16 queue_size)
{
    struct test_transport *transport = opaque;
    if (queue_index != 0) return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    transport->configured_queue_size = queue_size;
    return IOS_OK;
}
static ios_status transport_set_driver_ok(void *opaque)
{
    ((struct test_transport *)opaque)->driver_ok = true;
    return IOS_OK;
}
static ios_status transport_read_capacity(void *opaque, ios_u64 *sector_count)
{
    *sector_count = ((struct test_transport *)opaque)->backend->sector_count;
    return IOS_OK;
}
static ios_status transport_publish(
    void *opaque, ios_u16 request_id, const struct ios_virtio_blk_request *request)
{
    struct test_transport *transport = opaque;
    (void)request_id;
    if (!transport->reset_seen || request->state != IOS_VIRTIO_BLK_REQUEST_AVAILABLE) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    ++transport->published_count;
    return IOS_OK;
}

static struct ios_virtio_blk_transport make_transport(struct test_transport *context)
{
    return (struct ios_virtio_blk_transport){
        .context = context,
        .reset = transport_reset,
        .read_features = transport_read_features,
        .write_features = transport_write_features,
        .setup_queue = transport_setup_queue,
        .set_driver_ok = transport_set_driver_ok,
        .read_capacity = transport_read_capacity,
        .publish = transport_publish,
        .service = NULL
    };
}
static ios_status initialize_device(
    struct ios_virtio_blk_device *device, struct ios_virtio_blk_request *requests,
    struct test_transport *context)
{
    struct ios_virtio_blk_transport transport = make_transport(context);
    return virtio_blk_initialize(device, test_pci_functions, IOS_ARRAY_COUNT(test_pci_functions),
                                 &transport, requests, TEST_QUEUE_CAPACITY);
}
static ios_status complete_backend_request(
    struct ios_virtio_blk_device *device, struct test_transport *transport, ios_u16 request_id)
{
    struct ios_virtio_blk_request *request = &device->requests[request_id];
    ios_status status;
    if (request->type == IOS_VIRTIO_BLK_REQUEST_READ) {
        status = fake_block_read(transport->backend, request->sector, 1, request->buffer);
    } else if (request->type == IOS_VIRTIO_BLK_REQUEST_WRITE) {
        status = fake_block_write(transport->backend, request->sector, 1, request->buffer);
    } else {
        status = fake_block_flush(transport->backend);
    }
    IOS_TEST_ASSERT_STATUS(virtio_blk_complete(device, request_id,
        IOS_SUCCEEDED(status) ? IOS_VIRTIO_BLK_STATUS_OK : IOS_VIRTIO_BLK_STATUS_IO_ERROR), IOS_OK);
    return status;
}

static void test_discovery_and_negotiation_require_modern_transport_and_flush(void)
{
    struct ios_fault_injector faults;
    struct ios_fake_block backend;
    struct ios_virtio_blk_device device;
    struct ios_virtio_blk_request requests[TEST_QUEUE_CAPACITY];
    struct test_transport context = { 0 };
    fault_injector_initialize(&faults);
    IOS_TEST_ASSERT_STATUS(fake_block_initialize(&backend, 1024, 16, &faults), IOS_OK);
    context.backend = &backend;
    context.offered_features = IOS_VIRTIO_F_VERSION_1;
    IOS_TEST_ASSERT_STATUS(initialize_device(&device, requests, &context), IOS_ERROR(IOS_E_NOT_SUPPORTED));
    context.offered_features = IOS_VIRTIO_BLK_F_FLUSH;
    IOS_TEST_ASSERT_STATUS(initialize_device(&device, requests, &context), IOS_ERROR(IOS_E_NOT_SUPPORTED));
    context.offered_features = IOS_VIRTIO_F_VERSION_1 | IOS_VIRTIO_BLK_F_FLUSH | (UINT64_C(1) << 28);
    IOS_TEST_ASSERT_STATUS(initialize_device(&device, requests, &context), IOS_OK);
    IOS_TEST_ASSERT(device.pci.device == 4 && device.ready && context.driver_ok);
    IOS_TEST_ASSERT(context.accepted_features == (IOS_VIRTIO_F_VERSION_1 | IOS_VIRTIO_BLK_F_FLUSH));
    IOS_TEST_ASSERT(context.configured_queue_size == TEST_QUEUE_CAPACITY);
    IOS_TEST_ASSERT(virtio_blk_capacity_bytes(&device) == 1024 * 512);
    fake_block_destroy(&backend);
}

static void test_used_completion_controls_visibility_and_supports_out_of_order_results(void)
{
    struct ios_fault_injector faults;
    struct ios_fake_block backend;
    struct ios_virtio_blk_device device;
    struct ios_virtio_blk_request requests[TEST_QUEUE_CAPACITY];
    struct test_transport context = { 0 };
    ios_u8 first[IOS_FAKE_BLOCK_SECTOR_SIZE];
    ios_u8 second[IOS_FAKE_BLOCK_SECTOR_SIZE];
    ios_u16 first_id, second_id;
    ios_u32 generation;
    memset(first, 0x11, sizeof(first));
    memset(second, 0x22, sizeof(second));
    fault_injector_initialize(&faults);
    IOS_TEST_ASSERT_STATUS(fake_block_initialize(&backend, 1024, 16, &faults), IOS_OK);
    context.backend = &backend;
    context.offered_features = IOS_VIRTIO_F_VERSION_1 | IOS_VIRTIO_BLK_F_FLUSH;
    IOS_TEST_ASSERT_STATUS(initialize_device(&device, requests, &context), IOS_OK);
    IOS_TEST_ASSERT_STATUS(virtio_blk_submit(&device, IOS_VIRTIO_BLK_REQUEST_WRITE, 10,
        first, sizeof(first), 7, &first_id), IOS_OK);
    IOS_TEST_ASSERT_STATUS(virtio_blk_submit(&device, IOS_VIRTIO_BLK_REQUEST_WRITE, 11,
        second, sizeof(second), 8, &second_id), IOS_OK);
    IOS_TEST_ASSERT_STATUS(virtio_blk_poll(&device, first_id, NULL), IOS_ERROR(IOS_E_WOULD_BLOCK));
    IOS_TEST_ASSERT_STATUS(complete_backend_request(&device, &context, second_id), IOS_OK);
    IOS_TEST_ASSERT_STATUS(virtio_blk_poll(&device, second_id, &generation), IOS_OK);
    IOS_TEST_ASSERT(generation == 8 && device.outstanding == 1);
    IOS_TEST_ASSERT_STATUS(complete_backend_request(&device, &context, first_id), IOS_OK);
    IOS_TEST_ASSERT_STATUS(virtio_blk_poll(&device, first_id, &generation), IOS_OK);
    IOS_TEST_ASSERT(generation == 7 && device.outstanding == 0);
    fake_block_destroy(&backend);
}

static void test_queue_capacity_bounds_and_descriptor_reuse(void)
{
    struct ios_fault_injector faults;
    struct ios_fake_block backend;
    struct ios_virtio_blk_device device;
    struct ios_virtio_blk_request requests[TEST_QUEUE_CAPACITY];
    struct test_transport context = { 0 };
    ios_u8 buffer[IOS_FAKE_BLOCK_SECTOR_SIZE] = { 0 };
    ios_u16 ids[TEST_QUEUE_CAPACITY], extra;
    fault_injector_initialize(&faults);
    IOS_TEST_ASSERT_STATUS(fake_block_initialize(&backend, 1024, 16, &faults), IOS_OK);
    context.backend = &backend;
    context.offered_features = IOS_VIRTIO_F_VERSION_1 | IOS_VIRTIO_BLK_F_FLUSH;
    IOS_TEST_ASSERT_STATUS(initialize_device(&device, requests, &context), IOS_OK);
    for (ios_size index = 0; index < TEST_QUEUE_CAPACITY; ++index) {
        IOS_TEST_ASSERT_STATUS(virtio_blk_submit(&device, IOS_VIRTIO_BLK_REQUEST_READ, index,
            buffer, sizeof(buffer), 1, &ids[index]), IOS_OK);
    }
    IOS_TEST_ASSERT_STATUS(virtio_blk_submit(&device, IOS_VIRTIO_BLK_REQUEST_READ, 9,
        buffer, sizeof(buffer), 1, &extra), IOS_ERROR(IOS_E_WOULD_BLOCK));
    IOS_TEST_ASSERT_STATUS(virtio_blk_submit(&device, IOS_VIRTIO_BLK_REQUEST_READ,
        backend.sector_count, buffer, sizeof(buffer), 1, &extra), IOS_ERROR(IOS_E_OUT_OF_RANGE));
    IOS_TEST_ASSERT_STATUS(complete_backend_request(&device, &context, ids[0]), IOS_OK);
    IOS_TEST_ASSERT_STATUS(virtio_blk_poll(&device, ids[0], NULL), IOS_OK);
    IOS_TEST_ASSERT_STATUS(virtio_blk_submit(&device, IOS_VIRTIO_BLK_REQUEST_READ, 9,
        buffer, sizeof(buffer), 1, &extra), IOS_OK);
    IOS_TEST_ASSERT(extra == ids[0]);
    fake_block_destroy(&backend);
}

static void test_write_and_flush_errors_propagate_without_false_durability(void)
{
    struct ios_fault_injector faults;
    struct ios_fake_block backend;
    struct ios_virtio_blk_device device;
    struct ios_virtio_blk_request requests[TEST_QUEUE_CAPACITY];
    struct test_transport context = { 0 };
    ios_u8 buffer[IOS_FAKE_BLOCK_SECTOR_SIZE] = { 0x5a };
    ios_u16 request_id;
    fault_injector_initialize(&faults);
    IOS_TEST_ASSERT_STATUS(fake_block_initialize(&backend, 1024, 16, &faults), IOS_OK);
    context.backend = &backend;
    context.offered_features = IOS_VIRTIO_F_VERSION_1 | IOS_VIRTIO_BLK_F_FLUSH;
    IOS_TEST_ASSERT_STATUS(initialize_device(&device, requests, &context), IOS_OK);
    IOS_TEST_ASSERT_STATUS(fault_injector_fail_once(&faults, IOS_FAULT_BLOCK_WRITE, 1,
        IOS_ERROR(IOS_E_IO)), IOS_OK);
    IOS_TEST_ASSERT_STATUS(virtio_blk_submit(&device, IOS_VIRTIO_BLK_REQUEST_WRITE, 20,
        buffer, sizeof(buffer), 3, &request_id), IOS_OK);
    IOS_TEST_ASSERT_STATUS(complete_backend_request(&device, &context, request_id), IOS_ERROR(IOS_E_IO));
    IOS_TEST_ASSERT_STATUS(virtio_blk_poll(&device, request_id, NULL), IOS_ERROR(IOS_E_IO));
    IOS_TEST_ASSERT(backend.durable_generation == 0);
    IOS_TEST_ASSERT_STATUS(fault_injector_fail_once(&faults, IOS_FAULT_BLOCK_FLUSH, 1,
        IOS_ERROR(IOS_E_IO)), IOS_OK);
    IOS_TEST_ASSERT_STATUS(virtio_blk_submit(&device, IOS_VIRTIO_BLK_REQUEST_FLUSH, 0,
        NULL, 0, 4, &request_id), IOS_OK);
    IOS_TEST_ASSERT_STATUS(complete_backend_request(&device, &context, request_id), IOS_ERROR(IOS_E_IO));
    IOS_TEST_ASSERT_STATUS(virtio_blk_poll(&device, request_id, NULL), IOS_ERROR(IOS_E_IO));
    IOS_TEST_ASSERT(backend.durable_generation == 0);
    IOS_TEST_ASSERT_STATUS(virtio_blk_submit(&device, IOS_VIRTIO_BLK_REQUEST_FLUSH, 0,
        NULL, 0, 5, &request_id), IOS_OK);
    IOS_TEST_ASSERT_STATUS(complete_backend_request(&device, &context, request_id), IOS_OK);
    IOS_TEST_ASSERT_STATUS(virtio_blk_poll(&device, request_id, NULL), IOS_OK);
    IOS_TEST_ASSERT(backend.durable_generation == 1);
    fake_block_destroy(&backend);
}

enum {
    TEST_PCI_DEVICE = 5,
    TEST_MMIO_SIZE = 4096,
    TEST_COMMON_OFFSET = 0,
    TEST_NOTIFY_OFFSET = 0x100,
    TEST_ISR_OFFSET = 0x200,
    TEST_DEVICE_OFFSET = 0x300,
    TEST_DESCRIPTOR_LIMIT = 256
};

struct test_virtq_descriptor {
    ios_u64 address;
    ios_u32 length;
    ios_u16 flags;
    ios_u16 next;
};

struct test_virtq_available {
    ios_u16 flags;
    ios_u16 index;
    ios_u16 ring[TEST_DESCRIPTOR_LIMIT];
    ios_u16 used_event;
};

struct test_virtq_used_element {
    ios_u32 identifier;
    ios_u32 length;
};

struct test_virtq_used {
    ios_u16 flags;
    ios_u16 index;
    struct test_virtq_used_element ring[TEST_DESCRIPTOR_LIMIT];
    ios_u16 available_event;
};

struct test_virtio_header {
    ios_u32 type;
    ios_u32 reserved;
    ios_u64 sector;
};

struct test_pci_device {
    struct ios_fake_block *backend;
    ios_u32 configuration[64];
    ios_u64 offered_features;
    ios_u64 accepted_features;
    ios_u64 descriptor_address;
    ios_u64 available_address;
    ios_u64 used_address;
    ios_u32 device_feature_select;
    ios_u32 driver_feature_select;
    ios_u16 queue_size;
    ios_u16 queue_enable;
    ios_u16 last_available;
    ios_u8 device_status;
    ios_u8 isr_status;
    bool probe_bar_low;
    bool probe_bar_high;
    bool complete_notifications;
};

static ios_u8 test_mmio[TEST_MMIO_SIZE] IOS_ALIGNED(TEST_MMIO_SIZE);

static ios_u32 capability_header(
    ios_u8 next,
    ios_u8 length,
    ios_u8 configuration_type
)
{
    return UINT32_C(0x09) | ((ios_u32)next << 8) | ((ios_u32)length << 16)
        | ((ios_u32)configuration_type << 24);
}

static void initialize_test_pci_device(
    struct test_pci_device *device,
    struct ios_fake_block *backend
)
{
    const ios_u64 bar = (ios_uptr)test_mmio;

    memset(device, 0, sizeof(*device));
    memset(test_mmio, 0, sizeof(test_mmio));
    device->backend = backend;
    device->offered_features = IOS_VIRTIO_F_VERSION_1 | IOS_VIRTIO_BLK_F_FLUSH;
    device->queue_size = TEST_DESCRIPTOR_LIMIT;
    device->complete_notifications = true;
    device->configuration[0x00 / 4] = IOS_VIRTIO_PCI_VENDOR_ID
        | ((ios_u32)IOS_VIRTIO_BLK_MODERN_DEVICE_ID << 16);
    device->configuration[0x04 / 4] = UINT32_C(1) << 20;
    device->configuration[0x0c / 4] = 0;
    device->configuration[0x10 / 4] = (ios_u32)bar | UINT32_C(4);
    device->configuration[0x14 / 4] = (ios_u32)(bar >> 32);
    device->configuration[0x34 / 4] = 0x40;

    device->configuration[0x40 / 4] = capability_header(0x50, 16, 1);
    device->configuration[0x44 / 4] = 0;
    device->configuration[0x48 / 4] = TEST_COMMON_OFFSET;
    device->configuration[0x4c / 4] = 56;
    device->configuration[0x50 / 4] = capability_header(0x64, 20, 2);
    device->configuration[0x54 / 4] = 0;
    device->configuration[0x58 / 4] = TEST_NOTIFY_OFFSET;
    device->configuration[0x5c / 4] = 2;
    device->configuration[0x60 / 4] = 4;
    device->configuration[0x64 / 4] = capability_header(0x74, 16, 3);
    device->configuration[0x68 / 4] = 0;
    device->configuration[0x6c / 4] = TEST_ISR_OFFSET;
    device->configuration[0x70 / 4] = 1;
    device->configuration[0x74 / 4] = capability_header(0, 16, 4);
    device->configuration[0x78 / 4] = 0;
    device->configuration[0x7c / 4] = TEST_DEVICE_OFFSET;
    device->configuration[0x80 / 4] = 8;
}

static ios_status test_pci_read32(
    void *opaque,
    ios_u8 bus,
    ios_u8 device_number,
    ios_u8 function,
    ios_u16 offset,
    ios_u32 *value
)
{
    struct test_pci_device *device = opaque;

    if (value == NULL || (offset & 3U) != 0 || offset > 0xfc) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (bus != 0 || device_number != TEST_PCI_DEVICE || function != 0) {
        *value = UINT32_MAX;
        return IOS_OK;
    }
    if (offset == 0x10 && device->probe_bar_low) {
        *value = UINT32_C(0xfffff004);
    } else if (offset == 0x14 && device->probe_bar_high) {
        *value = UINT32_MAX;
    } else {
        *value = device->configuration[offset / 4U];
    }
    return IOS_OK;
}

static ios_status test_pci_write32(
    void *opaque,
    ios_u8 bus,
    ios_u8 device_number,
    ios_u8 function,
    ios_u16 offset,
    ios_u32 value
)
{
    struct test_pci_device *device = opaque;

    if (bus != 0 || device_number != TEST_PCI_DEVICE || function != 0
        || (offset & 3U) != 0 || offset > 0xfc) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (offset == 0x10) {
        device->probe_bar_low = value == UINT32_MAX;
        if (!device->probe_bar_low) {
            device->configuration[offset / 4U] = value;
        }
    } else if (offset == 0x14) {
        device->probe_bar_high = value == UINT32_MAX;
        if (!device->probe_bar_high) {
            device->configuration[offset / 4U] = value;
        }
    } else {
        device->configuration[offset / 4U] = value;
    }
    return IOS_OK;
}

static ios_uptr mmio_offset(ios_uptr address)
{
    IOS_TEST_ASSERT(address >= (ios_uptr)test_mmio);
    IOS_TEST_ASSERT(address < (ios_uptr)test_mmio + sizeof(test_mmio));
    return address - (ios_uptr)test_mmio;
}

static ios_u8 test_mmio_read8(void *opaque, ios_uptr address)
{
    struct test_pci_device *device = opaque;
    const ios_uptr offset = mmio_offset(address);

    if (offset == TEST_COMMON_OFFSET + 20U) {
        return device->device_status;
    }
    if (offset == TEST_COMMON_OFFSET + 21U) {
        return 0;
    }
    if (offset == TEST_ISR_OFFSET) {
        const ios_u8 value = device->isr_status;
        device->isr_status = 0;
        return value;
    }
    return test_mmio[offset];
}

static ios_u16 test_mmio_read16(void *opaque, ios_uptr address)
{
    struct test_pci_device *device = opaque;
    const ios_uptr offset = mmio_offset(address);

    if (offset == TEST_COMMON_OFFSET + 24U) {
        return device->queue_size;
    }
    if (offset == TEST_COMMON_OFFSET + 28U) {
        return device->queue_enable;
    }
    if (offset == TEST_COMMON_OFFSET + 30U) {
        return 0;
    }
    return (ios_u16)(test_mmio[offset] | ((ios_u16)test_mmio[offset + 1U] << 8));
}

static ios_u32 test_mmio_read32(void *opaque, ios_uptr address)
{
    struct test_pci_device *device = opaque;
    const ios_uptr offset = mmio_offset(address);

    if (offset == TEST_COMMON_OFFSET + 4U) {
        return device->device_feature_select == 0
            ? (ios_u32)device->offered_features : (ios_u32)(device->offered_features >> 32);
    }
    if (offset == TEST_DEVICE_OFFSET) {
        return (ios_u32)device->backend->sector_count;
    }
    if (offset == TEST_DEVICE_OFFSET + 4U) {
        return (ios_u32)(device->backend->sector_count >> 32);
    }
    return (ios_u32)test_mmio[offset] | ((ios_u32)test_mmio[offset + 1U] << 8)
        | ((ios_u32)test_mmio[offset + 2U] << 16)
        | ((ios_u32)test_mmio[offset + 3U] << 24);
}

static void test_mmio_write8(void *opaque, ios_uptr address, ios_u8 value)
{
    struct test_pci_device *device = opaque;
    const ios_uptr offset = mmio_offset(address);

    if (offset == TEST_COMMON_OFFSET + 20U) {
        device->device_status = value;
    } else {
        test_mmio[offset] = value;
    }
}

static void complete_pci_request(struct test_pci_device *device)
{
    volatile struct test_virtq_available *available =
        (volatile struct test_virtq_available *)(ios_uptr)device->available_address;
    volatile struct test_virtq_used *used =
        (volatile struct test_virtq_used *)(ios_uptr)device->used_address;
    struct test_virtq_descriptor *descriptors =
        (struct test_virtq_descriptor *)(ios_uptr)device->descriptor_address;
    const ios_u16 head = available->ring[device->last_available % device->queue_size];
    const struct test_virtio_header *header =
        (const struct test_virtio_header *)(ios_uptr)descriptors[head].address;
    ios_u16 status_descriptor = descriptors[head].next;
    ios_status status;

    if (header->type == IOS_VIRTIO_BLK_REQUEST_READ
        || header->type == IOS_VIRTIO_BLK_REQUEST_WRITE) {
        const ios_u16 data_descriptor = status_descriptor;
        void *data = (void *)(ios_uptr)descriptors[data_descriptor].address;

        status_descriptor = descriptors[data_descriptor].next;
        status = header->type == IOS_VIRTIO_BLK_REQUEST_READ
            ? fake_block_read(device->backend, header->sector, 1, data)
            : fake_block_write(device->backend, header->sector, 1, data);
    } else if (header->type == IOS_VIRTIO_BLK_REQUEST_FLUSH) {
        status = fake_block_flush(device->backend);
    } else {
        status = IOS_ERROR(IOS_E_NOT_SUPPORTED);
    }
    *(ios_u8 *)(ios_uptr)descriptors[status_descriptor].address = IOS_SUCCEEDED(status)
        ? IOS_VIRTIO_BLK_STATUS_OK : IOS_VIRTIO_BLK_STATUS_IO_ERROR;
    used->ring[used->index % device->queue_size] = (struct test_virtq_used_element){
        .identifier = head,
        .length = 0
    };
    used->index = (ios_u16)(used->index + 1U);
    device->last_available = (ios_u16)(device->last_available + 1U);
    device->isr_status = 1;
}

static void test_mmio_write16(void *opaque, ios_uptr address, ios_u16 value)
{
    struct test_pci_device *device = opaque;
    const ios_uptr offset = mmio_offset(address);

    if (offset == TEST_COMMON_OFFSET + 24U) {
        device->queue_size = value;
    } else if (offset == TEST_COMMON_OFFSET + 28U) {
        device->queue_enable = value;
    } else if (offset == TEST_NOTIFY_OFFSET) {
        IOS_TEST_ASSERT(value == 0);
        if (device->complete_notifications) {
            complete_pci_request(device);
        }
    } else {
        test_mmio[offset] = (ios_u8)value;
        test_mmio[offset + 1U] = (ios_u8)(value >> 8);
    }
}

static void test_mmio_write32(void *opaque, ios_uptr address, ios_u32 value)
{
    struct test_pci_device *device = opaque;
    const ios_uptr offset = mmio_offset(address);

    if (offset == TEST_COMMON_OFFSET) {
        device->device_feature_select = value;
    } else if (offset == TEST_COMMON_OFFSET + 8U) {
        device->driver_feature_select = value;
    } else if (offset == TEST_COMMON_OFFSET + 12U) {
        const ios_u32 shift = device->driver_feature_select == 0 ? 0 : 32;
        const ios_u64 mask = UINT64_C(0xffffffff) << shift;
        device->accepted_features = (device->accepted_features & ~mask) | ((ios_u64)value << shift);
    } else if (offset >= TEST_COMMON_OFFSET + 32U
               && offset < TEST_COMMON_OFFSET + 56U) {
        ios_u64 *target;
        if (offset < TEST_COMMON_OFFSET + 40U) {
            target = &device->descriptor_address;
        } else if (offset < TEST_COMMON_OFFSET + 48U) {
            target = &device->available_address;
        } else {
            target = &device->used_address;
        }
        if ((offset & 7U) == 0) {
            *target = (*target & UINT64_C(0xffffffff00000000)) | value;
        } else {
            *target = (*target & UINT64_C(0xffffffff)) | ((ios_u64)value << 32);
        }
    } else {
        test_mmio[offset] = (ios_u8)value;
        test_mmio[offset + 1U] = (ios_u8)(value >> 8);
        test_mmio[offset + 2U] = (ios_u8)(value >> 16);
        test_mmio[offset + 3U] = (ios_u8)(value >> 24);
    }
}

static void test_barrier(void *opaque)
{
    (void)opaque;
}

static void test_relax(void *opaque)
{
    (void)opaque;
}

static ios_status test_dma_address(
    void *opaque,
    const void *buffer,
    ios_size byte_count,
    ios_uptr *address
)
{
    (void)opaque;
    if (buffer == NULL || byte_count == 0 || address == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *address = (ios_uptr)buffer;
    return IOS_OK;
}

static struct ios_virtio_blk_pci_platform test_platform(struct test_pci_device *device)
{
    return (struct ios_virtio_blk_pci_platform){
        .pci = {
            .context = device,
            .read32 = test_pci_read32,
            .write32 = test_pci_write32
        },
        .mmio_read8 = test_mmio_read8,
        .mmio_read16 = test_mmio_read16,
        .mmio_read32 = test_mmio_read32,
        .mmio_write8 = test_mmio_write8,
        .mmio_write16 = test_mmio_write16,
        .mmio_write32 = test_mmio_write32,
        .memory_barrier = test_barrier,
        .relax = test_relax,
        .dma_address = test_dma_address,
        .context = device
    };
}

static void test_pci_transport_publishes_64_gib_block_device_and_moves_data(void)
{
    struct ios_fault_injector faults;
    struct ios_fake_block backend;
    struct test_pci_device pci;
    struct ios_virtio_blk_pci_platform platform;
    struct ios_block_device block;
    ios_u8 write_buffer[IOS_BLOCK_SECTOR_SIZE];
    ios_u8 read_buffer[IOS_BLOCK_SECTOR_SIZE];
    const ios_u64 sectors_64_gib = UINT64_C(64) * 1024 * 1024 * 1024 / 512;

    memset(write_buffer, 0x6d, sizeof(write_buffer));
    memset(read_buffer, 0, sizeof(read_buffer));
    fault_injector_initialize(&faults);
    IOS_TEST_ASSERT_STATUS(fake_block_initialize(&backend, sectors_64_gib, 16, &faults), IOS_OK);
    initialize_test_pci_device(&pci, &backend);
    platform = test_platform(&pci);

    IOS_TEST_ASSERT_STATUS(virtio_blk_pci_initialize_with_platform(&block, &platform), IOS_OK);
    IOS_TEST_ASSERT(block.status == IOS_BLOCK_DEVICE_READY);
    IOS_TEST_ASSERT(block.logical_sector_size == IOS_BLOCK_SECTOR_SIZE);
    IOS_TEST_ASSERT(block_device_capacity_bytes(&block) == UINT64_C(64) * 1024 * 1024 * 1024);
    IOS_TEST_ASSERT(
        pci.accepted_features == (IOS_VIRTIO_F_VERSION_1 | IOS_VIRTIO_BLK_F_FLUSH)
    );
    IOS_TEST_ASSERT((pci.configuration[0x04 / 4] & UINT32_C(6)) == UINT32_C(6));

    IOS_TEST_ASSERT_STATUS(block_device_write(&block, 1234, 1, write_buffer), IOS_OK);
    IOS_TEST_ASSERT_STATUS(block_device_read(&block, 1234, 1, read_buffer), IOS_OK);
    IOS_TEST_ASSERT(memcmp(write_buffer, read_buffer, sizeof(write_buffer)) == 0);
    IOS_TEST_ASSERT_STATUS(block_device_flush(&block), IOS_OK);
    IOS_TEST_ASSERT(backend.durable_generation == 1);
    fake_block_destroy(&backend);
}

static void test_pci_transport_propagates_device_error_and_times_out(void)
{
    struct ios_fault_injector faults;
    struct ios_fake_block backend;
    struct test_pci_device pci;
    struct ios_virtio_blk_pci_platform platform;
    struct ios_block_device block;
    ios_u8 buffer[IOS_BLOCK_SECTOR_SIZE] = { 0 };

    fault_injector_initialize(&faults);
    IOS_TEST_ASSERT_STATUS(fake_block_initialize(&backend, 4096, 8, &faults), IOS_OK);
    initialize_test_pci_device(&pci, &backend);
    platform = test_platform(&pci);
    IOS_TEST_ASSERT_STATUS(virtio_blk_pci_initialize_with_platform(&block, &platform), IOS_OK);
    IOS_TEST_ASSERT_STATUS(fault_injector_fail_once(
        &faults, IOS_FAULT_BLOCK_WRITE, 1, IOS_ERROR(IOS_E_IO)
    ), IOS_OK);
    IOS_TEST_ASSERT_STATUS(block_device_write(&block, 4, 1, buffer), IOS_ERROR(IOS_E_IO));

    pci.complete_notifications = false;
    IOS_TEST_ASSERT_STATUS(block_device_read(&block, 4, 1, buffer), IOS_ERROR(IOS_E_TIMEOUT));
    IOS_TEST_ASSERT(block.status == IOS_BLOCK_DEVICE_FAILED);
    fake_block_destroy(&backend);
}

static void test_pci_transport_rejects_out_of_bar_capability(void)
{
    struct ios_fault_injector faults;
    struct ios_fake_block backend;
    struct test_pci_device pci;
    struct ios_virtio_blk_pci_platform platform;
    struct ios_block_device block;

    fault_injector_initialize(&faults);
    IOS_TEST_ASSERT_STATUS(fake_block_initialize(&backend, 4096, 8, &faults), IOS_OK);
    initialize_test_pci_device(&pci, &backend);
    pci.configuration[0x5c / 4] = TEST_MMIO_SIZE;
    platform = test_platform(&pci);
    IOS_TEST_ASSERT_STATUS(
        virtio_blk_pci_initialize_with_platform(&block, &platform),
        IOS_ERROR(IOS_E_PROTOCOL)
    );
    fake_block_destroy(&backend);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_discovery_and_negotiation_require_modern_transport_and_flush),
    IOS_TEST_CASE(test_used_completion_controls_visibility_and_supports_out_of_order_results),
    IOS_TEST_CASE(test_queue_capacity_bounds_and_descriptor_reuse),
    IOS_TEST_CASE(test_write_and_flush_errors_propagate_without_false_durability),
    IOS_TEST_CASE(test_pci_transport_publishes_64_gib_block_device_and_moves_data),
    IOS_TEST_CASE(test_pci_transport_propagates_device_error_and_times_out),
    IOS_TEST_CASE(test_pci_transport_rejects_out_of_bar_capability)
};
const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
