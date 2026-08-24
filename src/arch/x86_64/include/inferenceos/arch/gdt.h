#ifndef INFERENCEOS_ARCH_GDT_H
#define INFERENCEOS_ARCH_GDT_H

#include <inferenceos/base.h>

enum x86_64_segment_selector {
    X86_64_KERNEL_CODE_SELECTOR = 0x08,
    X86_64_KERNEL_DATA_SELECTOR = 0x10,
    X86_64_USER_DATA_SELECTOR = 0x1b,
    X86_64_USER_CODE_SELECTOR = 0x23,
    X86_64_TSS_SELECTOR = 0x28
};

void x86_64_gdt_initialize(void);
void x86_64_gdt_set_kernel_stack(ios_uptr stack_pointer);

#endif
