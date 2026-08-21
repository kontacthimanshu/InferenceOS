#include <inferenceos/panic.h>
#include <inferenceos/serial.h>
#include <inferenceos/arch/x86_64/cpu.h>

static void panic_write(const char *text, inferenceos_size capacity)
{
    (void)inferenceos_serial_write_bounded_string(text, capacity);
}

static void panic_write_u32(inferenceos_u32 value)
{
    char digits[10];
    inferenceos_size length = 0U;

    do {
        digits[length] = (char)('0' + value % 10U);
        value /= 10U;
        ++length;
    } while (value != 0U && length < INFERENCEOS_ARRAY_COUNT(digits));

    while (length > 0U) {
        --length;
        (void)inferenceos_serial_write_byte((inferenceos_u8)digits[length]);
    }
}

static INFERENCEOS_NORETURN void panic_stop(void)
{
    inferenceos_arch_halt();
}

INFERENCEOS_NORETURN void inferenceos_panic_bounded(
    const char *message,
    inferenceos_size message_length
)
{
    const inferenceos_size bounded_length =
        message_length < INFERENCEOS_PANIC_MESSAGE_LIMIT
            ? message_length
            : INFERENCEOS_PANIC_MESSAGE_LIMIT;

    (void)inferenceos_serial_initialize();
    panic_write("PANIC: ", sizeof("PANIC: "));
    if (message != NULL) {
        (void)inferenceos_serial_write(message, bounded_length);
    } else {
        panic_write("(no message)", sizeof("(no message)"));
    }
    panic_write("\r\n", sizeof("\r\n"));
    panic_stop();
}

INFERENCEOS_NORETURN void inferenceos_panic(const char *message)
{
    inferenceos_size length = 0U;

    if (message != NULL) {
        while (length < INFERENCEOS_PANIC_MESSAGE_LIMIT
            && message[length] != '\0') {
            ++length;
        }
    }
    inferenceos_panic_bounded(message, length);
}

INFERENCEOS_NORETURN void inferenceos_assert_fail(
    const char *expression,
    const char *file,
    inferenceos_u32 line
)
{
    (void)inferenceos_serial_initialize();
    panic_write("PANIC: assertion failed: ", sizeof("PANIC: assertion failed: "));
    if (expression != NULL) {
        panic_write(expression, INFERENCEOS_PANIC_EXPRESSION_LIMIT);
    } else {
        panic_write("(unknown)", sizeof("(unknown)"));
    }
    panic_write(" at ", sizeof(" at "));
    if (file != NULL) {
        panic_write(file, INFERENCEOS_PANIC_FILE_LIMIT);
    } else {
        panic_write("(unknown)", sizeof("(unknown)"));
    }
    (void)inferenceos_serial_write_byte((inferenceos_u8)':');
    panic_write_u32(line);
    panic_write("\r\n", sizeof("\r\n"));
    panic_stop();
}
