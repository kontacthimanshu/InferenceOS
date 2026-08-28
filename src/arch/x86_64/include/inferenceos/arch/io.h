#ifndef INFERENCEOS_ARCH_IO_H
#define INFERENCEOS_ARCH_IO_H

#include <inferenceos/base.h>

/* Ordered byte-wide x86 port I/O. These operations are architecture barriers. */
void x86_64_port_write8(ios_u16 port, ios_u8 value);
void x86_64_port_write16(ios_u16 port, ios_u16 value);
void x86_64_port_write32(ios_u16 port, ios_u32 value);
ios_u8 x86_64_port_read8(ios_u16 port);
ios_u32 x86_64_port_read32(ios_u16 port);

struct x86_64_cpuid_result {
    ios_u32 eax;
    ios_u32 ebx;
    ios_u32 ecx;
    ios_u32 edx;
};

struct x86_64_cpuid_result x86_64_cpuid(ios_u32 leaf, ios_u32 subleaf);
ios_u64 x86_64_msr_read(ios_u32 msr);
void x86_64_msr_write(ios_u32 msr, ios_u64 value);
void x86_64_memory_barrier(void);
void x86_64_cpu_relax(void);

#endif
