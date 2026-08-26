#include <inferenceos/test.h>

#include <inferenceos/test_control.h>

#include <string.h>

struct fixture {
    const char *input;
    ios_size cursor;
    ios_size dispatch_count;
    struct ios_test_control_request request;
};

static bool read_character(void *context, char *character)
{
    struct fixture *fixture = context;
    if (fixture->input[fixture->cursor] == '\0') return false;
    *character = fixture->input[fixture->cursor++];
    return true;
}

static ios_status dispatch_request(
    void *context, const struct ios_test_control_request *request
)
{
    struct fixture *fixture = context;
    fixture->request = *request;
    ++fixture->dispatch_count;
    return IOS_OK;
}

static void test_fragmented_versioned_request_dispatches_once(void)
{
    struct fixture fixture = { "INFERENCEOS_TEST 1 42 start_gui keyboard,pointer\n", 0, 0, { 0 } };
    struct ios_test_control control;
    IOS_TEST_ASSERT_STATUS(
        ios_test_control_initialize(
            &control, read_character, &fixture, dispatch_request, &fixture
        ),
        IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(ios_test_control_poll(&control, 7), IOS_OK);
    IOS_TEST_ASSERT(fixture.dispatch_count == 0);
    IOS_TEST_ASSERT_STATUS(ios_test_control_poll(&control, 128), IOS_OK);
    IOS_TEST_ASSERT(fixture.dispatch_count == 1);
    IOS_TEST_ASSERT(fixture.request.sequence == 42);
    IOS_TEST_ASSERT(strcmp(fixture.request.action, "start_gui") == 0);
    IOS_TEST_ASSERT(strcmp(fixture.request.argument, "keyboard,pointer") == 0);
}

static void test_rejects_bad_version_and_recovers_at_next_line(void)
{
    struct fixture fixture = {
        "INFERENCEOS_TEST 2 1 rejected value\n"
        "INFERENCEOS_TEST 1 2 format_mount_remount\n",
        0, 0, { 0 }
    };
    struct ios_test_control control;
    IOS_TEST_ASSERT_STATUS(
        ios_test_control_initialize(
            &control, read_character, &fixture, dispatch_request, &fixture
        ),
        IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(
        ios_test_control_poll(&control, 256), IOS_ERROR(IOS_E_PROTOCOL)
    );
    IOS_TEST_ASSERT(fixture.dispatch_count == 1);
    IOS_TEST_ASSERT(fixture.request.sequence == 2);
    IOS_TEST_ASSERT(strcmp(fixture.request.action, "format_mount_remount") == 0);
    IOS_TEST_ASSERT(fixture.request.argument[0] == '\0');
}

static void test_overlong_line_is_discarded_without_dispatch(void)
{
    char input[IOS_TEST_CONTROL_LINE_CAPACITY + 40];
    struct fixture fixture = { input, 0, 0, { 0 } };
    struct ios_test_control control;
    memset(input, 'A', sizeof(input));
    input[sizeof(input) - 2] = '\n';
    input[sizeof(input) - 1] = '\0';
    IOS_TEST_ASSERT_STATUS(
        ios_test_control_initialize(
            &control, read_character, &fixture, dispatch_request, &fixture
        ),
        IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(
        ios_test_control_poll(&control, sizeof(input)), IOS_ERROR(IOS_E_NO_SPACE)
    );
    IOS_TEST_ASSERT(fixture.dispatch_count == 0);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_fragmented_versioned_request_dispatches_once),
    IOS_TEST_CASE(test_rejects_bad_version_and_recovers_at_next_line),
    IOS_TEST_CASE(test_overlong_line_is_discarded_without_dispatch)
};

const size_t ios_test_case_count = sizeof(ios_test_cases) / sizeof(ios_test_cases[0]);
