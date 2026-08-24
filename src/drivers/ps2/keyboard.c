#include <inferenceos/drivers/ps2.h>

#include <inferenceos/arch/io.h>
#include <inferenceos/runtime.h>

enum {
    PS2_DATA = 0x60,
    PS2_STATUS = 0x64,
    PS2_COMMAND = 0x64,
    PS2_STATUS_OUTPUT_FULL = 1,
    PS2_STATUS_INPUT_FULL = 2,
    PS2_STATUS_AUX_DATA = 32,
    PS2_ACK = 0xfa,
    PS2_RESEND = 0xfe,
    PS2_TIMEOUT_SPINS = 1000000
};

static ios_status wait_input_clear(void)
{
    for (ios_size spin = 0; spin < PS2_TIMEOUT_SPINS; ++spin) {
        if ((x86_64_port_read8(PS2_STATUS) & PS2_STATUS_INPUT_FULL) == 0) { return IOS_OK; }
    }
    return IOS_ERROR(IOS_E_TIMEOUT);
}

static ios_status wait_output_full(void)
{
    for (ios_size spin = 0; spin < PS2_TIMEOUT_SPINS; ++spin) {
        if ((x86_64_port_read8(PS2_STATUS) & PS2_STATUS_OUTPUT_FULL) != 0) { return IOS_OK; }
    }
    return IOS_ERROR(IOS_E_TIMEOUT);
}

static ios_status write_command(ios_u8 command)
{
    ios_status status = wait_input_clear();
    if (IOS_SUCCEEDED(status)) { x86_64_port_write8(PS2_COMMAND, command); }
    return status;
}

static ios_status write_data(ios_u8 data)
{
    ios_status status = wait_input_clear();
    if (IOS_SUCCEEDED(status)) { x86_64_port_write8(PS2_DATA, data); }
    return status;
}

static ios_status read_data(ios_u8 *data)
{
    ios_status status = wait_output_full();
    if (IOS_SUCCEEDED(status)) { *data = x86_64_port_read8(PS2_DATA); }
    return status;
}

static ios_status keyboard_command(ios_u8 command)
{
    for (ios_size attempt = 0; attempt < 3; ++attempt) {
        ios_u8 response;
        ios_status status = write_data(command);
        if (IOS_FAILED(status)) { return status; }
        status = read_data(&response);
        if (IOS_FAILED(status)) { return status; }
        if (response == PS2_ACK) { return IOS_OK; }
        if (response != PS2_RESEND) { return IOS_ERROR(IOS_E_PROTOCOL); }
    }
    return IOS_ERROR(IOS_E_IO);
}

ios_status ps2_keyboard_hardware_initialize(void)
{
    ios_u8 configuration;
    ios_status status = write_command(UINT8_C(0xad));
    if (IOS_FAILED(status)) { return status; }
    while ((x86_64_port_read8(PS2_STATUS) & PS2_STATUS_OUTPUT_FULL) != 0) {
        (void)x86_64_port_read8(PS2_DATA);
    }
    status = write_command(UINT8_C(0x20));
    if (IOS_SUCCEEDED(status)) { status = read_data(&configuration); }
    if (IOS_FAILED(status)) { return status; }
    configuration &= (ios_u8)~(UINT8_C(1) | UINT8_C(1) << 1 | UINT8_C(1) << 6);
    status = write_command(UINT8_C(0x60));
    if (IOS_SUCCEEDED(status)) { status = write_data(configuration); }
    if (IOS_SUCCEEDED(status)) { status = write_command(UINT8_C(0xaa)); }
    ios_u8 response = 0;
    if (IOS_SUCCEEDED(status)) { status = read_data(&response); }
    if (IOS_FAILED(status) || response != UINT8_C(0x55)) { return IOS_ERROR(IOS_E_NOT_SUPPORTED); }
    status = write_command(UINT8_C(0xab));
    if (IOS_SUCCEEDED(status)) { status = read_data(&response); }
    if (IOS_FAILED(status) || response != 0) { return IOS_ERROR(IOS_E_NOT_SUPPORTED); }
    status = write_command(UINT8_C(0xae));
    if (IOS_SUCCEEDED(status)) { status = keyboard_command(UINT8_C(0xf0)); }
    if (IOS_SUCCEEDED(status)) { status = keyboard_command(UINT8_C(0x02)); }
    if (IOS_SUCCEEDED(status)) { status = keyboard_command(UINT8_C(0xf4)); }
    if (IOS_FAILED(status)) { return status; }
    configuration |= UINT8_C(1);
    status = write_command(UINT8_C(0x60));
    return IOS_SUCCEEDED(status) ? write_data(configuration) : status;
}

