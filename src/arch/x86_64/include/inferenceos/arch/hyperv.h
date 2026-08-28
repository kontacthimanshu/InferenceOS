#ifndef INFERENCEOS_ARCH_HYPERV_H
#define INFERENCEOS_ARCH_HYPERV_H

#include <inferenceos/errors.h>

enum {
    X86_64_HV_CPUID_VENDOR_AND_MAX_FUNCTIONS = 0x40000000,
    X86_64_HV_CPUID_INTERFACE = 0x40000001,
    X86_64_HV_CPUID_FEATURES = 0x40000003,

    X86_64_HV_MSR_GUEST_OS_ID = 0x40000000,
    X86_64_HV_MSR_HYPERCALL = 0x40000001,
    X86_64_HV_MSR_VP_INDEX = 0x40000002,
    X86_64_HV_MSR_TIME_REF_COUNT = 0x40000020,
    X86_64_HV_MSR_SCONTROL = 0x40000080,
    X86_64_HV_MSR_SIEFP = 0x40000082,
    X86_64_HV_MSR_SIMP = 0x40000083,
    X86_64_HV_MSR_EOM = 0x40000084,
    X86_64_HV_MSR_SINT0 = 0x40000090,

    X86_64_HV_SYNIC_SINT_COUNT = 16
};

enum {
    X86_64_HV_MSR_ENABLE = UINT64_C(1),
    X86_64_HV_SINT_MASKED = UINT64_C(1) << 16,
    X86_64_HV_SINT_AUTO_EOI = UINT64_C(1) << 17,
    X86_64_HV_FEATURE_REFERENCE_COUNTER = UINT32_C(1) << 1,
    X86_64_HV_FEATURE_SYNIC = UINT32_C(1) << 2,
    X86_64_HV_FEATURE_HYPERCALL = UINT32_C(1) << 5,
    X86_64_HV_FEATURE_VP_INDEX = UINT32_C(1) << 6,
    X86_64_HV_INTERFACE_HV1 = UINT32_C(0x31237648)
};

struct x86_64_hyperv_capabilities {
    ios_u32 maximum_leaf;
    ios_u32 feature_eax;
    bool hypercall;
    bool synic;
    bool vp_index;
    bool reference_counter;
};

bool x86_64_hyperv_present(void);
ios_status x86_64_hyperv_query_capabilities(struct x86_64_hyperv_capabilities *capabilities);
ios_status x86_64_hyperv_read_reference_time(ios_u64 *reference_time);
ios_status x86_64_hyperv_enable_hypercall(
    ios_u64 guest_os_id, ios_uptr hypercall_page_physical
);
ios_status x86_64_hyperv_enable_synic(
    ios_uptr message_page_physical,
    ios_uptr event_page_physical,
    ios_u8 sint,
    ios_u8 vector
);
void x86_64_hyperv_end_of_message(void);
ios_u64 x86_64_hyperv_hypercall(
    const void *hypercall_page,
    ios_u64 control,
    ios_uptr input_physical,
    ios_uptr output_physical
);

#endif
