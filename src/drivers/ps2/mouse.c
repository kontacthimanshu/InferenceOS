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

static ios_status controller_command(ios_u8 command)
{
    ios_status status = wait_input_clear();
    if (IOS_SUCCEEDED(status)) { x86_64_port_write8(PS2_COMMAND, command); }
    return status;
}

static ios_status controller_data(ios_u8 data)
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

static ios_status mouse_command(ios_u8 command)
{
    for (ios_size attempt = 0; attempt < 3; ++attempt) {
        ios_u8 response;
        ios_status status = controller_command(UINT8_C(0xd4));
        if (IOS_SUCCEEDED(status)) { status = controller_data(command); }
        if (IOS_SUCCEEDED(status)) { status = read_data(&response); }
        if (IOS_FAILED(status)) { return status; }
        if (response == PS2_ACK) { return IOS_OK; }
        if (response != PS2_RESEND) { return IOS_ERROR(IOS_E_PROTOCOL); }
    }
    return IOS_ERROR(IOS_E_IO);
}

ios_status ps2_mouse_hardware_initialize(void)
{
    ios_u8 response; ios_u8 configuration;
    ios_status status = controller_command(UINT8_C(0xa8));
    if (IOS_SUCCEEDED(status)) { status = controller_command(UINT8_C(0xa9)); }
    if (IOS_SUCCEEDED(status)) { status = read_data(&response); }
    if (IOS_FAILED(status) || response != 0) { return IOS_ERROR(IOS_E_NOT_SUPPORTED); }
    status = controller_command(UINT8_C(0x20));
    if (IOS_SUCCEEDED(status)) { status = read_data(&configuration); }
    if (IOS_FAILED(status)) { return status; }
    configuration |= UINT8_C(1) << 1;
    status = controller_command(UINT8_C(0x60));
    if (IOS_SUCCEEDED(status)) { status = controller_data(configuration); }
    if (IOS_SUCCEEDED(status)) { status = mouse_command(UINT8_C(0xf6)); }
    if (IOS_SUCCEEDED(status)) { status = mouse_command(UINT8_C(0xf4)); }
    return status;
}

void ps2_mouse_initialize(struct ps2_mouse *mouse, struct ios_input_queue *queue)
{
    if (mouse == NULL) { return; }
    memset(mouse, 0, sizeof(*mouse));
    mouse->queue = queue;
}

static ios_status emit_button_changes(
    struct ps2_mouse *mouse, ios_u8 buttons, ios_u64 timestamp_ticks
) {
    static const ios_u32 button_codes[3] = {
        IOS_POINTER_BUTTON_LEFT, IOS_POINTER_BUTTON_RIGHT, IOS_POINTER_BUTTON_MIDDLE
    };
    ios_status first_failure = IOS_OK;
    const ios_u8 changed = (ios_u8)(mouse->buttons ^ buttons);
    for (ios_size index = 0; index < IOS_ARRAY_COUNT(button_codes); ++index) {
        const ios_u8 mask = (ios_u8)(UINT8_C(1) << index);
        if ((changed & mask) != 0) {
            const ios_status status = input_emit_pointer_button(
                mouse->queue, timestamp_ticks, button_codes[index], (buttons & mask) != 0
            );
            if (IOS_FAILED(status) && IOS_SUCCEEDED(first_failure)) { first_failure = status; }
        }
    }
    mouse->buttons = buttons;
    return first_failure;
}

ios_status ps2_mouse_handle_byte(
    struct ps2_mouse *mouse, ios_u8 byte, ios_u64 timestamp_ticks
) {
    ios_status status = IOS_OK;
    if (mouse == NULL || mouse->queue == NULL) { return IOS_ERROR(IOS_E_INVALID_ARGUMENT); }
    if (mouse->packet_size == 0 && (byte & UINT8_C(0x08)) == 0) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    mouse->packet[mouse->packet_size++] = byte;
    if (mouse->packet_size != IOS_ARRAY_COUNT(mouse->packet)) { return IOS_OK; }
    mouse->packet_size = 0;
    const ios_u8 header = *mouse->packet;
    const ios_u8 buttons = header & UINT8_C(0x07);
    if ((header & UINT8_C(0xc0)) == 0) {
        const ios_i32 delta_x = (ios_i8)mouse->packet[1];
        const ios_i32 delta_y = -(ios_i32)(ios_i8)mouse->packet[2];
        if (delta_x != 0 || delta_y != 0) {
            status = input_emit_pointer_move(mouse->queue, timestamp_ticks, delta_x, delta_y);
        }
    }
    const ios_status button_status = emit_button_changes(mouse, buttons, timestamp_ticks);
    return IOS_FAILED(status) ? status : button_status;
}

ios_status ps2_mouse_interrupt(struct ps2_mouse *mouse, ios_u64 timestamp_ticks)
{
    const ios_u8 status = x86_64_port_read8(PS2_STATUS);
    if ((status & (PS2_STATUS_OUTPUT_FULL | PS2_STATUS_AUX_DATA))
        != (PS2_STATUS_OUTPUT_FULL | PS2_STATUS_AUX_DATA)) {
        return IOS_ERROR(IOS_E_WOULD_BLOCK);
    }
    return ps2_mouse_handle_byte(mouse, x86_64_port_read8(PS2_DATA), timestamp_ticks);
}
