#ifndef INFERENCEOS_GUI_INPUT_H
#define INFERENCEOS_GUI_INPUT_H

#include <inferenceos/base.h>
#include <inferenceos/errors.h>

enum {
    IOS_INPUT_EVENT_VERSION = 1,
    IOS_INPUT_QUEUE_CAPACITY = 128
};

enum ios_input_event_type {
    IOS_INPUT_EVENT_KEY = 1,
    IOS_INPUT_EVENT_POINTER_MOVE = 2,
    IOS_INPUT_EVENT_POINTER_BUTTON = 3
};

enum ios_input_event_flags {
    IOS_INPUT_PRESSED = UINT32_C(1) << 0,
    IOS_INPUT_REPEAT = UINT32_C(1) << 1,
    IOS_INPUT_SHIFT = UINT32_C(1) << 2,
    IOS_INPUT_CONTROL = UINT32_C(1) << 3,
    IOS_INPUT_ALT = UINT32_C(1) << 4,
    IOS_INPUT_CAPS_LOCK = UINT32_C(1) << 5
};

enum ios_input_key_code {
    IOS_KEY_NONE = 0,
    IOS_KEY_BACKSPACE = 8,
    IOS_KEY_ENTER = 13,
    IOS_KEY_ESCAPE = 27,
    IOS_KEY_SPACE = 32,
    IOS_KEY_LEFT = 256,
    IOS_KEY_RIGHT,
    IOS_KEY_UP,
    IOS_KEY_DOWN,
    IOS_KEY_LEFT_SHIFT,
    IOS_KEY_RIGHT_SHIFT,
    IOS_KEY_LEFT_CONTROL,
    IOS_KEY_RIGHT_CONTROL,
    IOS_KEY_LEFT_ALT,
    IOS_KEY_RIGHT_ALT,
    IOS_KEY_CAPS_LOCK
};

enum ios_input_pointer_button {
    IOS_POINTER_BUTTON_LEFT = 1,
    IOS_POINTER_BUTTON_RIGHT = 2,
    IOS_POINTER_BUTTON_MIDDLE = 3
};

struct ios_input_event {
    ios_u16 structure_size;
    ios_u16 structure_version;
    ios_u32 type;
    ios_u64 timestamp_ticks;
    ios_u32 flags;
    ios_u32 code;
    ios_i32 x;
    ios_i32 y;
    ios_i32 delta_x;
    ios_i32 delta_y;
    ios_u32 text;
    ios_u32 reserved;
};

struct ios_input_queue {
    struct ios_input_event events[IOS_INPUT_QUEUE_CAPACITY];
    ios_size head;
    ios_size count;
    ios_u64 dropped_event_count;
    ios_i32 pointer_x;
    ios_i32 pointer_y;
    ios_i32 pointer_width;
    ios_i32 pointer_height;
};

void input_queue_initialize(
    struct ios_input_queue *queue, ios_i32 pointer_width, ios_i32 pointer_height
);
ios_status input_queue_push(struct ios_input_queue *queue, const struct ios_input_event *event);
ios_status input_queue_pop(struct ios_input_queue *queue, struct ios_input_event *event);
ios_status input_emit_key(
    struct ios_input_queue *queue, ios_u64 timestamp_ticks, ios_u32 key_code,
    ios_u32 text, ios_u32 flags
);
ios_status input_emit_pointer_move(
    struct ios_input_queue *queue, ios_u64 timestamp_ticks, ios_i32 delta_x, ios_i32 delta_y
);
ios_status input_emit_pointer_button(
    struct ios_input_queue *queue, ios_u64 timestamp_ticks, ios_u32 button, bool pressed
);

#endif
