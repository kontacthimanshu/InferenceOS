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

struct x86_64_cpuid_result x86_64_cpuid(ios_u32 leaf, ios_u32 subleaf)
{
    struct x86_64_cpuid_result result;

    result.eax = leaf;
    result.ecx = subleaf;
    __asm__ volatile("cpuid"
                     : "+a"(result.eax), "=b"(result.ebx),
                       "+c"(result.ecx), "=d"(result.edx)
                     :
                     : "memory");
    return result;
}

ios_u64 x86_64_msr_read(ios_u32 msr)
{
    ios_u32 low;
    ios_u32 high;

    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr) : "memory");
    return ((ios_u64)high << 32) | low;
}

void x86_64_msr_write(ios_u32 msr, ios_u64 value)
{
    __asm__ volatile("wrmsr"
                     :
                     : "c"(msr), "a"((ios_u32)value), "d"((ios_u32)(value >> 32))
                     : "memory");
}

void x86_64_memory_barrier(void)
{
    __asm__ volatile("mfence" : : : "memory");
}

void x86_64_cpu_relax(void)
{
    __asm__ volatile("pause" : : : "memory");
}
