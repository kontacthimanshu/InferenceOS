#include <inferenceos/gui/input.h>

#include <inferenceos/arch/interrupts.h>
#include <inferenceos/runtime.h>

IOS_STATIC_ASSERT(sizeof(struct ios_input_event) == 48, "normalized input-event ABI size");

static bool event_is_valid(const struct ios_input_event *event)
{
    const ios_u32 known_flags = IOS_INPUT_PRESSED | IOS_INPUT_REPEAT | IOS_INPUT_SHIFT
        | IOS_INPUT_CONTROL | IOS_INPUT_ALT | IOS_INPUT_CAPS_LOCK;
    if ((event->flags & ~known_flags) != 0 || event->reserved != 0) { return false; }
    switch (event->type) {
    case IOS_INPUT_EVENT_KEY:
        return event->code != IOS_KEY_NONE && event->x == 0 && event->y == 0
            && event->delta_x == 0 && event->delta_y == 0;
    case IOS_INPUT_EVENT_POINTER_MOVE:
        return event->flags == 0 && event->code == 0 && event->text == 0;
    case IOS_INPUT_EVENT_POINTER_BUTTON:
        return event->code >= IOS_POINTER_BUTTON_LEFT
            && event->code <= IOS_POINTER_BUTTON_MIDDLE
            && (event->flags & ~IOS_INPUT_PRESSED) == 0 && event->delta_x == 0
            && event->delta_y == 0 && event->text == 0;
    default:
        return false;
    }
}

void input_queue_initialize(
    struct ios_input_queue *queue, ios_i32 pointer_width, ios_i32 pointer_height
) {
    if (queue == NULL) { return; }
    memset(queue, 0, sizeof(*queue));
    queue->pointer_width = pointer_width > 0 ? pointer_width : 1;
    queue->pointer_height = pointer_height > 0 ? pointer_height : 1;
}

ios_status input_queue_push(struct ios_input_queue *queue, const struct ios_input_event *event)
{
    if (queue == NULL || event == NULL || event->structure_size != sizeof(*event)
        || event->structure_version != IOS_INPUT_EVENT_VERSION || !event_is_valid(event)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    const ios_u64 interrupt_state = x86_64_interrupt_save_disable();
    if (queue->count == IOS_INPUT_QUEUE_CAPACITY) {
        ++queue->dropped_event_count;
        x86_64_interrupt_restore(interrupt_state);
        return IOS_ERROR(IOS_E_NO_SPACE);
    }
    queue->events[(queue->head + queue->count) % IOS_INPUT_QUEUE_CAPACITY] = *event;
    ++queue->count;
    x86_64_interrupt_restore(interrupt_state);
    return IOS_OK;
}

ios_status input_queue_pop(struct ios_input_queue *queue, struct ios_input_event *event)
{
    if (queue == NULL || event == NULL) { return IOS_ERROR(IOS_E_INVALID_ARGUMENT); }
    const ios_u64 interrupt_state = x86_64_interrupt_save_disable();
    if (queue->count == 0) {
        x86_64_interrupt_restore(interrupt_state);
        return IOS_ERROR(IOS_E_WOULD_BLOCK);
    }
    *event = queue->events[queue->head];
    queue->head = (queue->head + 1) % IOS_INPUT_QUEUE_CAPACITY;
    --queue->count;
    x86_64_interrupt_restore(interrupt_state);
    return IOS_OK;
}

ios_status input_emit_key(
    struct ios_input_queue *queue, ios_u64 timestamp_ticks, ios_u32 key_code,
    ios_u32 text, ios_u32 flags
) {
    const struct ios_input_event event = {
        .structure_size = sizeof(struct ios_input_event),
        .structure_version = IOS_INPUT_EVENT_VERSION,
        .type = IOS_INPUT_EVENT_KEY,
        .timestamp_ticks = timestamp_ticks,
        .flags = flags,
        .code = key_code,
        .text = text
    };
    return input_queue_push(queue, &event);
}

ios_status input_emit_pointer_move(
    struct ios_input_queue *queue, ios_u64 timestamp_ticks, ios_i32 delta_x, ios_i32 delta_y
) {
    struct ios_input_event event;
    if (queue == NULL || queue->pointer_width <= 0 || queue->pointer_height <= 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    ios_i64 next_x = (ios_i64)queue->pointer_x + delta_x;
    ios_i64 next_y = (ios_i64)queue->pointer_y + delta_y;
    if (next_x < 0) { next_x = 0; }
    if (next_y < 0) { next_y = 0; }
    if (next_x >= queue->pointer_width) { next_x = queue->pointer_width - 1; }
    if (next_y >= queue->pointer_height) { next_y = queue->pointer_height - 1; }
    queue->pointer_x = (ios_i32)next_x;
    queue->pointer_y = (ios_i32)next_y;
    event = (struct ios_input_event) {
        .structure_size = sizeof(struct ios_input_event),
        .structure_version = IOS_INPUT_EVENT_VERSION,
        .type = IOS_INPUT_EVENT_POINTER_MOVE,
        .timestamp_ticks = timestamp_ticks,
        .x = queue->pointer_x,
        .y = queue->pointer_y,
        .delta_x = delta_x,
        .delta_y = delta_y
    };
    return input_queue_push(queue, &event);
}

ios_status input_emit_pointer_button(
    struct ios_input_queue *queue, ios_u64 timestamp_ticks, ios_u32 button, bool pressed
) {
    if (queue == NULL || button < IOS_POINTER_BUTTON_LEFT
        || button > IOS_POINTER_BUTTON_MIDDLE) { return IOS_ERROR(IOS_E_INVALID_ARGUMENT); }
    const struct ios_input_event event = {
        .structure_size = sizeof(struct ios_input_event),
        .structure_version = IOS_INPUT_EVENT_VERSION,
        .type = IOS_INPUT_EVENT_POINTER_BUTTON,
        .timestamp_ticks = timestamp_ticks,
        .flags = pressed ? IOS_INPUT_PRESSED : 0,
        .code = button,
        .x = queue->pointer_x,
        .y = queue->pointer_y
    };
    return input_queue_push(queue, &event);
}
