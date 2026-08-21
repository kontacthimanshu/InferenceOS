#ifndef INFERENCEOS_CONSOLE_H
#define INFERENCEOS_CONSOLE_H

#include <inferenceos/framebuffer.h>

/* Initializes COM1 and, when config is non-null and valid, GOP output. The
 * console succeeds when at least one output sink is available. */
inferenceos_result inferenceos_console_initialize(
    const inferenceos_framebuffer_config *config
);
bool inferenceos_console_is_available(void);
inferenceos_result inferenceos_console_write_byte(inferenceos_u8 byte);
inferenceos_result inferenceos_console_write(
    const char *data,
    inferenceos_size length
);
inferenceos_result inferenceos_console_write_bounded_string(
    const char *text,
    inferenceos_size capacity
);
inferenceos_result inferenceos_console_clear(void);

#endif