void ps2_keyboard_initialize(struct ps2_keyboard *keyboard, struct ios_input_queue *queue)
{
    if (keyboard == NULL) { return; }
    memset(keyboard, 0, sizeof(*keyboard));
    keyboard->queue = queue;
}

static ios_u32 key_for_scan(ios_u8 scan, bool extended)
{
    if (extended) {
        switch (scan) {
        case 0x6b: return IOS_KEY_LEFT; case 0x74: return IOS_KEY_RIGHT;
        case 0x75: return IOS_KEY_UP; case 0x72: return IOS_KEY_DOWN;
        case 0x14: return IOS_KEY_RIGHT_CONTROL; case 0x11: return IOS_KEY_RIGHT_ALT;
        default: return IOS_KEY_NONE;
        }
    }
    switch (scan) {
    case 0x66: return IOS_KEY_BACKSPACE; case 0x5a: return IOS_KEY_ENTER;
    case 0x76: return IOS_KEY_ESCAPE; case 0x29: return IOS_KEY_SPACE;
    case 0x12: return IOS_KEY_LEFT_SHIFT; case 0x59: return IOS_KEY_RIGHT_SHIFT;
    case 0x14: return IOS_KEY_LEFT_CONTROL; case 0x11: return IOS_KEY_LEFT_ALT;
    case 0x58: return IOS_KEY_CAPS_LOCK;
    case 0x1c: return 'a'; case 0x32: return 'b'; case 0x21: return 'c';
    case 0x23: return 'd'; case 0x24: return 'e'; case 0x2b: return 'f';
    case 0x34: return 'g'; case 0x33: return 'h'; case 0x43: return 'i';
    case 0x3b: return 'j'; case 0x42: return 'k'; case 0x4b: return 'l';
    case 0x3a: return 'm'; case 0x31: return 'n'; case 0x44: return 'o';
    case 0x4d: return 'p'; case 0x15: return 'q'; case 0x2d: return 'r';
    case 0x1b: return 's'; case 0x2c: return 't'; case 0x3c: return 'u';
    case 0x2a: return 'v'; case 0x1d: return 'w'; case 0x22: return 'x';
    case 0x35: return 'y'; case 0x1a: return 'z';
    case 0x16: return '1'; case 0x1e: return '2'; case 0x26: return '3';
    case 0x25: return '4'; case 0x2e: return '5'; case 0x36: return '6';
    case 0x3d: return '7'; case 0x3e: return '8'; case 0x46: return '9';
    case 0x45: return '0'; case 0x0e: return '`'; case 0x4e: return '-';
    case 0x55: return '='; case 0x54: return '['; case 0x5b: return ']';
    case 0x5d: return '\\'; case 0x4c: return ';'; case 0x52: return '\'';
    case 0x41: return ','; case 0x49: return '.'; case 0x4a: return '/';
    default: return IOS_KEY_NONE;
    }
}

