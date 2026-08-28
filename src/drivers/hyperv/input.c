#include <inferenceos/drivers/hyperv/input.h>

#include <inferenceos/arch/io.h>
#include <inferenceos/runtime.h>

enum {
    SYNTH_KEYBOARD_PROTOCOL_REQUEST = 1,
    SYNTH_KEYBOARD_PROTOCOL_RESPONSE = 2,
    SYNTH_KEYBOARD_EVENT = 3,
    SYNTH_KEYBOARD_VERSION_1_0 = 0x00010000,
    SYNTH_KEYBOARD_UNICODE = 1 << 0,
    SYNTH_KEYBOARD_BREAK = 1 << 1,
    SYNTH_KEYBOARD_E0 = 1 << 2,
    SYNTH_KEYBOARD_E1 = 1 << 3,

    SYNTH_HID_PROTOCOL_REQUEST = 0,
    SYNTH_HID_PROTOCOL_RESPONSE = 1,
    SYNTH_HID_INITIAL_DEVICE_INFO = 2,
    SYNTH_HID_INITIAL_DEVICE_INFO_ACK = 3,
    SYNTH_HID_INPUT_REPORT = 4,
    SYNTH_HID_VERSION_2_0 = 0x00020000,
    SYNTH_HID_PIPE_MESSAGE_DATA = 1
};

struct IOS_PACKED synth_keyboard_protocol_request {
    ios_u32 type;
    ios_u32 version;
};

struct IOS_PACKED synth_keyboard_protocol_response {
    ios_u32 type;
    ios_u32 status;
};

struct IOS_PACKED synth_keyboard_event {
    ios_u32 type;
    ios_u16 make_code;
    ios_u16 reserved;
    ios_u32 information;
};

IOS_STATIC_ASSERT(sizeof(struct synth_keyboard_protocol_request) == 8,
                  "synthetic keyboard request wire size");
IOS_STATIC_ASSERT(sizeof(struct synth_keyboard_protocol_response) == 8,
                  "synthetic keyboard response wire size");
IOS_STATIC_ASSERT(sizeof(struct synth_keyboard_event) == 12,
                  "synthetic keyboard event wire size");

struct IOS_PACKED synth_hid_header {
    ios_u32 type;
    ios_u32 size;
};

struct IOS_PACKED synth_hid_protocol_request {
    struct synth_hid_header header;
    ios_u32 version;
};

struct IOS_PACKED synth_hid_protocol_response {
    struct synth_hid_header header;
    ios_u32 version;
    ios_u8 approved;
};

struct IOS_PACKED synth_hid_pipe_header {
    ios_u32 type;
    ios_u32 size;
};

struct IOS_PACKED synth_hid_pipe_request {
    struct synth_hid_pipe_header pipe;
    struct synth_hid_protocol_request request;
};

struct IOS_PACKED synth_hid_device_info_acknowledgement {
    struct synth_hid_pipe_header pipe;
    struct synth_hid_header header;
    ios_u8 reserved;
};

IOS_STATIC_ASSERT(sizeof(struct synth_hid_pipe_request) == 20,
                  "SynthHID pipe request wire size");
IOS_STATIC_ASSERT(sizeof(struct synth_hid_protocol_response) == 13,
                  "SynthHID protocol response wire size");

static ios_status channel_receive(
    struct ios_vmbus_channel *channel, void *message, ios_size capacity,
    ios_size *size, ios_u32 spin_limit
)
{
    for (ios_u32 spin = 0; spin < spin_limit; ++spin) {
        ios_u16 packet_type;
        ios_u64 transaction;
        ios_status status = vmbus_channel_read(
            channel, &packet_type, &transaction, message, capacity, size);
        (void)transaction;
        if (status == IOS_ERROR(IOS_E_WOULD_BLOCK)) {
            x86_64_cpu_relax();
            continue;
        }
        if (IOS_FAILED(status)) return status;
        if (packet_type != IOS_HV_PACKET_TYPE_DATA_INBAND
            && packet_type != IOS_HV_PACKET_TYPE_COMPLETION) {
            return IOS_ERROR(IOS_E_PROTOCOL);
        }
        return IOS_OK;
    }
    return IOS_ERROR(IOS_E_TIMEOUT);
}

