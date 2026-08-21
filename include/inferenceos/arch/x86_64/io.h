#ifndef INFERENCEOS_ARCH_X86_64_IO_H
#define INFERENCEOS_ARCH_X86_64_IO_H

#include <inferenceos/base.h>

/* Port I/O is x86-64-specific. The port is supplied in DX, the value is
 * transferred through AL, and the memory clobber prevents compiler movement
 * of memory accesses across the I/O operation. */
static inline void inferenceos_arch_out8(
    inferenceos_u16 port,
    inferenceos_u8 value
)
{
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

static inline inferenceos_u8 inferenceos_arch_in8(inferenceos_u16 port)
{
    inferenceos_u8 value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port) : "memory");
    return value;
}

static inline void inferenceos_arch_out16(
    inferenceos_u16 port,
    inferenceos_u16 value
)
{
    __asm__ __volatile__("outw %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

static inline inferenceos_u16 inferenceos_arch_in16(inferenceos_u16 port)
{
    inferenceos_u16 value;
    __asm__ __volatile__("inw %1, %0" : "=a"(value) : "Nd"(port) : "memory");
    return value;
}

#endif
