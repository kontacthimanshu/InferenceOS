#include <inferenceos/arch/x86_64/cpu.h>
#include <inferenceos/arch/x86_64/io.h>
#include <inferenceos/serial.h>

#define GDT_KERNEL_CODE_SELECTOR UINT16_C(0x08)
#define GDT_KERNEL_DATA_SELECTOR UINT16_C(0x10)
#define IDT_INTERRUPT_GATE UINT8_C(0x8E)
#define I8042_STATUS_PORT UINT16_C(0x0064)
#define I8042_COMMAND_PORT UINT16_C(0x0064)
#define I8042_INPUT_BUFFER_FULL UINT8_C(0x02)
#define I8042_RESET_PULSE UINT8_C(0xFE)
#define I8042_REBOOT_POLL_LIMIT UINT32_C(100000)

typedef struct INFERENCEOS_PACKED descriptor_table_pointer {
    inferenceos_u16 limit;
    inferenceos_u64 base;
} descriptor_table_pointer;

typedef struct INFERENCEOS_PACKED idt_gate {
    inferenceos_u16 offset_low;
    inferenceos_u16 selector;
    inferenceos_u8 ist;
    inferenceos_u8 attributes;
    inferenceos_u16 offset_middle;
    inferenceos_u32 offset_high;
    inferenceos_u32 reserved;
} idt_gate;

INFERENCEOS_STATIC_ASSERT(sizeof(descriptor_table_pointer) == 10U,
    "x86-64 descriptor pointer layout");
INFERENCEOS_STATIC_ASSERT(sizeof(idt_gate) == 16U, "x86-64 IDT gate layout");

/* Null, 64-bit ring-0 code, and ring-0 data descriptors. */
INFERENCEOS_ALIGNED(16) static const inferenceos_u64 gdt[] = {
    UINT64_C(0x0000000000000000),
    UINT64_C(0x00AF9A000000FFFF),
    UINT64_C(0x00CF92000000FFFF)
};
INFERENCEOS_ALIGNED(16) static idt_gate idt[256];

/* Assembly boundaries: loaders consume a 10-byte descriptor pointer. The
 * stub table contains 32 code addresses and the default stub is used for all
 * remaining vectors. Assembly preserves the exception-frame contract. */
extern void inferenceos_x86_64_load_gdt(const descriptor_table_pointer *pointer);
extern void inferenceos_x86_64_load_idt(const descriptor_table_pointer *pointer);
extern const inferenceos_u64 inferenceos_x86_64_exception_stubs[32];
extern const inferenceos_u64 inferenceos_x86_64_unhandled_stub_address;

static void set_idt_gate(inferenceos_u32 vector, inferenceos_u64 address)
{
    idt_gate *const gate = &idt[vector];

    gate->offset_low = (inferenceos_u16)(address & UINT64_C(0xFFFF));
    gate->selector = GDT_KERNEL_CODE_SELECTOR;
    gate->ist = 0U;
    gate->attributes = IDT_INTERRUPT_GATE;
    gate->offset_middle = (inferenceos_u16)((address >> 16U) & UINT64_C(0xFFFF));
    gate->offset_high = (inferenceos_u32)(address >> 32U);
    gate->reserved = 0U;
}

void inferenceos_arch_cpu_initialize(void)
{
    const descriptor_table_pointer gdt_pointer = {
        (inferenceos_u16)(sizeof(gdt) - 1U),
        (inferenceos_u64)(inferenceos_uptr)gdt
    };
    const descriptor_table_pointer idt_pointer = {
        (inferenceos_u16)(sizeof(idt) - 1U),
        (inferenceos_u64)(inferenceos_uptr)idt
    };

    /* Interrupts remain disabled until a later task installs interrupt-driven
     * devices and a controller policy. T033 installs exception gates only. */
    __asm__ __volatile__("cli" ::: "memory");
    inferenceos_x86_64_load_gdt(&gdt_pointer);
    for (inferenceos_u32 vector = 0U; vector < 256U; ++vector) {
        set_idt_gate(vector, inferenceos_x86_64_unhandled_stub_address);
    }
    for (inferenceos_u32 vector = 0U;
         vector < INFERENCEOS_X86_64_EXCEPTION_COUNT;
         ++vector) {
        set_idt_gate(vector, inferenceos_x86_64_exception_stubs[vector]);
    }
    inferenceos_x86_64_load_idt(&idt_pointer);
}

static void serial_write_literal(const char *text, inferenceos_size capacity)
{
    (void)inferenceos_serial_write_bounded_string(text, capacity);
}

static void serial_write_hex64(inferenceos_u64 value)
{
    static const char digits[] = "0123456789ABCDEF";
    char output[18];

    output[0] = '0';
    output[1] = 'x';
    for (inferenceos_u32 index = 0U; index < 16U; ++index) {
        const inferenceos_u32 shift = (15U - index) * 4U;
        output[index + 2U] = digits[(inferenceos_size)((value >> shift) & 0x0FU)];
    }
    (void)inferenceos_serial_write(output, sizeof(output));
}

INFERENCEOS_NORETURN void inferenceos_arch_exception_dispatch(
    const inferenceos_x86_64_exception_frame *frame
)
{
    (void)inferenceos_serial_initialize();
    serial_write_literal("PANIC: x86-64 exception vector=",
        sizeof("PANIC: x86-64 exception vector="));
    if (frame != NULL) {
        serial_write_hex64(frame->vector);
        serial_write_literal(" error=", sizeof(" error="));
        serial_write_hex64(frame->error_code);
        serial_write_literal(" rip=", sizeof(" rip="));
        serial_write_hex64(frame->instruction_pointer);
    } else {
        serial_write_literal("INVALID_FRAME", sizeof("INVALID_FRAME"));
    }
    serial_write_literal("\r\n", sizeof("\r\n"));
    inferenceos_arch_halt();
}

INFERENCEOS_NORETURN void inferenceos_arch_halt(void)
{
    /* CLI prevents maskable wakeups; HLT stops execution until an NMI/reset.
     * No inputs or outputs. Flags and CPU execution state change, while the
     * memory clobber prevents compiler movement across the terminal boundary. */
    __asm__ __volatile__("cli" ::: "memory");
    for (;;) {
        __asm__ __volatile__("hlt" ::: "memory");
    }
}

INFERENCEOS_NORETURN void inferenceos_arch_reboot(void)
{
    /* The QEMU i440fx profile exposes the legacy i8042 reset command. Wait a
     * bounded time for its input buffer, then request a reset. Port I/O is the
     * observable effect and the wrappers provide the compiler memory barrier. */
    __asm__ __volatile__("cli" ::: "memory");
    for (inferenceos_u32 poll = 0U; poll < I8042_REBOOT_POLL_LIMIT; ++poll) {
        if ((inferenceos_arch_in8(I8042_STATUS_PORT) & I8042_INPUT_BUFFER_FULL) == 0U) {
            inferenceos_arch_out8(I8042_COMMAND_PORT, I8042_RESET_PULSE);
            for (inferenceos_u32 wait = 0U;
                 wait < I8042_REBOOT_POLL_LIMIT;
                 ++wait) {
                __asm__ __volatile__("pause" ::: "memory");
            }
            break;
        }
    }

    /* If the controller is wedged, loading a zero-length IDT and raising an
     * exception deliberately causes the documented x86 triple-fault reset. */
    {
        const descriptor_table_pointer empty_idt = {0U, 0U};
        inferenceos_x86_64_load_idt(&empty_idt);
        __asm__ __volatile__("int3" ::: "memory");
    }
    inferenceos_arch_halt();
}
