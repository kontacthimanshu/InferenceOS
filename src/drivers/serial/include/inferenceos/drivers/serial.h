#ifndef INFERENCEOS_DRIVERS_SERIAL_H
#define INFERENCEOS_DRIVERS_SERIAL_H

#include <inferenceos/base.h>

bool serial_initialize(void);
bool serial_is_ready(void);
bool serial_try_write_character(char character);
void serial_write(const char *text);
void serial_write_line(const char *text);
void serial_write_hex_u64(ios_u64 value);
void serial_write_decimal_u64(ios_u64 value);

#endif
