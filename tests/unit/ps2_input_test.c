#include <inferenceos/test.h>

#include <inferenceos/drivers/ps2.h>

/* Decoder tests never enter hardware initialization or interrupt wrappers. */
void x86_64_port_write8(ios_u16 port, ios_u8 value)
{
    (void)port;
    (void)value;
}

ios_u8 x86_64_port_read8(ios_u16 port)
{
    (void)port;
    return 0;
}

ios_u64 x86_64_interrupt_save_disable(void)
{
    return 0;
}

void x86_64_interrupt_restore(ios_u64 flags)
{
    (void)flags;
}

static void test_set2_keyboard_make_break_modifiers_and_repeat(void)
{
    struct ios_input_queue queue;
    struct ps2_keyboard keyboard;
    struct ios_input_event event;
    input_queue_initialize(&queue, 100, 100);
    ps2_keyboard_initialize(&keyboard, &queue);

    IOS_TEST_ASSERT_STATUS(ps2_keyboard_handle_byte(&keyboard, 0x16, 10), IOS_OK);
    IOS_TEST_ASSERT_STATUS(input_queue_pop(&queue, &event), IOS_OK);
    IOS_TEST_ASSERT(event.type == IOS_INPUT_EVENT_KEY && event.code == '1');
    IOS_TEST_ASSERT(event.text == '1' && (event.flags & IOS_INPUT_PRESSED) != 0);

    IOS_TEST_ASSERT_STATUS(ps2_keyboard_handle_byte(&keyboard, 0xf0, 11), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ps2_keyboard_handle_byte(&keyboard, 0x16, 11), IOS_OK);
    IOS_TEST_ASSERT_STATUS(input_queue_pop(&queue, &event), IOS_OK);
    IOS_TEST_ASSERT(event.code == '1' && event.text == 0 && event.flags == 0);

    IOS_TEST_ASSERT_STATUS(ps2_keyboard_handle_byte(&keyboard, 0x12, 12), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ps2_keyboard_handle_byte(&keyboard, 0x16, 13), IOS_OK);
    IOS_TEST_ASSERT_STATUS(input_queue_pop(&queue, &event), IOS_OK);
    IOS_TEST_ASSERT(event.code == IOS_KEY_LEFT_SHIFT);
    IOS_TEST_ASSERT_STATUS(input_queue_pop(&queue, &event), IOS_OK);
    IOS_TEST_ASSERT(event.text == '!' && (event.flags & IOS_INPUT_SHIFT) != 0);
    IOS_TEST_ASSERT_STATUS(ps2_keyboard_handle_byte(&keyboard, 0x16, 14), IOS_OK);
    IOS_TEST_ASSERT_STATUS(input_queue_pop(&queue, &event), IOS_OK);
    IOS_TEST_ASSERT((event.flags & IOS_INPUT_REPEAT) != 0);
}

static void test_keyboard_caps_lock_and_extended_arrow(void)
{
    struct ios_input_queue queue;
    struct ps2_keyboard keyboard;
    struct ios_input_event event;
    input_queue_initialize(&queue, 100, 100);
    ps2_keyboard_initialize(&keyboard, &queue);
    IOS_TEST_ASSERT_STATUS(ps2_keyboard_handle_byte(&keyboard, 0x58, 1), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ps2_keyboard_handle_byte(&keyboard, 0x1c, 2), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ps2_keyboard_handle_byte(&keyboard, 0xe0, 3), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ps2_keyboard_handle_byte(&keyboard, 0x6b, 3), IOS_OK);
    IOS_TEST_ASSERT_STATUS(input_queue_pop(&queue, &event), IOS_OK);
    IOS_TEST_ASSERT(event.code == IOS_KEY_CAPS_LOCK);
    IOS_TEST_ASSERT_STATUS(input_queue_pop(&queue, &event), IOS_OK);
    IOS_TEST_ASSERT(event.code == 'a' && event.text == 'A');
    IOS_TEST_ASSERT_STATUS(input_queue_pop(&queue, &event), IOS_OK);
    IOS_TEST_ASSERT(event.code == IOS_KEY_LEFT && event.text == 0);
}

static void test_mouse_normalizes_motion_clamps_and_emits_buttons(void)
{
    struct ios_input_queue queue;
    struct ps2_mouse mouse;
    struct ios_input_event event;
    input_queue_initialize(&queue, 100, 100);
    ps2_mouse_initialize(&mouse, &queue);
    IOS_TEST_ASSERT_STATUS(ps2_mouse_handle_byte(&mouse, 0x08, 20), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ps2_mouse_handle_byte(&mouse, 120, 20), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ps2_mouse_handle_byte(&mouse, 0x80, 20), IOS_OK);
    IOS_TEST_ASSERT_STATUS(input_queue_pop(&queue, &event), IOS_OK);
    IOS_TEST_ASSERT(event.type == IOS_INPUT_EVENT_POINTER_MOVE);
    IOS_TEST_ASSERT(event.x == 99 && event.y == 99);
    IOS_TEST_ASSERT(event.delta_x == 120 && event.delta_y == 128);

    IOS_TEST_ASSERT_STATUS(ps2_mouse_handle_byte(&mouse, 0x09, 21), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ps2_mouse_handle_byte(&mouse, 0, 21), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ps2_mouse_handle_byte(&mouse, 0, 21), IOS_OK);
    IOS_TEST_ASSERT_STATUS(input_queue_pop(&queue, &event), IOS_OK);
    IOS_TEST_ASSERT(event.type == IOS_INPUT_EVENT_POINTER_BUTTON);
    IOS_TEST_ASSERT(event.code == IOS_POINTER_BUTTON_LEFT);
    IOS_TEST_ASSERT((event.flags & IOS_INPUT_PRESSED) != 0);
}

static void test_queue_is_bounded_and_reports_drops(void)
{
    struct ios_input_queue queue;
    input_queue_initialize(&queue, 1, 1);
    for (ios_size index = 0; index < IOS_INPUT_QUEUE_CAPACITY; ++index) {
        IOS_TEST_ASSERT_STATUS(input_emit_key(&queue, index, 'a', 'a', IOS_INPUT_PRESSED), IOS_OK);
    }
    IOS_TEST_ASSERT_STATUS(
        input_emit_key(&queue, 999, 'b', 'b', IOS_INPUT_PRESSED), IOS_ERROR(IOS_E_NO_SPACE)
    );
    IOS_TEST_ASSERT(queue.count == IOS_INPUT_QUEUE_CAPACITY && queue.dropped_event_count == 1);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_set2_keyboard_make_break_modifiers_and_repeat),
    IOS_TEST_CASE(test_keyboard_caps_lock_and_extended_arrow),
    IOS_TEST_CASE(test_mouse_normalizes_motion_clamps_and_emits_buttons),
    IOS_TEST_CASE(test_queue_is_bounded_and_reports_drops)
};
const size_t ios_test_case_count = sizeof(ios_test_cases) / sizeof(ios_test_cases[0]);
