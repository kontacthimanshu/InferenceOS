#include <inferenceos/test.h>

#include <inferenceos/power.h>

enum { POWER_TRACE_CAPACITY = 8 };

struct power_trace {
    ios_u32 events[POWER_TRACE_CAPACITY];
    ios_size event_count;
    ios_status sync_result;
    ios_status transition_result;
    enum ios_power_action action;
};

struct power_device_context {
    struct power_trace *trace;
    ios_u32 event;
    ios_status flush_result;
};

static void record(struct power_trace *trace, ios_u32 event)
{
    IOS_TEST_ASSERT(trace->event_count < POWER_TRACE_CAPACITY);
    trace->events[trace->event_count++] = event;
}

static ios_status unused_read(
    void *context, ios_u64 sector, ios_size count, void *buffer)
{
    (void)context; (void)sector; (void)count; (void)buffer;
    return IOS_OK;
}

static ios_status unused_write(
    void *context, ios_u64 sector, ios_size count, const void *buffer)
{
    (void)context; (void)sector; (void)count; (void)buffer;
    return IOS_OK;
}

static ios_status device_flush(void *context)
{
    struct power_device_context *device = context;
    record(device->trace, device->event);
    return device->flush_result;
}

static ios_status filesystem_sync(void *context)
{
    struct power_trace *trace = context;
    record(trace, 1);
    return trace->sync_result;
}

static ios_status platform_transition(void *context, enum ios_power_action action)
{
    struct power_trace *trace = context;
    record(trace, 4);
    trace->action = action;
    return trace->transition_result;
}

static void initialize_device(
    struct ios_block_device *device, struct power_device_context *context)
{
    const struct ios_block_device_operations operations = {
        unused_read, unused_write, device_flush
    };
    IOS_TEST_ASSERT_STATUS(
        block_device_initialize(
            device, context, &operations, IOS_BLOCK_SECTOR_SIZE, 128,
            IOS_BLOCK_DEVICE_READY),
        IOS_OK);
}

static void initialize_controller(
    struct ios_power_controller *controller,
    struct power_trace *trace,
    struct ios_block_device *first,
    struct ios_block_device *second)
{
    IOS_TEST_ASSERT_STATUS(
        ios_power_initialize(controller, trace, platform_transition), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_power_set_filesystem_sync(controller, trace, filesystem_sync), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_power_add_device(controller, first), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_power_add_device(controller, second), IOS_OK);
}

static void test_filesystem_and_devices_flush_before_transition(void)
{
    struct power_trace trace = { 0 };
    struct power_device_context first_context = { &trace, 2, IOS_OK };
    struct power_device_context second_context = { &trace, 3, IOS_OK };
    struct ios_block_device first;
    struct ios_block_device second;
    struct ios_power_controller controller;

    initialize_device(&first, &first_context);
    initialize_device(&second, &second_context);
    initialize_controller(&controller, &trace, &first, &second);
    IOS_TEST_ASSERT_STATUS(ios_power_request(&controller, IOS_POWER_REBOOT), IOS_OK);
    IOS_TEST_ASSERT(trace.event_count == 4);
    for (ios_size index = 0; index < trace.event_count; ++index) {
        IOS_TEST_ASSERT(trace.events[index] == index + 1);
    }
    IOS_TEST_ASSERT(trace.action == IOS_POWER_REBOOT);
    IOS_TEST_ASSERT(controller.state == IOS_POWER_TRANSITIONING);
    IOS_TEST_ASSERT_STATUS(
        ios_power_request(&controller, IOS_POWER_SHUTDOWN), IOS_ERROR(IOS_E_BUSY));
}

static void test_sync_failure_prevents_device_flush_and_transition(void)
{
    struct power_trace trace = { .sync_result = IOS_ERROR(IOS_E_IO) };
    struct power_device_context device_context = { &trace, 2, IOS_OK };
    struct ios_block_device device;
    struct ios_power_controller controller;

    initialize_device(&device, &device_context);
    IOS_TEST_ASSERT_STATUS(
        ios_power_initialize(&controller, &trace, platform_transition), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_power_set_filesystem_sync(&controller, &trace, filesystem_sync), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_power_add_device(&controller, &device), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_power_request(&controller, IOS_POWER_SHUTDOWN), IOS_ERROR(IOS_E_IO));
    IOS_TEST_ASSERT(trace.event_count == 1 && trace.events[0] == 1);
    IOS_TEST_ASSERT(controller.state == IOS_POWER_READY);
}

static void test_device_failure_prevents_later_flush_and_transition(void)
{
    struct power_trace trace = { 0 };
    struct power_device_context first_context = {
        &trace, 2, IOS_ERROR(IOS_E_IO)
    };
    struct power_device_context second_context = { &trace, 3, IOS_OK };
    struct ios_block_device first;
    struct ios_block_device second;
    struct ios_power_controller controller;

    initialize_device(&first, &first_context);
    initialize_device(&second, &second_context);
    initialize_controller(&controller, &trace, &first, &second);
    IOS_TEST_ASSERT_STATUS(
        ios_power_request(&controller, IOS_POWER_REBOOT), IOS_ERROR(IOS_E_IO));
    IOS_TEST_ASSERT(trace.event_count == 2);
    IOS_TEST_ASSERT(trace.events[0] == 1 && trace.events[1] == 2);
    IOS_TEST_ASSERT(controller.state == IOS_POWER_READY);
}

static void test_transition_failure_is_reported_and_allows_retry(void)
{
    struct power_trace trace = { .transition_result = IOS_ERROR(IOS_E_NOT_SUPPORTED) };
    struct ios_power_controller controller;

    IOS_TEST_ASSERT_STATUS(
        ios_power_initialize(&controller, &trace, platform_transition), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_power_request(&controller, IOS_POWER_SHUTDOWN),
        IOS_ERROR(IOS_E_NOT_SUPPORTED));
    IOS_TEST_ASSERT(trace.event_count == 1 && trace.events[0] == 4);
    IOS_TEST_ASSERT(trace.action == IOS_POWER_SHUTDOWN);
    IOS_TEST_ASSERT(controller.state == IOS_POWER_READY);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_filesystem_and_devices_flush_before_transition),
    IOS_TEST_CASE(test_sync_failure_prevents_device_flush_and_transition),
    IOS_TEST_CASE(test_device_failure_prevents_later_flush_and_transition),
    IOS_TEST_CASE(test_transition_failure_is_reported_and_allows_retry)
};

const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
