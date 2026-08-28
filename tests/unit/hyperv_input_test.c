#include <inferenceos/drivers/hyperv/input.h>
#include <inferenceos/test.h>

#include <string.h>

extern ios_u8 hyperv_input_test_response[32];
extern ios_size hyperv_input_test_response_size;
extern ios_size hyperv_input_test_request_size;
extern bool hyperv_input_test_response_ready;

static void keyboard_protocol_uses_exact_wire_message_sizes(void)
{
    struct ios_input_queue queue;
    struct ios_hyperv_keyboard keyboard;
    struct ios_vmbus bus = { 0 };
    struct ios_vmbus_channel channel = { 0 };
    const ios_u32 response[2] = {2, 1};

    input_queue_initialize(&queue, 100, 100);
    memcpy(hyperv_input_test_response, response, sizeof(response));
    hyperv_input_test_response_size = sizeof(response);
    hyperv_input_test_request_size = 0;
    hyperv_input_test_response_ready = true;

    IOS_TEST_ASSERT_STATUS(hyperv_keyboard_initialize(
        &keyboard, &bus, &channel, &queue, 10), IOS_OK);
    IOS_TEST_ASSERT(hyperv_input_test_request_size == 8);
    IOS_TEST_ASSERT(keyboard.ready);
}

static void mouse_protocol_uses_pipe_envelope(void)
{
    struct ios_input_queue queue;
    struct ios_hyperv_mouse mouse;
    struct ios_vmbus bus = { 0 };
    struct ios_vmbus_channel channel = { 0 };
    const ios_u8 response[21] = {
        1, 0, 0, 0, 13, 0, 0, 0,
        1, 0, 0, 0, 5, 0, 0, 0,
        0, 0, 2, 0, 1
    };

    input_queue_initialize(&queue, 100, 100);
    memcpy(hyperv_input_test_response, response, sizeof(response));
    hyperv_input_test_response_size = sizeof(response);
    hyperv_input_test_request_size = 0;
    hyperv_input_test_response_ready = true;

    IOS_TEST_ASSERT_STATUS(hyperv_mouse_initialize(
        &mouse, &bus, &channel, &queue, 10), IOS_OK);
    IOS_TEST_ASSERT(hyperv_input_test_request_size == 20);
    IOS_TEST_ASSERT(mouse.ready);
}

static void keyboard_make_break_repeat_and_modifiers_are_normalized(void)
{
    struct ios_input_queue queue;
    struct ios_hyperv_keyboard keyboard;
    struct ios_input_event event;

    input_queue_initialize(&queue, 100, 100);
    memset(&keyboard, 0, sizeof(keyboard));
    keyboard.queue = &queue;
    IOS_TEST_ASSERT_STATUS(hyperv_keyboard_handle_event(&keyboard, 0x1e, 0, 1), IOS_OK);
    IOS_TEST_ASSERT_STATUS(input_queue_pop(&queue, &event), IOS_OK);
    IOS_TEST_ASSERT(event.code == 'a' && event.text == 'a');
    IOS_TEST_ASSERT((event.flags & IOS_INPUT_PRESSED) != 0);

    IOS_TEST_ASSERT_STATUS(hyperv_keyboard_handle_event(&keyboard, 0x1e, 0, 2), IOS_OK);
    IOS_TEST_ASSERT_STATUS(input_queue_pop(&queue, &event), IOS_OK);
    IOS_TEST_ASSERT((event.flags & IOS_INPUT_REPEAT) != 0);

    IOS_TEST_ASSERT_STATUS(hyperv_keyboard_handle_event(&keyboard, 0x1e, 2, 3), IOS_OK);
    IOS_TEST_ASSERT_STATUS(input_queue_pop(&queue, &event), IOS_OK);
    IOS_TEST_ASSERT(event.text == 0 && event.flags == 0);

    IOS_TEST_ASSERT_STATUS(hyperv_keyboard_handle_event(&keyboard, 0x2a, 0, 4), IOS_OK);
    IOS_TEST_ASSERT_STATUS(hyperv_keyboard_handle_event(&keyboard, 0x02, 0, 5), IOS_OK);
    IOS_TEST_ASSERT_STATUS(input_queue_pop(&queue, &event), IOS_OK);
    IOS_TEST_ASSERT(event.code == IOS_KEY_LEFT_SHIFT);
    IOS_TEST_ASSERT_STATUS(input_queue_pop(&queue, &event), IOS_OK);
    IOS_TEST_ASSERT(event.code == '1' && event.text == '!');
}

