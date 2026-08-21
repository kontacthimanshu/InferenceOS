#ifndef INFERENCEOS_KEYBOARD_H
#define INFERENCEOS_KEYBOARD_H

#include <inferenceos/base.h>
#include <inferenceos/result.h>

#define INFERENCEOS_KEYBOARD_POLL_LIMIT UINT32_C(100000)

inferenceos_result inferenceos_ps2_keyboard_initialize(void);
bool inferenceos_ps2_keyboard_is_available(void);

/* Performs one bounded controller observation. NOT_READY means that no
 * complete supported key press is currently available. */
inferenceos_result inferenceos_ps2_keyboard_poll(
    inferenceos_u8 *character
);

/* Polls up to INFERENCEOS_KEYBOARD_POLL_LIMIT observations for one printable
 * ASCII byte, Backspace ('\b'), or Enter ('\n'). */
inferenceos_result inferenceos_ps2_keyboard_read(
    inferenceos_u8 *character
);

#endif
