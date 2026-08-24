#ifndef INFERENCEOS_DRIVERS_PS2_H
#define INFERENCEOS_DRIVERS_PS2_H

#include <inferenceos/gui/input.h>

struct ps2_keyboard {
    struct ios_input_queue *queue;
    bool break_prefix;
    bool extended_prefix;
    ios_u8 pause_bytes_remaining;
    bool left_shift;
    bool right_shift;
    bool left_control;
    bool right_control;
    bool left_alt;
    bool right_alt;
    bool caps_lock;
    bool key_down[512];
};

struct ps2_mouse {
    struct ios_input_queue *queue;
    ios_u8 packet[3];
    ios_u8 packet_size;
    ios_u8 buttons;
};

void ps2_keyboard_initialize(
    struct ps2_keyboard *keyboard, struct ios_input_queue *queue
);
ios_status ps2_keyboard_handle_byte(
    struct ps2_keyboard *keyboard, ios_u8 byte, ios_u64 timestamp_ticks
);
ios_status ps2_keyboard_hardware_initialize(void);
ios_status ps2_keyboard_interrupt(
    struct ps2_keyboard *keyboard, ios_u64 timestamp_ticks
);

void ps2_mouse_initialize(struct ps2_mouse *mouse, struct ios_input_queue *queue);
ios_status ps2_mouse_handle_byte(
    struct ps2_mouse *mouse, ios_u8 byte, ios_u64 timestamp_ticks
);
ios_status ps2_mouse_hardware_initialize(void);
ios_status ps2_mouse_interrupt(struct ps2_mouse *mouse, ios_u64 timestamp_ticks);

#endif
