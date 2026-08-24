#include <inferenceos/arch/syscall.h>

#include <inferenceos/arch/gdt.h>

#define X86_64_CPUID_EXTENDED_MAX UINT32_C(0x80000000)
#define X86_64_CPUID_EXTENDED_FEATURES UINT32_C(0x80000001)
#define X86_64_MSR_EFER UINT32_C(0xc0000080)
#define X86_64_MSR_STAR UINT32_C(0xc0000081)
#define X86_64_MSR_LSTAR UINT32_C(0xc0000082)
#define X86_64_MSR_FMASK UINT32_C(0xc0000084)

enum {
    X86_64_CPUID_SYSCALL_BIT = 11,
    X86_64_EFER_SCE_BIT = 0,
    X86_64_SYSRET_SELECTOR_BASE = 0x13
};

static void cpuid(ios_u32 leaf, ios_u32 *eax, ios_u32 *ebx, ios_u32 *ecx, ios_u32 *edx)
{
    __asm__ volatile(
        "cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(0U)
        : "memory"
    );
}

static ios_u64 read_msr(ios_u32 msr)
{
    ios_u32 low;
    ios_u32 high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr) : "memory");
    return ((ios_u64)high << 32) | low;
}

static void write_msr(ios_u32 msr, ios_u64 value)
{
    __asm__ volatile(
        "wrmsr" : : "c"(msr), "a"((ios_u32)value), "d"((ios_u32)(value >> 32)) : "memory"
    );
}

ios_status x86_64_syscall_configure(ios_uptr entry_point)
{
    ios_u32 eax;
    ios_u32 ebx;
    ios_u32 ecx;
    ios_u32 edx;
    ios_u64 efer;
    const ios_u64 star = ((ios_u64)X86_64_SYSRET_SELECTOR_BASE << 48)
        | ((ios_u64)X86_64_KERNEL_CODE_SELECTOR << 32);
    const ios_u64 flags_to_clear = UINT64_C(0x700) | UINT64_C(0x40000);

    if (entry_point == 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    cpuid(X86_64_CPUID_EXTENDED_MAX, &eax, &ebx, &ecx, &edx);
    if (eax < X86_64_CPUID_EXTENDED_FEATURES) {
        return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    }
    cpuid(X86_64_CPUID_EXTENDED_FEATURES, &eax, &ebx, &ecx, &edx);
    if ((edx & (UINT32_C(1) << X86_64_CPUID_SYSCALL_BIT)) == 0) {
        return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    }
    efer = read_msr(X86_64_MSR_EFER) | (UINT64_C(1) << X86_64_EFER_SCE_BIT);
    write_msr(X86_64_MSR_STAR, star);
    write_msr(X86_64_MSR_LSTAR, entry_point);
    write_msr(X86_64_MSR_FMASK, flags_to_clear);
    write_msr(X86_64_MSR_EFER, efer);
    return IOS_OK;
}
