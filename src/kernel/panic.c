#include <inferenceos/panic.h>

#include <inferenceos/drivers/serial.h>

static bool panic_active;

static const char *const exception_names[X86_64_EXCEPTION_VECTOR_COUNT] = {
    "divide error",
    "debug",
    "non-maskable interrupt",
    "breakpoint",
    "overflow",
    "bound range exceeded",
    "invalid opcode",
    "device not available",
    "double fault",
    "coprocessor segment overrun",
    "invalid TSS",
    "segment not present",
    "stack-segment fault",
    "general protection fault",
    "page fault",
    "reserved",
    "x87 floating-point exception",
    "alignment check",
    "machine check",
    "SIMD floating-point exception",
    "virtualization exception",
    "control-protection exception",
    "reserved",
    "reserved",
    "reserved",
    "reserved",
    "reserved",
    "reserved",
    "hypervisor injection exception",
    "VMM communication exception",
    "security exception",
    "reserved"
};

static void write_hex_field(const char *name, ios_u64 value)
{
    serial_write(name);
    serial_write_hex_u64(value);
    serial_write("\n");
}

static _Noreturn void finish_panic(void)
{
    serial_write_line("INFERENCEOS:PANIC_HALT");
    x86_64_halt_forever();
}

static bool begin_panic(void)
{
    x86_64_interrupt_disable();
    if (panic_active) {
        return false;
    }

    panic_active = true;
    serial_write_line("INFERENCEOS:PANIC");
    return true;
}

static void panic_interrupt_handler(struct x86_64_interrupt_frame *frame)
{
    ios_panic_interrupt(frame);
}

void ios_panic_initialize(void)
{
    for (ios_u8 vector = 0; vector < X86_64_EXCEPTION_VECTOR_COUNT; ++vector) {
        x86_64_interrupt_set_handler(vector, panic_interrupt_handler);
    }
}

_Noreturn void ios_panic(const char *message)
{
    if (!begin_panic()) {
        x86_64_halt_forever();
    }

    serial_write("message: ");
    serial_write_line(message);
    finish_panic();
}

_Noreturn void ios_panic_interrupt(const struct x86_64_interrupt_frame *frame)
{
    if (!begin_panic()) {
        x86_64_halt_forever();
    }

    serial_write("exception: ");
    if (frame != NULL && frame->vector < X86_64_EXCEPTION_VECTOR_COUNT) {
        serial_write(exception_names[frame->vector]);
    } else {
        serial_write("invalid interrupt frame");
    }
    serial_write("\n");

    if (frame != NULL) {
        write_hex_field("vector: ", frame->vector);
        write_hex_field("error: ", frame->error_code);
        write_hex_field("rip: ", frame->rip);
        write_hex_field("cs: ", frame->cs);
        write_hex_field("rflags: ", frame->rflags);
        write_hex_field("rax: ", frame->rax);
        write_hex_field("rbx: ", frame->rbx);
        write_hex_field("rcx: ", frame->rcx);
        write_hex_field("rdx: ", frame->rdx);
        write_hex_field("rsi: ", frame->rsi);
        write_hex_field("rdi: ", frame->rdi);
        write_hex_field("rbp: ", frame->rbp);
        write_hex_field("r8: ", frame->r8);
        write_hex_field("r9: ", frame->r9);
        write_hex_field("r10: ", frame->r10);
        write_hex_field("r11: ", frame->r11);
        write_hex_field("r12: ", frame->r12);
        write_hex_field("r13: ", frame->r13);
        write_hex_field("r14: ", frame->r14);
        write_hex_field("r15: ", frame->r15);
        if (x86_64_interrupt_from_user(frame)) {
            write_hex_field("user_rsp: ", x86_64_interrupt_user_stack(frame));
        }
    }

    finish_panic();
}

_Noreturn void ios_assertion_failed(
    const char *expression,
    const char *source_file,
    ios_u32 source_line
)
{
    if (!begin_panic()) {
        x86_64_halt_forever();
    }

    serial_write_line("reason: assertion failed");
    serial_write("expression: ");
    serial_write_line(expression);
    serial_write("file: ");
    serial_write_line(source_file);
    serial_write("line: ");
    serial_write_decimal_u64(source_line);
    serial_write("\n");
    finish_panic();
}
