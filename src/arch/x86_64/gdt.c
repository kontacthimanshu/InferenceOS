#include <inferenceos/arch/gdt.h>

#include <stddef.h>

enum {
    X86_64_GDT_ENTRY_COUNT = 7,
    X86_64_DOUBLE_FAULT_STACK_SIZE = 16384
};

struct IOS_PACKED x86_64_descriptor_pointer {
    ios_u16 limit;
    ios_u64 base;
};

struct IOS_PACKED x86_64_tss {
    ios_u32 reserved0;
    ios_u64 rsp0;
    ios_u64 rsp1;
    ios_u64 rsp2;
    ios_u64 reserved1;
    ios_u64 ist1;
    ios_u64 ist2;
    ios_u64 ist3;
    ios_u64 ist4;
    ios_u64 ist5;
    ios_u64 ist6;
    ios_u64 ist7;
    ios_u64 reserved2;
    ios_u16 reserved3;
    ios_u16 io_map_base;
};

IOS_STATIC_ASSERT(sizeof(struct x86_64_descriptor_pointer) == 10,
                  "x86-64 descriptor pointer must be 10 bytes");
IOS_STATIC_ASSERT(sizeof(struct x86_64_tss) == 104,
                  "x86-64 TSS must be 104 bytes");
IOS_STATIC_ASSERT(offsetof(struct x86_64_tss, io_map_base) == 102,
                  "x86-64 TSS I/O-map offset must be 102");

static ios_u64 gdt[X86_64_GDT_ENTRY_COUNT] IOS_ALIGNED(16);
static struct x86_64_tss tss IOS_ALIGNED(16);
static ios_u8 double_fault_stack[X86_64_DOUBLE_FAULT_STACK_SIZE] IOS_ALIGNED(16);

extern ios_u8 __bootstrap_stack_top[];
extern void x86_64_load_gdt(const struct x86_64_descriptor_pointer *descriptor);
extern void x86_64_load_task_register(ios_u16 selector);

static void set_tss_descriptor(ios_uptr base, ios_u32 limit)
{
    gdt[5] = ((ios_u64)(limit & 0xffffU))
        | ((ios_u64)(base & 0xffffffU) << 16)
        | ((ios_u64)0x89U << 40)
        | ((ios_u64)((limit >> 16) & 0x0fU) << 48)
        | ((ios_u64)((base >> 24) & 0xffU) << 56);
    gdt[6] = (ios_u64)(base >> 32);
}

void x86_64_gdt_initialize(void)
{
    const struct x86_64_descriptor_pointer descriptor = {
        .limit = (ios_u16)(sizeof(gdt) - 1U),
        .base = (ios_u64)(ios_uptr)gdt
    };

    *gdt = 0;
    gdt[1] = UINT64_C(0x00af9a000000ffff);
    gdt[2] = UINT64_C(0x00cf92000000ffff);
    gdt[3] = UINT64_C(0x00cff2000000ffff);
    gdt[4] = UINT64_C(0x00affa000000ffff);

    tss.rsp0 = (ios_u64)(ios_uptr)__bootstrap_stack_top;
    tss.ist1 = (ios_u64)(ios_uptr)(double_fault_stack + sizeof(double_fault_stack));
    tss.io_map_base = (ios_u16)sizeof(tss);
    set_tss_descriptor((ios_uptr)&tss, (ios_u32)(sizeof(tss) - 1U));

    x86_64_load_gdt(&descriptor);
    x86_64_load_task_register((ios_u16)X86_64_TSS_SELECTOR);
}

void x86_64_gdt_set_kernel_stack(ios_uptr stack_pointer)
{
    IOS_ASSERT(stack_pointer != 0);
    tss.rsp0 = (ios_u64)stack_pointer;
}
