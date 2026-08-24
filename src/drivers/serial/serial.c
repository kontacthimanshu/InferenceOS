#include <inferenceos/arch/io.h>
#include <inferenceos/drivers/serial.h>

enum {
    COM1_BASE = 0x3f8,
    COM_DATA = 0,
    COM_INTERRUPT_ENABLE = 1,
    COM_FIFO_CONTROL = 2,
    COM_LINE_CONTROL = 3,
    COM_MODEM_CONTROL = 4,
    COM_LINE_STATUS = 5,
    COM_TRANSMIT_READY = 0x20,
    COM_POLL_LIMIT = 100000
};

static bool serial_ready;

static void write_register(ios_u16 offset, ios_u8 value)
{
    x86_64_port_write8((ios_u16)(COM1_BASE + offset), value);
}

static ios_u8 read_register(ios_u16 offset)
{
    return x86_64_port_read8((ios_u16)(COM1_BASE + offset));
}

bool serial_initialize(void)
{
    serial_ready = false;

    write_register(COM_INTERRUPT_ENABLE, 0x00);
    write_register(COM_LINE_CONTROL, 0x80);
    write_register(COM_DATA, 0x01);
    write_register(COM_INTERRUPT_ENABLE, 0x00);
    write_register(COM_LINE_CONTROL, 0x03);
    write_register(COM_FIFO_CONTROL, 0xc7);

    write_register(COM_MODEM_CONTROL, 0x1e);
    write_register(COM_DATA, 0xae);
    if (read_register(COM_DATA) != 0xaeU) {
        write_register(COM_MODEM_CONTROL, 0x00);
        return false;
    }

    write_register(COM_MODEM_CONTROL, 0x0b);
    serial_ready = true;
    serial_write_line("INFERENCEOS:EARLY_SERIAL_READY");
    return true;
}

bool serial_is_ready(void)
{
    return serial_ready;
}

bool serial_try_write_character(char character)
{
    if (!serial_ready) {
        return false;
    }

    for (ios_u32 attempt = 0; attempt < COM_POLL_LIMIT; ++attempt) {
        if ((read_register(COM_LINE_STATUS) & COM_TRANSMIT_READY) != 0U) {
            write_register(COM_DATA, (ios_u8)character);
            return true;
        }
    }

    serial_ready = false;
    return false;
}

void serial_write(const char *text)
{
    if (text == NULL) {
        text = "<null>";
    }

    while (*text != '\0') {
        if (*text == '\n') {
            (void)serial_try_write_character('\r');
        }
        (void)serial_try_write_character(*text);
        ++text;
    }
}

void serial_write_line(const char *text)
{
    serial_write(text);
    serial_write("\n");
}

void serial_write_hex_u64(ios_u64 value)
{
    static const char digits[] = "0123456789ABCDEF";

    serial_write("0x");
    for (ios_u32 shift = 64; shift != 0; shift -= 4) {
        const ios_u32 digit = (ios_u32)((value >> (shift - 4)) & UINT64_C(0x0f));
        (void)serial_try_write_character(digits[digit]);
    }
}

void serial_write_decimal_u64(ios_u64 value)
{
    char buffer[20];
    char *cursor = buffer + sizeof(buffer);

    do {
        const ios_u64 quotient = value / UINT64_C(10);
        const ios_u64 remainder = value - (quotient * UINT64_C(10));

        --cursor;
        *cursor = (char)('0' + (char)remainder);
        value = quotient;
    } while (value != 0);

    while (cursor != buffer + sizeof(buffer)) {
        (void)serial_try_write_character(*cursor);
        ++cursor;
    }
}
