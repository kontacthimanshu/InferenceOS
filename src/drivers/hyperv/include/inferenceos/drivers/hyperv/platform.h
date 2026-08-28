#ifndef INFERENCEOS_DRIVERS_HYPERV_PLATFORM_H
#define INFERENCEOS_DRIVERS_HYPERV_PLATFORM_H

#include <inferenceos/errors.h>

struct ios_input_queue;

ios_status hyperv_platform_input_initialize(struct ios_input_queue *queue);
void hyperv_platform_input_poll(ios_u64 timestamp_ticks);
const char *hyperv_platform_keyboard_stage(void);
ios_status hyperv_platform_keyboard_status(void);
const char *hyperv_platform_mouse_stage(void);
ios_status hyperv_platform_mouse_status(void);

#endif
