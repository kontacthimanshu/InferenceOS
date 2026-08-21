#include <inferenceos/arch/x86_64/io.h>
#include <inferenceos/serial.h>

#define COM1_DATA_PORT (INFERENCEOS_SERIAL_COM1_PORT + 0U)
#define COM1_INTERRUPT_ENABLE_PORT (INFERENCEOS_SERIAL_COM1_PORT + 1U)
#define COM1_FIFO_CONTROL_PORT (INFERENCEOS_SERIAL_COM1_PORT + 2U)
#define COM1_LINE_CONTROL_PORT (INFERENCEOS_SERIAL_COM1_PORT + 3U)
#define COM1_MODEM_CONTROL_PORT (INFERENCEOS_SERIAL_COM1_PORT + 4U)
#define COM1_LINE_STATUS_PORT (INFERENCEOS_SERIAL_COM1_PORT + 5U)

#define COM1_LINE_STATUS_TRANSMITTER_EMPTY 0x20U

static bool serial_initialized;
static bool serial_available;

static bool wait_for_transmitter(void)
{
    for (inferenceos_u32 poll = 0U;
         poll < INFERENCEOS_SERIAL_POLL_LIMIT;
         ++poll) {
        const inferenceos_u8 status = inferenceos_arch_in8(
            COM1_LINE_STATUS_PORT
        );
        if (status == UINT8_C(0xFF)) {
            return false;
        }
        if ((status & COM1_LINE_STATUS_TRANSMITTER_EMPTY) != 0U) {
            return true;
        }
    }
    return false;
}

inferenceos_result inferenceos_serial_initialize(void)
{
    if (serial_initialized) {
        return serial_available
            ? INFERENCEOS_RESULT_OK
            : INFERENCEOS_RESULT_NOT_READY;
    }

    /* Disable interrupts, select divisor latch, choose divisor 3 (38400 baud),
     * then configure 8 data bits, no parity, one stop bit and a 14-byte FIFO. */
    inferenceos_arch_out8(COM1_INTERRUPT_ENABLE_PORT, 0x00U);
    inferenceos_arch_out8(COM1_LINE_CONTROL_PORT, 0x80U);
    inferenceos_arch_out8(COM1_DATA_PORT, 0x03U);
    inferenceos_arch_out8(COM1_INTERRUPT_ENABLE_PORT, 0x00U);
    inferenceos_arch_out8(COM1_LINE_CONTROL_PORT, 0x03U);
    inferenceos_arch_out8(COM1_FIFO_CONTROL_PORT, 0xC7U);
    inferenceos_arch_out8(COM1_MODEM_CONTROL_PORT, 0x0BU);

    serial_initialized = true;
    serial_available = wait_for_transmitter();
    return serial_available
        ? INFERENCEOS_RESULT_OK
        : INFERENCEOS_RESULT_NOT_READY;
}

bool inferenceos_serial_is_available(void)
{
    return serial_initialized && serial_available;
}

inferenceos_result inferenceos_serial_write_byte(inferenceos_u8 byte)
{
    inferenceos_result result;

    if (!serial_initialized) {
        result = inferenceos_serial_initialize();
        if (!inferenceos_result_is_success(result)) {
            return result;
        }
    }
    if (!serial_available) {
        return INFERENCEOS_RESULT_NOT_READY;
    }
    if (!wait_for_transmitter()) {
        serial_available = false;
        return INFERENCEOS_RESULT_TIMEOUT;
    }
    inferenceos_arch_out8(COM1_DATA_PORT, byte);
    return INFERENCEOS_RESULT_OK;
}

inferenceos_result inferenceos_serial_write(
    const char *data,
    inferenceos_size length
)
{
    if (data == NULL && length != 0U) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    for (inferenceos_size index = 0U; index < length; ++index) {
        inferenceos_result result = inferenceos_serial_write_byte(
            (inferenceos_u8)data[index]
        );
        if (!inferenceos_result_is_success(result)) {
            return result;
        }
    }
    return INFERENCEOS_RESULT_OK;
}

inferenceos_result inferenceos_serial_write_bounded_string(
    const char *text,
    inferenceos_size capacity
)
{
    if (text == NULL) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    for (inferenceos_size index = 0U; index < capacity; ++index) {
        inferenceos_result result;

        if (text[index] == '\0') {
            return INFERENCEOS_RESULT_OK;
        }
        result = inferenceos_serial_write_byte((inferenceos_u8)text[index]);
        if (!inferenceos_result_is_success(result)) {
            return result;
        }
    }
    return INFERENCEOS_RESULT_OUT_OF_RANGE;
}
