#ifndef INFERENCEOS_SERIAL_H
#define INFERENCEOS_SERIAL_H

#include <inferenceos/base.h>
#include <inferenceos/result.h>

#define INFERENCEOS_SERIAL_COM1_PORT UINT16_C(0x03F8)
#define INFERENCEOS_SERIAL_POLL_LIMIT UINT32_C(100000)

inferenceos_result inferenceos_serial_initialize(void);
bool inferenceos_serial_is_available(void);

inferenceos_result inferenceos_serial_write_byte(inferenceos_u8 byte);

inferenceos_result inferenceos_serial_write(
    const char *data,
    inferenceos_size length
);

/* Writes up to capacity characters, stopping at a null terminator. */
inferenceos_result inferenceos_serial_write_bounded_string(
    const char *text,
    inferenceos_size capacity
);

#endif
