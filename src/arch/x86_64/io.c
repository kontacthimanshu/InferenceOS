#include <inferenceos/arch/io.h>

void x86_64_port_write8(ios_u16 port, ios_u8 value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

void x86_64_port_write16(ios_u16 port, ios_u16 value)
{
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

void x86_64_port_write32(ios_u16 port, ios_u32 value)
{
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

ios_u8 x86_64_port_read8(ios_u16 port)
{
    ios_u8 value;

    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port) : "memory");
    return value;
}

ios_u32 x86_64_port_read32(ios_u16 port)
{
    ios_u32 value;

    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port) : "memory");
    return value;
}

void x86_64_memory_barrier(void)
{
    __asm__ volatile("mfence" : : : "memory");
}

void x86_64_cpu_relax(void)
{
    __asm__ volatile("pause" : : : "memory");
}
