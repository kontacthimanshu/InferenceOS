#include <inferenceos/arch/hyperv.h>

#include <inferenceos/arch/interrupts.h>
#include <inferenceos/arch/io.h>

extern ios_u64 x86_64_hyperv_invoke(
    const void *hypercall_page,
    ios_u64 control,
    ios_uptr input_physical,
    ios_uptr output_physical
);

static bool hyperv_vendor_matches(struct x86_64_cpuid_result vendor)
{
    return vendor.ebx == UINT32_C(0x7263694d) /* Micr */
        && vendor.ecx == UINT32_C(0x666f736f) /* osof */
        && vendor.edx == UINT32_C(0x76482074); /* t Hv */
}

static void hyperv_synic_interrupt(struct x86_64_interrupt_frame *frame)
{
    /*
     * VMBus is deliberately driven by bounded polling in the platform layer.
     * The interrupt only wakes a halted/idle VP; the SINT is configured for
     * automatic EOI, so no LAPIC acknowledgement is required here.
     */
    (void)frame;
}

bool x86_64_hyperv_present(void)
{
    const struct x86_64_cpuid_result vendor =
        x86_64_cpuid(X86_64_HV_CPUID_VENDOR_AND_MAX_FUNCTIONS, 0);
    struct x86_64_cpuid_result interface;

    if (!hyperv_vendor_matches(vendor) || vendor.eax < X86_64_HV_CPUID_INTERFACE) {
        return false;
    }
    interface = x86_64_cpuid(X86_64_HV_CPUID_INTERFACE, 0);
    return interface.eax == X86_64_HV_INTERFACE_HV1;
}

ios_status x86_64_hyperv_query_capabilities(struct x86_64_hyperv_capabilities *capabilities)
{
    const struct x86_64_cpuid_result vendor =
        x86_64_cpuid(X86_64_HV_CPUID_VENDOR_AND_MAX_FUNCTIONS, 0);
    struct x86_64_cpuid_result interface;
    struct x86_64_cpuid_result features;

    if (capabilities == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (!hyperv_vendor_matches(vendor) || vendor.eax < X86_64_HV_CPUID_FEATURES) {
        return IOS_ERROR(IOS_E_NOT_FOUND);
    }
    interface = x86_64_cpuid(X86_64_HV_CPUID_INTERFACE, 0);
    if (interface.eax != X86_64_HV_INTERFACE_HV1) {
        return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    }
    features = x86_64_cpuid(X86_64_HV_CPUID_FEATURES, 0);
    capabilities->maximum_leaf = vendor.eax;
    capabilities->feature_eax = features.eax;
    capabilities->hypercall = (features.eax & X86_64_HV_FEATURE_HYPERCALL) != 0;
    capabilities->synic = (features.eax & X86_64_HV_FEATURE_SYNIC) != 0;
    capabilities->vp_index = (features.eax & X86_64_HV_FEATURE_VP_INDEX) != 0;
    capabilities->reference_counter =
        (features.eax & X86_64_HV_FEATURE_REFERENCE_COUNTER) != 0;
    if (!capabilities->hypercall || !capabilities->synic) {
        return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    }
    return IOS_OK;
}

ios_status x86_64_hyperv_read_reference_time(ios_u64 *reference_time)
{
    const struct x86_64_cpuid_result vendor =
        x86_64_cpuid(X86_64_HV_CPUID_VENDOR_AND_MAX_FUNCTIONS, 0);
    struct x86_64_cpuid_result features;

    if (reference_time == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (!hyperv_vendor_matches(vendor) || vendor.eax < X86_64_HV_CPUID_FEATURES) {
        return IOS_ERROR(IOS_E_NOT_FOUND);
    }
    features = x86_64_cpuid(X86_64_HV_CPUID_FEATURES, 0);
    if ((features.eax & X86_64_HV_FEATURE_REFERENCE_COUNTER) == 0) {
        return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    }
    *reference_time = x86_64_msr_read(X86_64_HV_MSR_TIME_REF_COUNT);
    return IOS_OK;
}

ios_status x86_64_hyperv_enable_hypercall(
    ios_u64 guest_os_id, ios_uptr hypercall_page_physical
)
{
    struct x86_64_hyperv_capabilities capabilities;
    ios_status status;

    if (guest_os_id == 0 || (hypercall_page_physical & 0xfffU) != 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    status = x86_64_hyperv_query_capabilities(&capabilities);
    if (IOS_FAILED(status)) {
        return status;
    }
    x86_64_msr_write(X86_64_HV_MSR_GUEST_OS_ID, guest_os_id);
    x86_64_msr_write(X86_64_HV_MSR_HYPERCALL,
        (ios_u64)hypercall_page_physical | X86_64_HV_MSR_ENABLE);
    x86_64_memory_barrier();
    return IOS_OK;
}

ios_status x86_64_hyperv_enable_synic(
    ios_uptr message_page_physical,
    ios_uptr event_page_physical,
    ios_u8 sint,
    ios_u8 vector
)
{
    struct x86_64_hyperv_capabilities capabilities;
    ios_status status;

    if ((message_page_physical & 0xfffU) != 0 || (event_page_physical & 0xfffU) != 0
        || sint >= X86_64_HV_SYNIC_SINT_COUNT || vector < 0x20) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    status = x86_64_hyperv_query_capabilities(&capabilities);
    if (IOS_FAILED(status)) {
        return status;
    }
    x86_64_interrupt_set_handler(vector, hyperv_synic_interrupt);
    x86_64_msr_write(X86_64_HV_MSR_SIMP,
        (ios_u64)message_page_physical | X86_64_HV_MSR_ENABLE);
    x86_64_msr_write(X86_64_HV_MSR_SIEFP,
        (ios_u64)event_page_physical | X86_64_HV_MSR_ENABLE);
    x86_64_msr_write(X86_64_HV_MSR_SINT0 + sint,
        (ios_u64)vector | X86_64_HV_SINT_AUTO_EOI);
    x86_64_msr_write(X86_64_HV_MSR_SCONTROL, X86_64_HV_MSR_ENABLE);
    x86_64_memory_barrier();
    return IOS_OK;
}

void x86_64_hyperv_end_of_message(void)
{
    x86_64_msr_write(X86_64_HV_MSR_EOM, 0);
}

ios_u64 x86_64_hyperv_hypercall(
    const void *hypercall_page,
    ios_u64 control,
    ios_uptr input_physical,
    ios_uptr output_physical
)
{
    if (hypercall_page == NULL) {
        return UINT64_MAX;
    }
    return x86_64_hyperv_invoke(
        hypercall_page, control, input_physical, output_physical);
}
