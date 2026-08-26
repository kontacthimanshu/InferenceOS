#include <inferenceos/arch/gdt.h>
#include <inferenceos/arch/interrupts.h>

#include <stddef.h>

enum {
    X86_64_IDT_INTERRUPT_GATE = 0x8e,
    X86_64_IDT_USER_TRAP_GATE = 0xef,
    X86_64_DOUBLE_FAULT_VECTOR = 8,
    X86_64_DOUBLE_FAULT_IST = 1
};

struct IOS_PACKED x86_64_idt_entry {
    ios_u16 offset_low;
    ios_u16 selector;
    ios_u8 ist;
    ios_u8 attributes;
    ios_u16 offset_middle;
    ios_u32 offset_high;
    ios_u32 reserved;
};

struct IOS_PACKED x86_64_descriptor_pointer {
    ios_u16 limit;
    ios_u64 base;
};

IOS_STATIC_ASSERT(sizeof(struct x86_64_idt_entry) == 16,
                  "x86-64 IDT entry must be 16 bytes");
IOS_STATIC_ASSERT(sizeof(struct x86_64_interrupt_frame) == 160,
                  "interrupt frame must match the assembly save order");
IOS_STATIC_ASSERT(offsetof(struct x86_64_interrupt_frame, vector) == 120,
                  "interrupt vector offset must match entry.S");
IOS_STATIC_ASSERT(offsetof(struct x86_64_interrupt_frame, rip) == 136,
                  "interrupt RIP offset must match entry.S");

static struct x86_64_idt_entry idt[X86_64_INTERRUPT_VECTOR_COUNT] IOS_ALIGNED(16);
static x86_64_interrupt_handler handlers[X86_64_INTERRUPT_VECTOR_COUNT];

extern const ios_uptr x86_64_interrupt_stub_table[X86_64_INTERRUPT_VECTOR_COUNT];
extern void x86_64_load_idt(const struct x86_64_descriptor_pointer *descriptor);

static void set_idt_gate(ios_u16 vector, ios_uptr entry, ios_u8 attributes, ios_u8 ist)
{
    struct x86_64_idt_entry *gate = &idt[vector];

    gate->offset_low = (ios_u16)(entry & UINT64_C(0xffff));
    gate->selector = (ios_u16)X86_64_KERNEL_CODE_SELECTOR;
    gate->ist = (ios_u8)(ist & 0x07U);
    gate->attributes = attributes;
    gate->offset_middle = (ios_u16)((entry >> 16) & UINT64_C(0xffff));
    gate->offset_high = (ios_u32)(entry >> 32);
    gate->reserved = 0;
}

void x86_64_interrupts_initialize(void)
{
    const struct x86_64_descriptor_pointer descriptor = {
        .limit = (ios_u16)(sizeof(idt) - 1U),
        .base = (ios_u64)(ios_uptr)idt
    };

    for (ios_u16 vector = 0; vector < X86_64_INTERRUPT_VECTOR_COUNT; ++vector) {
        ios_u8 attributes = X86_64_IDT_INTERRUPT_GATE;
        ios_u8 ist = 0;

        if (vector == 3U || vector == 4U) {
            attributes = X86_64_IDT_USER_TRAP_GATE;
        }
        if (vector == X86_64_DOUBLE_FAULT_VECTOR) {
            ist = X86_64_DOUBLE_FAULT_IST;
        }

        handlers[vector] = NULL;
        set_idt_gate(vector, x86_64_interrupt_stub_table[vector], attributes, ist);
    }

    x86_64_load_idt(&descriptor);
}

void x86_64_interrupt_set_handler(ios_u8 vector, x86_64_interrupt_handler handler)
{
    handlers[vector] = handler;
}

void x86_64_interrupt_dispatch(struct x86_64_interrupt_frame *frame)
{
    x86_64_interrupt_handler handler;

    IOS_ASSERT(frame != NULL);
    IOS_ASSERT(frame->vector < X86_64_INTERRUPT_VECTOR_COUNT);

    handler = handlers[frame->vector];
    if (handler == NULL) {
        x86_64_halt_forever();
    }

    handler(frame);
}

bool x86_64_interrupt_from_user(const struct x86_64_interrupt_frame *frame)
{
    IOS_ASSERT(frame != NULL);
    return (frame->cs & UINT64_C(3)) == UINT64_C(3);
}

ios_uptr x86_64_interrupt_user_stack(const struct x86_64_interrupt_frame *frame)
{
    const ios_u64 *privilege_frame;

    IOS_ASSERT(frame != NULL);
    IOS_ASSERT(x86_64_interrupt_from_user(frame));
    privilege_frame = (const ios_u64 *)(frame + 1);
    return (ios_uptr)*privilege_frame;
}

void x86_64_halt(void)
{
    __asm__ volatile("hlt");
}
