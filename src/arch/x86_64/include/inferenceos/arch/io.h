#ifndef INFERENCEOS_ARCH_IO_H
#define INFERENCEOS_ARCH_IO_H

#include <inferenceos/base.h>

/* Ordered byte-wide x86 port I/O. These operations are architecture barriers. */
void x86_64_port_write8(ios_u16 port, ios_u8 value);
ios_u8 x86_64_port_read8(ios_u16 port);
ios_u32 x86_64_port_read32(ios_u16 port);

#endif
