#include <inferenceos/arch/x86_64/io.h>
#include <inferenceos/keyboard.h>

#define I8042_DATA_PORT UINT16_C(0x0060)
#define I8042_STATUS_PORT UINT16_C(0x0064)
#define I8042_STATUS_OUTPUT_FULL UINT8_C(0x01)
#define I8042_STATUS_AUXILIARY UINT8_C(0x20)
#define I8042_STATUS_TIMEOUT UINT8_C(0x40)
#define I8042_STATUS_PARITY UINT8_C(0x80)
#define PS2_RELEASE_MASK UINT8_C(0x80)
#define PS2_EXTENDED_PREFIX UINT8_C(0xE0)
#define PS2_LEFT_SHIFT UINT8_C(0x2A)
#define PS2_RIGHT_SHIFT UINT8_C(0x36)
#define PS2_CAPS_LOCK UINT8_C(0x3A)
#define PS2_BACKSPACE UINT8_C(0x0E)
#define PS2_ENTER UINT8_C(0x1C)

typedef struct keyboard_state {
    bool initialized;
    bool available;
    bool left_shift;
    bool right_shift;
    bool caps_lock;
    bool extended;
} keyboard_state;

static keyboard_state keyboard;

static const char normal_map[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
    [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
    [0x0A] = '9', [0x0B] = '0', [0x0C] = '-', [0x0D] = '=',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
    [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
    [0x18] = 'o', [0x19] = 'p', [0x1A] = '[', [0x1B] = ']',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f',
    [0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
    [0x26] = 'l', [0x27] = ';', [0x28] = '\'', [0x29] = '`',
    [0x2B] = '\\', [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c',
    [0x2F] = 'v', [0x30] = 'b', [0x31] = 'n', [0x32] = 'm',
    [0x33] = ',', [0x34] = '.', [0x35] = '/', [0x39] = ' '
};

static const char shifted_map[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$',
    [0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*',
    [0x0A] = '(', [0x0B] = ')', [0x0C] = '_', [0x0D] = '+',
    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R',
    [0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I',
    [0x18] = 'O', [0x19] = 'P', [0x1A] = '{', [0x1B] = '}',
    [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F',
    [0x22] = 'G', [0x23] = 'H', [0x24] = 'J', [0x25] = 'K',
    [0x26] = 'L', [0x27] = ':', [0x28] = '"', [0x29] = '~',
    [0x2B] = '|', [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C',
    [0x2F] = 'V', [0x30] = 'B', [0x31] = 'N', [0x32] = 'M',
    [0x33] = '<', [0x34] = '>', [0x35] = '?', [0x39] = ' '
};

static bool shift_is_active(void)
{
    return keyboard.left_shift || keyboard.right_shift;
}

static bool decode_scancode(inferenceos_u8 scancode, inferenceos_u8 *character)
{
    const bool released = (scancode & PS2_RELEASE_MASK) != 0U;
    const inferenceos_u8 code = scancode & (inferenceos_u8)~PS2_RELEASE_MASK;
    const bool shifted = shift_is_active();
    char decoded;

    if (scancode == PS2_EXTENDED_PREFIX) {
        keyboard.extended = true;
        return false;
    }
    if (keyboard.extended) {
        keyboard.extended = false;
        return false;
    }
    if (code == PS2_LEFT_SHIFT) {
        keyboard.left_shift = !released;
        return false;
    }
    if (code == PS2_RIGHT_SHIFT) {
        keyboard.right_shift = !released;
        return false;
    }
    if (released) {
        return false;
    }
    if (code == PS2_CAPS_LOCK) {
        keyboard.caps_lock = !keyboard.caps_lock;
        return false;
    }
    if (code == PS2_BACKSPACE) {
        *character = (inferenceos_u8)'\b';
        return true;
    }
    if (code == PS2_ENTER) {
        *character = (inferenceos_u8)'\n';
        return true;
    }

    decoded = shifted ? shifted_map[code] : normal_map[code];
    if (decoded >= 'A' && decoded <= 'Z') {
        if (keyboard.caps_lock) {
            decoded = (char)(decoded - 'A' + 'a');
        }
    } else if (decoded >= 'a' && decoded <= 'z') {
        if (keyboard.caps_lock) {
            decoded = (char)(decoded - 'a' + 'A');
        }
    }
    if (decoded < ' ' || decoded > '~') {
        return false;
    }
    *character = (inferenceos_u8)decoded;
    return true;
}

inferenceos_result inferenceos_ps2_keyboard_initialize(void)
{
    const inferenceos_u8 status = inferenceos_arch_in8(I8042_STATUS_PORT);

    keyboard.left_shift = false;
    keyboard.right_shift = false;
    keyboard.caps_lock = false;
    keyboard.extended = false;
    keyboard.initialized = true;
    keyboard.available = status != UINT8_C(0xFF);
    return keyboard.available
        ? INFERENCEOS_RESULT_OK
        : INFERENCEOS_RESULT_NOT_READY;
}

bool inferenceos_ps2_keyboard_is_available(void)
{
    return keyboard.initialized && keyboard.available;
}

inferenceos_result inferenceos_ps2_keyboard_poll(inferenceos_u8 *character)
{
    inferenceos_u8 status;
    inferenceos_u8 scancode;

    if (character == NULL) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    if (!keyboard.initialized) {
        const inferenceos_result result = inferenceos_ps2_keyboard_initialize();
        if (!inferenceos_result_is_success(result)) {
            return result;
        }
    }
    if (!keyboard.available) {
        return INFERENCEOS_RESULT_NOT_READY;
    }

    status = inferenceos_arch_in8(I8042_STATUS_PORT);
    if (status == UINT8_C(0xFF)) {
        keyboard.available = false;
        return INFERENCEOS_RESULT_NOT_READY;
    }
    if ((status & I8042_STATUS_OUTPUT_FULL) == 0U) {
        return INFERENCEOS_RESULT_NOT_READY;
    }
    scancode = inferenceos_arch_in8(I8042_DATA_PORT);
    if ((status & (I8042_STATUS_TIMEOUT | I8042_STATUS_PARITY)) != 0U) {
        return INFERENCEOS_RESULT_IO_ERROR;
    }
    if ((status & I8042_STATUS_AUXILIARY) != 0U) {
        return INFERENCEOS_RESULT_NOT_READY;
    }
    return decode_scancode(scancode, character)
        ? INFERENCEOS_RESULT_OK
        : INFERENCEOS_RESULT_NOT_READY;
}

inferenceos_result inferenceos_ps2_keyboard_read(inferenceos_u8 *character)
{
    if (character == NULL) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    for (inferenceos_u32 poll = 0U;
         poll < INFERENCEOS_KEYBOARD_POLL_LIMIT;
         ++poll) {
        const inferenceos_result result = inferenceos_ps2_keyboard_poll(character);
        if (result == INFERENCEOS_RESULT_OK) {
            return result;
        }
        if (result != INFERENCEOS_RESULT_NOT_READY) {
            return result;
        }
    }
    return INFERENCEOS_RESULT_TIMEOUT;
}
