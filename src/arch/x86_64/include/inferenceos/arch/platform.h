#ifndef INFERENCEOS_ARCH_PLATFORM_H
#define INFERENCEOS_ARCH_PLATFORM_H

#include <inferenceos/base.h>
#include <inferenceos/errors.h>

enum {
    X86_64_APIC_TIMER_VECTOR = 0x40,
    X86_64_SCHEDULER_QUANTUM_MS = 10
};

struct x86_64_platform_info {
    ios_uptr local_apic_address;
    ios_u8 bootstrap_apic_id;
    ios_u8 enabled_processor_count;
    ios_u16 pm_timer_port;
    ios_u8 pm_timer_width;
};

ios_status x86_64_acpi_discover(
    const void *root_system_description_pointer,
    struct x86_64_platform_info *platform
);
void x86_64_pic_mask_and_remap(void);
ios_status x86_64_apic_timer_initialize(const struct x86_64_platform_info *platform);
void x86_64_apic_timer_acknowledge(void);
ios_u32 x86_64_apic_timer_ticks_per_quantum(void);

#endif