static ios_u32 keyboard_key(ios_u16 scan, bool extended)
{
    if (extended) {
        switch (scan) {
        case 0x4b: return IOS_KEY_LEFT;
        case 0x4d: return IOS_KEY_RIGHT;
        case 0x48: return IOS_KEY_UP;
        case 0x50: return IOS_KEY_DOWN;
        case 0x1d: return IOS_KEY_RIGHT_CONTROL;
        case 0x38: return IOS_KEY_RIGHT_ALT;
        default: return IOS_KEY_NONE;
        }
    }
    switch (scan) {
    case 0x0e: return IOS_KEY_BACKSPACE; case 0x1c: return IOS_KEY_ENTER;
    case 0x01: return IOS_KEY_ESCAPE; case 0x39: return IOS_KEY_SPACE;
    case 0x2a: return IOS_KEY_LEFT_SHIFT; case 0x36: return IOS_KEY_RIGHT_SHIFT;
    case 0x1d: return IOS_KEY_LEFT_CONTROL; case 0x38: return IOS_KEY_LEFT_ALT;
    case 0x3a: return IOS_KEY_CAPS_LOCK;
    case 0x1e: return 'a'; case 0x30: return 'b'; case 0x2e: return 'c';
    case 0x20: return 'd'; case 0x12: return 'e'; case 0x21: return 'f';
    case 0x22: return 'g'; case 0x23: return 'h'; case 0x17: return 'i';
    case 0x24: return 'j'; case 0x25: return 'k'; case 0x26: return 'l';
    case 0x32: return 'm'; case 0x31: return 'n'; case 0x18: return 'o';
    case 0x19: return 'p'; case 0x10: return 'q'; case 0x13: return 'r';
    case 0x1f: return 's'; case 0x14: return 't'; case 0x16: return 'u';
    case 0x2f: return 'v'; case 0x11: return 'w'; case 0x2d: return 'x';
    case 0x15: return 'y'; case 0x2c: return 'z';
    case 0x02: return '1'; case 0x03: return '2'; case 0x04: return '3';
    case 0x05: return '4'; case 0x06: return '5'; case 0x07: return '6';
    case 0x08: return '7'; case 0x09: return '8'; case 0x0a: return '9';
    case 0x0b: return '0'; case 0x29: return '`'; case 0x0c: return '-';
    case 0x0d: return '='; case 0x1a: return '['; case 0x1b: return ']';
    case 0x2b: return '\\'; case 0x27: return ';'; case 0x28: return '\'';
    case 0x33: return ','; case 0x34: return '.'; case 0x35: return '/';
    default: return IOS_KEY_NONE;
    }
}

static ios_u32 keyboard_text(ios_u32 key, bool shift_active, bool caps_lock)
{
    static const char unshifted[] = "`1234567890-=[]\\;',./";
    static const char shifted_values[] = "~!@#$%^&*()_+{}|:\"<>?";
    if (key >= 'a' && key <= 'z') return shift_active != caps_lock ? key - 'a' + 'A' : key;
    for (ios_size index = 0; unshifted[index] != '\0'; ++index) {
        if (key == (ios_u32)unshifted[index]) {
            return shift_active ? (ios_u32)shifted_values[index] : key;
        }
    }
    return key >= IOS_KEY_SPACE && key < 127 ? key : 0;
}