static ios_u32 shifted_text(ios_u32 key, bool shifted, bool caps_lock)
{
    static const char unshifted[] = "`1234567890-=[]\\;',./";
    static const char shifted_values[] = "~!@#$%^&*()_+{}|:\"<>?";
    if (key >= 'a' && key <= 'z') {
        return shifted != caps_lock ? key - 'a' + 'A' : key;
    }
    for (ios_size index = 0; unshifted[index] != '\0'; ++index) {
        if (key == (ios_u32)unshifted[index]) { return shifted ? (ios_u32)shifted_values[index] : key; }
    }
    return key >= IOS_KEY_SPACE && key < 127 ? key : 0;
}

ios_status ps2_keyboard_handle_byte(
    struct ps2_keyboard *keyboard, ios_u8 byte, ios_u64 timestamp_ticks
) {
    if (keyboard == NULL || keyboard->queue == NULL) { return IOS_ERROR(IOS_E_INVALID_ARGUMENT); }
    if (keyboard->pause_bytes_remaining != 0) {
        --keyboard->pause_bytes_remaining;
        return IOS_OK;
    }
    if (byte == UINT8_C(0xe0)) { keyboard->extended_prefix = true; return IOS_OK; }
    if (byte == UINT8_C(0xf0)) { keyboard->break_prefix = true; return IOS_OK; }
    if (byte == UINT8_C(0xe1)) {
        keyboard->extended_prefix = false; keyboard->break_prefix = false;
        keyboard->pause_bytes_remaining = 7;
        return IOS_OK;
    }
    const bool released = keyboard->break_prefix;
    const bool extended = keyboard->extended_prefix;
    const ios_u32 key = key_for_scan(byte, extended);
    const ios_size key_index = (extended ? 256U : 0U) + byte;
    keyboard->break_prefix = false; keyboard->extended_prefix = false;
    if (key == IOS_KEY_NONE) { return IOS_ERROR(IOS_E_NOT_SUPPORTED); }
    const bool repeat = !released && keyboard->key_down[key_index];
    keyboard->key_down[key_index] = !released;
    if (key == IOS_KEY_LEFT_SHIFT) { keyboard->left_shift = !released; }
    if (key == IOS_KEY_RIGHT_SHIFT) { keyboard->right_shift = !released; }
    if (key == IOS_KEY_LEFT_CONTROL) { keyboard->left_control = !released; }
    if (key == IOS_KEY_RIGHT_CONTROL) { keyboard->right_control = !released; }
    if (key == IOS_KEY_LEFT_ALT) { keyboard->left_alt = !released; }
    if (key == IOS_KEY_RIGHT_ALT) { keyboard->right_alt = !released; }
    if (key == IOS_KEY_CAPS_LOCK && !released && !repeat) { keyboard->caps_lock = !keyboard->caps_lock; }
    ios_u32 flags = released ? 0 : IOS_INPUT_PRESSED;
    if (repeat) { flags |= IOS_INPUT_REPEAT; }
    if (keyboard->left_shift || keyboard->right_shift) { flags |= IOS_INPUT_SHIFT; }
    if (keyboard->left_control || keyboard->right_control) { flags |= IOS_INPUT_CONTROL; }
    if (keyboard->left_alt || keyboard->right_alt) { flags |= IOS_INPUT_ALT; }
    if (keyboard->caps_lock) { flags |= IOS_INPUT_CAPS_LOCK; }
    return input_emit_key(keyboard->queue, timestamp_ticks, key,
        released ? 0 : shifted_text(key, (flags & IOS_INPUT_SHIFT) != 0, keyboard->caps_lock), flags);
}

ios_status ps2_keyboard_interrupt(struct ps2_keyboard *keyboard, ios_u64 timestamp_ticks)
{
    const ios_u8 status = x86_64_port_read8(PS2_STATUS);
    if ((status & PS2_STATUS_OUTPUT_FULL) == 0 || (status & PS2_STATUS_AUX_DATA) != 0) {
        return IOS_ERROR(IOS_E_WOULD_BLOCK);
    }
    return ps2_keyboard_handle_byte(keyboard, x86_64_port_read8(PS2_DATA), timestamp_ticks);
}