static void keyboard_poll_accepts_vmbus_alignment_padding(void)
{
    struct ios_input_queue queue;
    struct ios_hyperv_keyboard keyboard;
    struct ios_vmbus_channel channel = { 0 };
    struct ios_input_event event;
    const ios_u32 padded_message[4] = {
        3, 0x001e, 0, 0
    };

    input_queue_initialize(&queue, 100, 100);
    memset(&keyboard, 0, sizeof(keyboard));
    keyboard.channel = &channel;
    keyboard.queue = &queue;
    keyboard.ready = true;
    memcpy(hyperv_input_test_response, padded_message, sizeof(padded_message));
    hyperv_input_test_response_size = sizeof(padded_message);
    hyperv_input_test_response_ready = true;

    IOS_TEST_ASSERT_STATUS(hyperv_keyboard_poll(&keyboard, 1), IOS_OK);
    IOS_TEST_ASSERT_STATUS(input_queue_pop(&queue, &event), IOS_OK);
    IOS_TEST_ASSERT(event.code == 'a' && event.text == 'a');
}

static void extended_arrows_and_absolute_mouse_reports_are_normalized(void)
{
    struct ios_input_queue queue;
    struct ios_hyperv_keyboard keyboard;
    struct ios_hyperv_mouse mouse;
    struct ios_input_event event;
    const ios_u8 report[5] = {1, 0xff, 0xff, 0xff, 0xff};

    input_queue_initialize(&queue, 100, 80);
    memset(&keyboard, 0, sizeof(keyboard));
    keyboard.queue = &queue;
    IOS_TEST_ASSERT_STATUS(hyperv_keyboard_handle_event(&keyboard, 0x4b, 4, 1), IOS_OK);
    IOS_TEST_ASSERT_STATUS(input_queue_pop(&queue, &event), IOS_OK);
    IOS_TEST_ASSERT(event.code == IOS_KEY_LEFT);

    memset(&mouse, 0, sizeof(mouse));
    mouse.queue = &queue;
    IOS_TEST_ASSERT_STATUS(hyperv_mouse_handle_report(
        &mouse, report, sizeof(report), 2), IOS_OK);
    IOS_TEST_ASSERT_STATUS(input_queue_pop(&queue, &event), IOS_OK);
    IOS_TEST_ASSERT(event.type == IOS_INPUT_EVENT_POINTER_MOVE);
    IOS_TEST_ASSERT(event.x == 99 && event.y == 79);
    IOS_TEST_ASSERT_STATUS(input_queue_pop(&queue, &event), IOS_OK);
    IOS_TEST_ASSERT(event.type == IOS_INPUT_EVENT_POINTER_BUTTON);
    IOS_TEST_ASSERT(event.code == IOS_POINTER_BUTTON_LEFT);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(keyboard_protocol_uses_exact_wire_message_sizes),
    IOS_TEST_CASE(mouse_protocol_uses_pipe_envelope),
    IOS_TEST_CASE(keyboard_make_break_repeat_and_modifiers_are_normalized),
    IOS_TEST_CASE(keyboard_poll_accepts_vmbus_alignment_padding),
    IOS_TEST_CASE(extended_arrows_and_absolute_mouse_reports_are_normalized)
};

const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