ios_status hyperv_keyboard_handle_event(
    struct ios_hyperv_keyboard *keyboard, ios_u16 make_code,
    ios_u32 information, ios_u64 timestamp_ticks
)
{
    const bool released = (information & SYNTH_KEYBOARD_BREAK) != 0;
    const bool extended = (information & SYNTH_KEYBOARD_E0) != 0;
    const ios_u32 key = keyboard_key(make_code, extended);
    const ios_size index = (extended ? 256U : 0U) + (make_code & 0xffU);
    bool repeat;
    ios_u32 flags;

    if (keyboard == NULL || keyboard->queue == NULL
        || (information & SYNTH_KEYBOARD_E1) != 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (key == IOS_KEY_NONE) return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    repeat = !released && keyboard->key_down[index];
    keyboard->key_down[index] = !released;
    if (key == IOS_KEY_LEFT_SHIFT || key == IOS_KEY_RIGHT_SHIFT) keyboard->shift = !released;
    if (key == IOS_KEY_LEFT_CONTROL || key == IOS_KEY_RIGHT_CONTROL) keyboard->control = !released;
    if (key == IOS_KEY_LEFT_ALT || key == IOS_KEY_RIGHT_ALT) keyboard->alt = !released;
    if (key == IOS_KEY_CAPS_LOCK && !released && !repeat) keyboard->caps_lock = !keyboard->caps_lock;
    flags = released ? 0 : IOS_INPUT_PRESSED;
    if (repeat) flags |= IOS_INPUT_REPEAT;
    if (keyboard->shift) flags |= IOS_INPUT_SHIFT;
    if (keyboard->control) flags |= IOS_INPUT_CONTROL;
    if (keyboard->alt) flags |= IOS_INPUT_ALT;
    if (keyboard->caps_lock) flags |= IOS_INPUT_CAPS_LOCK;
    return input_emit_key(keyboard->queue, timestamp_ticks, key,
        released ? 0 : keyboard_text(key, keyboard->shift, keyboard->caps_lock), flags);
}

ios_status hyperv_keyboard_initialize(
    struct ios_hyperv_keyboard *keyboard, struct ios_vmbus *bus,
    struct ios_vmbus_channel *channel, struct ios_input_queue *queue, ios_u32 spin_limit
)
{
    struct synth_keyboard_protocol_request request = {
        .type = SYNTH_KEYBOARD_PROTOCOL_REQUEST,
        .version = SYNTH_KEYBOARD_VERSION_1_0
    };
    struct synth_keyboard_protocol_response response;
    ios_size size;
    ios_status status;

    if (keyboard == NULL || bus == NULL || channel == NULL || queue == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    memset(keyboard, 0, sizeof(*keyboard));
    keyboard->bus = bus; keyboard->channel = channel; keyboard->queue = queue;
    status = vmbus_channel_write(bus, channel, IOS_HV_PACKET_TYPE_DATA_INBAND,
        IOS_HV_PACKET_FLAG_COMPLETION_REQUESTED, 1, &request, sizeof(request));
    if (IOS_FAILED(status)) return status;
    status = channel_receive(channel, &response, sizeof(response), &size, spin_limit);
    if (IOS_FAILED(status)) return status;
    if (size < sizeof(response) || response.type != SYNTH_KEYBOARD_PROTOCOL_RESPONSE
        || (response.status & 1U) == 0) return IOS_ERROR(IOS_E_PROTOCOL);
    keyboard->ready = true;
    return IOS_OK;
}

ios_status hyperv_keyboard_poll(
    struct ios_hyperv_keyboard *keyboard, ios_u64 timestamp_ticks
)
{
    /* VMBus rounds in-band payloads to an eight-byte boundary.  A keyboard
     * event is 12 bytes on the wire, so leave room for the host's padding. */
    ios_u8 payload[IOS_HV_MESSAGE_PAYLOAD_SIZE];
    struct synth_keyboard_event *message = (void *)payload;
    ios_u16 type;
    ios_u64 transaction;
    ios_size size;
    ios_status status;

    if (keyboard == NULL || !keyboard->ready) return IOS_ERROR(IOS_E_INVALID_STATE);
    status = vmbus_channel_read(keyboard->channel, &type, &transaction,
        payload, sizeof(payload), &size);
    (void)type; (void)transaction;
    if (IOS_FAILED(status)) return status;
    if (size < sizeof(*message) || message->type != SYNTH_KEYBOARD_EVENT) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    return hyperv_keyboard_handle_event(keyboard, message->make_code,
        message->information, timestamp_ticks);
}

static ios_status emit_mouse_buttons(
    struct ios_hyperv_mouse *mouse, ios_u8 buttons, ios_u64 timestamp_ticks
)
{
    static const ios_u32 codes[3] = {
        IOS_POINTER_BUTTON_LEFT, IOS_POINTER_BUTTON_RIGHT, IOS_POINTER_BUTTON_MIDDLE
    };
    ios_status first = IOS_OK;
    const ios_u8 changed = mouse->buttons ^ buttons;
    for (ios_size index = 0; index < IOS_ARRAY_COUNT(codes); ++index) {
        const ios_u8 mask = (ios_u8)(1U << index);
        if ((changed & mask) != 0) {
            ios_status status = input_emit_pointer_button(
                mouse->queue, timestamp_ticks, codes[index], (buttons & mask) != 0);
            if (IOS_FAILED(status) && IOS_SUCCEEDED(first)) first = status;
        }
    }
    mouse->buttons = buttons;
    return first;
}

ios_status hyperv_mouse_handle_report(
    struct ios_hyperv_mouse *mouse, const ios_u8 *report,
    ios_size report_size, ios_u64 timestamp_ticks
)
{
    ios_u16 raw_x;
    ios_u16 raw_y;
    ios_i32 target_x;
    ios_i32 target_y;
    ios_status move_status = IOS_OK;

    if (mouse == NULL || mouse->queue == NULL || report == NULL || report_size < 5) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    raw_x = (ios_u16)(report[1] | ((ios_u16)report[2] << 8));
    raw_y = (ios_u16)(report[3] | ((ios_u16)report[4] << 8));
    target_x = mouse->queue->pointer_width <= 1 ? 0
        : (ios_i32)(((ios_u64)raw_x * (ios_u32)(mouse->queue->pointer_width - 1)) / 0xffffU);
    target_y = mouse->queue->pointer_height <= 1 ? 0
        : (ios_i32)(((ios_u64)raw_y * (ios_u32)(mouse->queue->pointer_height - 1)) / 0xffffU);
    if (target_x != mouse->queue->pointer_x || target_y != mouse->queue->pointer_y) {
        move_status = input_emit_pointer_move(mouse->queue, timestamp_ticks,
            target_x - mouse->queue->pointer_x, target_y - mouse->queue->pointer_y);
    }
    {
        const ios_status button_status = emit_mouse_buttons(mouse, report[0] & 7U, timestamp_ticks);
        return IOS_FAILED(move_status) ? move_status : button_status;
    }
}

ios_status hyperv_mouse_initialize(
    struct ios_hyperv_mouse *mouse, struct ios_vmbus *bus,
    struct ios_vmbus_channel *channel, struct ios_input_queue *queue, ios_u32 spin_limit
)
{
    struct synth_hid_pipe_request request = {
        .pipe = {SYNTH_HID_PIPE_MESSAGE_DATA, sizeof(struct synth_hid_protocol_request)},
        .request = {
            .header = {SYNTH_HID_PROTOCOL_REQUEST, sizeof(ios_u32)},
            .version = SYNTH_HID_VERSION_2_0
        }
    };
    ios_u8 response[IOS_HV_MESSAGE_PAYLOAD_SIZE];
    struct synth_hid_pipe_header *pipe = (void *)response;
    struct synth_hid_protocol_response *protocol =
        (void *)(response + sizeof(*pipe));
    ios_size size;
    ios_status status;

    if (mouse == NULL || bus == NULL || channel == NULL || queue == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    memset(mouse, 0, sizeof(*mouse));
    mouse->bus = bus; mouse->channel = channel; mouse->queue = queue;
    status = vmbus_channel_write(bus, channel, IOS_HV_PACKET_TYPE_DATA_INBAND,
        IOS_HV_PACKET_FLAG_COMPLETION_REQUESTED, 1, &request, sizeof(request));
    if (IOS_FAILED(status)) return status;
    status = channel_receive(channel, response, sizeof(response), &size, spin_limit);
    if (IOS_FAILED(status)) return status;
    if (size < sizeof(*pipe) + sizeof(*protocol)
        || pipe->type != SYNTH_HID_PIPE_MESSAGE_DATA
        || pipe->size < sizeof(*protocol) || pipe->size > size - sizeof(*pipe)
        || protocol->header.type != SYNTH_HID_PROTOCOL_RESPONSE
        || protocol->header.size < sizeof(protocol->version) + sizeof(protocol->approved)
        || protocol->approved == 0) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    mouse->ready = true;
    return IOS_OK;
}

ios_status hyperv_mouse_poll(struct ios_hyperv_mouse *mouse, ios_u64 timestamp_ticks)
{
    ios_u8 message[IOS_HV_MESSAGE_PAYLOAD_SIZE];
    struct synth_hid_pipe_header *pipe = (void *)message;
    struct synth_hid_header *header = (void *)(message + sizeof(*pipe));
    ios_u16 type;
    ios_u64 transaction;
    ios_size size;
    ios_status status;

    if (mouse == NULL || !mouse->ready) return IOS_ERROR(IOS_E_INVALID_STATE);
    status = vmbus_channel_read(mouse->channel, &type, &transaction,
        message, sizeof(message), &size);
    (void)type; (void)transaction;
    if (IOS_FAILED(status)) return status;
    if (size < sizeof(*pipe) + sizeof(*header)
        || pipe->type != SYNTH_HID_PIPE_MESSAGE_DATA
        || pipe->size < sizeof(*header) || pipe->size > size - sizeof(*pipe)
        || header->size > pipe->size - sizeof(*header)) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    if (header->type == SYNTH_HID_INITIAL_DEVICE_INFO) {
        struct synth_hid_device_info_acknowledgement acknowledgement = {
            .pipe = {
                SYNTH_HID_PIPE_MESSAGE_DATA,
                sizeof(struct synth_hid_header) + sizeof(ios_u8)
            },
            .header = {SYNTH_HID_INITIAL_DEVICE_INFO_ACK, sizeof(ios_u8)},
            .reserved = 0
        };
        return vmbus_channel_write(mouse->bus, mouse->channel,
            IOS_HV_PACKET_TYPE_DATA_INBAND, 0, 2,
            &acknowledgement, sizeof(acknowledgement));
    }
    if (header->type != SYNTH_HID_INPUT_REPORT) return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    return hyperv_mouse_handle_report(mouse, (ios_u8 *)header + sizeof(*header),
        header->size, timestamp_ticks);
}
