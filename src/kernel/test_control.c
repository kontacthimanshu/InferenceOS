#include <inferenceos/test_control.h>

#include <inferenceos/runtime.h>

static bool is_space(char character)
{
    return character == ' ' || character == '\t';
}

static const char *skip_spaces(const char *cursor)
{
    while (is_space(*cursor)) ++cursor;
    return cursor;
}

static bool consume_word(const char **cursor, const char *word)
{
    const char *input = skip_spaces(*cursor);
    while (*word != '\0' && *input == *word) {
        ++input;
        ++word;
    }
    if (*word != '\0' || (*input != '\0' && !is_space(*input))) return false;
    *cursor = input;
    return true;
}

static bool parse_u32(const char **cursor, ios_u32 *value)
{
    const char *input = skip_spaces(*cursor);
    ios_u32 parsed = 0;
    if (*input < '0' || *input > '9') return false;
    do {
        const ios_u32 digit = (ios_u32)(*input - '0');
        if (parsed > (UINT32_MAX - digit) / 10U) return false;
        parsed = parsed * 10U + digit;
        ++input;
    } while (*input >= '0' && *input <= '9');
    if (*input != '\0' && !is_space(*input)) return false;
    *cursor = input;
    *value = parsed;
    return true;
}

static bool copy_token(
    const char **cursor, char *destination, ios_size capacity, bool remainder
)
{
    const char *input = skip_spaces(*cursor);
    ios_size length = 0;
    if (*input == '\0' && !remainder) return false;
    while (*input != '\0' && (remainder || !is_space(*input))) {
        if (length + 1U >= capacity) return false;
        destination[length++] = *input++;
    }
    while (remainder && length != 0 && is_space(destination[length - 1U])) --length;
    destination[length] = '\0';
    *cursor = input;
    return true;
}

static ios_status parse_request(
    const char *line, struct ios_test_control_request *request
)
{
    const char *cursor = line;
    ios_u32 version;
    memset(request, 0, sizeof(*request));
    if (!consume_word(&cursor, "INFERENCEOS_TEST")
        || !parse_u32(&cursor, &version)
        || version != IOS_TEST_CONTROL_PROTOCOL_VERSION
        || !parse_u32(&cursor, &request->sequence)
        || !copy_token(
            &cursor, request->action, sizeof(request->action), false
        )
        || !copy_token(
            &cursor, request->argument, sizeof(request->argument), true
        )) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    return IOS_OK;
}

ios_status ios_test_control_initialize(
    struct ios_test_control *control,
    ios_test_control_read_character read_character,
    void *read_context,
    ios_test_control_dispatch dispatch,
    void *dispatch_context
)
{
    if (control == NULL || read_character == NULL || dispatch == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    memset(control, 0, sizeof(*control));
    control->read_character = read_character;
    control->read_context = read_context;
    control->dispatch = dispatch;
    control->dispatch_context = dispatch_context;
    return IOS_OK;
}

ios_status ios_test_control_poll(
    struct ios_test_control *control, ios_size character_budget
)
{
    ios_status first_error = IOS_OK;
    if (control == NULL || control->read_character == NULL
        || control->dispatch == NULL || character_budget == 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    for (ios_size count = 0; count < character_budget; ++count) {
        struct ios_test_control_request request;
        ios_status status;
        char character;
        if (!control->read_character(control->read_context, &character)) break;
        if (character == '\r') continue;
        if (character != '\n') {
            if (control->discarding_line) continue;
            if (control->line_length == IOS_TEST_CONTROL_LINE_CAPACITY) {
                control->line_length = 0;
                control->discarding_line = true;
                if (IOS_SUCCEEDED(first_error)) first_error = IOS_ERROR(IOS_E_NO_SPACE);
                continue;
            }
            control->line[control->line_length++] = character;
            continue;
        }
        if (control->discarding_line) {
            control->discarding_line = false;
            control->line_length = 0;
            continue;
        }
        control->line[control->line_length] = '\0';
        status = parse_request(control->line, &request);
        control->line_length = 0;
        if (IOS_SUCCEEDED(status)) {
            status = control->dispatch(control->dispatch_context, &request);
        }
        if (IOS_FAILED(status) && IOS_SUCCEEDED(first_error)) first_error = status;
    }
    return first_error;
}
