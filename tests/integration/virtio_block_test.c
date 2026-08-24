#include <inferenceos/test.h>
#include <inferenceos/drivers/virtio_blk.h>
#include <inferenceos/fake_block.h>
#include <string.h>

enum { TEST_QUEUE_CAPACITY = 8 };

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
        context, transport_reset, transport_read_features, transport_write_features,
        transport_setup_queue, transport_set_driver_ok, transport_read_capacity, transport_publish
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

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_discovery_and_negotiation_require_modern_transport_and_flush),
    IOS_TEST_CASE(test_used_completion_controls_visibility_and_supports_out_of_order_results),
    IOS_TEST_CASE(test_queue_capacity_bounds_and_descriptor_reuse),
    IOS_TEST_CASE(test_write_and_flush_errors_propagate_without_false_durability)
};
const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
