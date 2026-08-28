#ifndef INFERENCEOS_DRIVERS_HYPERV_INPUT_H
#define INFERENCEOS_DRIVERS_HYPERV_INPUT_H

#include <inferenceos/drivers/hyperv/vmbus.h>
#include <inferenceos/gui/input.h>

struct ios_hyperv_keyboard {
    struct ios_vmbus *bus;
    struct ios_vmbus_channel *channel;
    struct ios_input_queue *queue;
    bool key_down[512];
    bool shift;
    bool control;
    bool alt;
    bool caps_lock;
    bool ready;
};

struct ios_hyperv_mouse {
    struct ios_vmbus *bus;
    struct ios_vmbus_channel *channel;
    struct ios_input_queue *queue;
    ios_u8 buttons;
    bool ready;
};

ios_status hyperv_keyboard_initialize(
    struct ios_hyperv_keyboard *keyboard, struct ios_vmbus *bus,
    struct ios_vmbus_channel *channel, struct ios_input_queue *queue, ios_u32 spin_limit
);
ios_status hyperv_keyboard_handle_event(
    struct ios_hyperv_keyboard *keyboard, ios_u16 make_code,
    ios_u32 information, ios_u64 timestamp_ticks
);
ios_status hyperv_keyboard_poll(
    struct ios_hyperv_keyboard *keyboard, ios_u64 timestamp_ticks
);
ios_status hyperv_mouse_initialize(
    struct ios_hyperv_mouse *mouse, struct ios_vmbus *bus,
    struct ios_vmbus_channel *channel, struct ios_input_queue *queue, ios_u32 spin_limit
);
ios_status hyperv_mouse_handle_report(
    struct ios_hyperv_mouse *mouse, const ios_u8 *report,
    ios_size report_size, ios_u64 timestamp_ticks
);
ios_status hyperv_mouse_poll(struct ios_hyperv_mouse *mouse, ios_u64 timestamp_ticks);

#endif
