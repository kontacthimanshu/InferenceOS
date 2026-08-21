#include <inferenceos/console.h>
#include <inferenceos/serial.h>

static bool console_initialized;

inferenceos_result inferenceos_console_initialize(
    const inferenceos_framebuffer_config *config
)
{
    const inferenceos_result serial_result = inferenceos_serial_initialize();
    const inferenceos_result framebuffer_result = config != NULL
        ? inferenceos_framebuffer_initialize(config)
        : INFERENCEOS_RESULT_NOT_READY;

    console_initialized = inferenceos_result_is_success(serial_result)
        || inferenceos_result_is_success(framebuffer_result);
    if (console_initialized) {
        return INFERENCEOS_RESULT_OK;
    }
    return serial_result != INFERENCEOS_RESULT_NOT_READY
        ? serial_result
        : framebuffer_result;
}

bool inferenceos_console_is_available(void)
{
    return console_initialized
        && (inferenceos_serial_is_available()
            || inferenceos_framebuffer_is_available());
}

inferenceos_result inferenceos_console_write_byte(inferenceos_u8 byte)
{
    const inferenceos_result serial_result =
        inferenceos_serial_write_byte(byte);
    const inferenceos_result framebuffer_result =
        inferenceos_framebuffer_write_byte(byte);

    if (inferenceos_result_is_success(serial_result)
        || inferenceos_result_is_success(framebuffer_result)) {
        return INFERENCEOS_RESULT_OK;
    }
    return serial_result != INFERENCEOS_RESULT_NOT_READY
        ? serial_result
        : framebuffer_result;
}

inferenceos_result inferenceos_console_write(
    const char *data,
    inferenceos_size length
)
{
    if (data == NULL && length != 0U) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    for (inferenceos_size index = 0U; index < length; ++index) {
        const inferenceos_result result = inferenceos_console_write_byte(
            (inferenceos_u8)data[index]
        );
        if (!inferenceos_result_is_success(result)) {
            return result;
        }
    }
    return INFERENCEOS_RESULT_OK;
}

inferenceos_result inferenceos_console_write_bounded_string(
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
        result = inferenceos_console_write_byte((inferenceos_u8)text[index]);
        if (!inferenceos_result_is_success(result)) {
            return result;
        }
    }
    return INFERENCEOS_RESULT_OUT_OF_RANGE;
}

inferenceos_result inferenceos_console_clear(void)
{
    static const char serial_clear[] = "\x1B[2J\x1B[H";
    const inferenceos_result serial_result = inferenceos_serial_write(
        serial_clear, sizeof(serial_clear) - 1U
    );
    const inferenceos_result framebuffer_result = inferenceos_framebuffer_clear();

    if (inferenceos_result_is_success(serial_result)
        || inferenceos_result_is_success(framebuffer_result)) {
        return INFERENCEOS_RESULT_OK;
    }
    return serial_result != INFERENCEOS_RESULT_NOT_READY
        ? serial_result
        : framebuffer_result;
}
