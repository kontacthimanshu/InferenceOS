#include <inferenceos/arch/paging.h>

#define X86_64_CPUID_EXTENDED_MAX UINT32_C(0x80000000)
#define X86_64_CPUID_EXTENDED_FEATURES UINT32_C(0x80000001)
#define X86_64_MSR_EFER UINT32_C(0xc0000080)

enum {
    X86_64_CPUID_NX_BIT = 20,
    X86_64_EFER_NXE_BIT = 11
};

static void cpuid(
    ios_u32 leaf,
    ios_u32 *eax,
    ios_u32 *ebx,
    ios_u32 *ecx,
    ios_u32 *edx
)
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
        "wrmsr"
        :
        : "c"(msr), "a"((ios_u32)value), "d"((ios_u32)(value >> 32))
        : "memory"
    );
}

bool x86_64_paging_enable_execute_disable(void)
{
    ios_u32 eax;
    ios_u32 ebx;
    ios_u32 ecx;
    ios_u32 edx;
    ios_u64 efer;

    cpuid(X86_64_CPUID_EXTENDED_MAX, &eax, &ebx, &ecx, &edx);
    if (eax < X86_64_CPUID_EXTENDED_FEATURES) {
        return false;
    }

    cpuid(X86_64_CPUID_EXTENDED_FEATURES, &eax, &ebx, &ecx, &edx);
    if ((edx & (UINT32_C(1) << X86_64_CPUID_NX_BIT)) == 0U) {
        return false;
    }

    efer = read_msr(X86_64_MSR_EFER);
    efer |= UINT64_C(1) << X86_64_EFER_NXE_BIT;
    write_msr(X86_64_MSR_EFER, efer);
    return true;
}

ios_uptr x86_64_paging_root(void)
{
    ios_uptr root;

    __asm__ volatile("movq %%cr3, %0" : "=r"(root) : : "memory");
    return root & UINT64_C(0x000ffffffffff000);
}

void x86_64_paging_activate(ios_uptr root_address)
{
    IOS_ASSERT((root_address & UINT64_C(0xfff)) == 0);
    __asm__ volatile("movq %0, %%cr3" : : "r"(root_address) : "memory");
}

void x86_64_paging_invalidate(ios_uptr virtual_address)
{
    __asm__ volatile("invlpg (%0)" : : "r"(virtual_address) : "memory");
}
