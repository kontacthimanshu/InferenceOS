#include <inferenceos/console.h>
#include <inferenceos/keyboard.h>
#include <inferenceos/shell.h>
#include <inferenceos/string.h>

static const inferenceos_shell_command *command_registry[INFERENCEOS_SHELL_MAX_COMMANDS];
static inferenceos_size command_count;
static bool shell_initialized;

static inferenceos_result write_error(const char *message, inferenceos_size capacity)
{
    inferenceos_result result = inferenceos_shell_write_literal(
        "error: ", sizeof("error: ")
    );
    if (!inferenceos_result_is_success(result)) {
        return result;
    }
    result = inferenceos_shell_write_literal(message, capacity);
    if (!inferenceos_result_is_success(result)) {
        return result;
    }
    return inferenceos_shell_write_literal("\r\n", sizeof("\r\n"));
}

inferenceos_result inferenceos_shell_write_literal(
    const char *text,
    inferenceos_size capacity
)
{
    return inferenceos_console_write_bounded_string(text, capacity);
}

static const inferenceos_shell_command *find_command(const char *name)
{
    for (inferenceos_size index = 0U; index < command_count; ++index) {
        bool equal = false;
        const inferenceos_result result = inferenceos_string_equal(
            name, INFERENCEOS_SHELL_LINE_CAPACITY,
            command_registry[index]->name, INFERENCEOS_SHELL_LINE_CAPACITY,
            &equal
        );
        if (inferenceos_result_is_success(result) && equal) {
            return command_registry[index];
        }
    }
    return NULL;
}

inferenceos_result inferenceos_shell_register_command(
    const inferenceos_shell_command *command
)
{
    inferenceos_size name_length;

    if (command == NULL || command->name == NULL || command->usage == NULL
        || command->description == NULL || command->handler == NULL
        || command->minimum_arguments > command->maximum_arguments
        || command->maximum_arguments > INFERENCEOS_SHELL_MAX_ARGUMENTS
        || (command->remainder_argument != INFERENCEOS_SHELL_NO_REMAINDER
            && command->remainder_argument >= command->maximum_arguments)) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    if (!inferenceos_result_is_success(inferenceos_string_length(
            command->name, INFERENCEOS_SHELL_LINE_CAPACITY, &name_length))
        || name_length == 0U) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    for (inferenceos_size index = 0U; index < name_length; ++index) {
        if (!((command->name[index] >= 'a' && command->name[index] <= 'z')
            || (command->name[index] >= '0' && command->name[index] <= '9')
            || command->name[index] == '-')) {
            return INFERENCEOS_RESULT_INVALID_ARGUMENT;
        }
    }
    if (find_command(command->name) != NULL) {
        return INFERENCEOS_RESULT_ALREADY_EXISTS;
    }
    if (command_count >= INFERENCEOS_SHELL_MAX_COMMANDS) {
        return INFERENCEOS_RESULT_NO_SPACE;
    }
    command_registry[command_count] = command;
    ++command_count;
    return INFERENCEOS_RESULT_OK;
}

inferenceos_result inferenceos_shell_initialize(void)
{
    if (shell_initialized) {
        return INFERENCEOS_RESULT_OK;
    }
    command_count = 0U;
    shell_initialized = true;
    return inferenceos_shell_register_core_commands();
}

inferenceos_result inferenceos_shell_print_help(void)
{
    for (inferenceos_size index = 0U; index < command_count; ++index) {
        inferenceos_result result = inferenceos_shell_write_literal(
            command_registry[index]->usage, INFERENCEOS_SHELL_LINE_CAPACITY
        );
        if (!inferenceos_result_is_success(result)) {
            return result;
        }
        result = inferenceos_shell_write_literal(" - ", sizeof(" - "));
        if (!inferenceos_result_is_success(result)) {
            return result;
        }
        result = inferenceos_shell_write_literal(
            command_registry[index]->description,
            INFERENCEOS_SHELL_LINE_CAPACITY
        );
        if (!inferenceos_result_is_success(result)) {
            return result;
        }
        result = inferenceos_shell_write_literal("\r\n", sizeof("\r\n"));
        if (!inferenceos_result_is_success(result)) {
            return result;
        }
    }
    return INFERENCEOS_RESULT_OK;
}

static inferenceos_result reject_quotes(const char *line)
{
    for (inferenceos_size index = 0U;
         index < INFERENCEOS_SHELL_LINE_CAPACITY && line[index] != '\0';
         ++index) {
        if (line[index] == '\'' || line[index] == '"') {
            return INFERENCEOS_RESULT_UNSUPPORTED;
        }
    }
    return INFERENCEOS_RESULT_OK;
}

inferenceos_result inferenceos_shell_execute_line(char *line)
{
    const inferenceos_shell_command *command;
    const char *arguments[INFERENCEOS_SHELL_MAX_ARGUMENTS];
    inferenceos_size argument_count = 0U;
    inferenceos_size line_length;
    char *cursor;
    char *command_name;

    if (line == NULL) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    if (!inferenceos_result_is_success(inferenceos_string_length(
            line, INFERENCEOS_SHELL_LINE_CAPACITY, &line_length))) {
        return write_error("line is not terminated", sizeof("line is not terminated"));
    }
    (void)line_length;
    if (!shell_initialized) {
        const inferenceos_result result = inferenceos_shell_initialize();
        if (!inferenceos_result_is_success(result)) {
            return result;
        }
    }
    if (reject_quotes(line) == INFERENCEOS_RESULT_UNSUPPORTED) {
        return write_error("quoting is unsupported", sizeof("quoting is unsupported"));
    }

    cursor = line;
    while (*cursor == ' ') {
        ++cursor;
    }
    if (*cursor == '\0') {
        return INFERENCEOS_RESULT_OK;
    }
    command_name = cursor;
    while (*cursor != '\0' && *cursor != ' ') {
        ++cursor;
    }
    if (*cursor == ' ') {
        *cursor = '\0';
        ++cursor;
    }
    command = find_command(command_name);
    if (command == NULL) {
        return write_error("unknown command", sizeof("unknown command"));
    }

    for (;;) {
        while (*cursor == ' ') {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }
        if (argument_count >= INFERENCEOS_SHELL_MAX_ARGUMENTS) {
            return write_error("too many arguments", sizeof("too many arguments"));
        }
        arguments[argument_count] = cursor;
        ++argument_count;
        if (command->remainder_argument != INFERENCEOS_SHELL_NO_REMAINDER
            && argument_count > command->remainder_argument) {
            break;
        }
        while (*cursor != '\0' && *cursor != ' ') {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }
        *cursor = '\0';
        ++cursor;
    }

    if (argument_count < command->minimum_arguments
        || argument_count > command->maximum_arguments) {
        inferenceos_result result = write_error(
            "invalid arguments; usage:", sizeof("invalid arguments; usage:")
        );
        if (!inferenceos_result_is_success(result)) {
            return result;
        }
        result = inferenceos_shell_write_literal(
            command->usage, INFERENCEOS_SHELL_LINE_CAPACITY
        );
        if (!inferenceos_result_is_success(result)) {
            return result;
        }
        return inferenceos_shell_write_literal("\r\n", sizeof("\r\n"));
    }
    return command->handler(argument_count, arguments);
}

static inferenceos_result read_line(char line[INFERENCEOS_SHELL_LINE_CAPACITY])
{
    inferenceos_size length = 0U;
    bool overflowed = false;

    for (;;) {
        inferenceos_u8 character;
        const inferenceos_result result = inferenceos_ps2_keyboard_read(&character);
        if (result == INFERENCEOS_RESULT_TIMEOUT
            || result == INFERENCEOS_RESULT_NOT_READY) {
            continue;
        }
        if (!inferenceos_result_is_success(result)) {
            return result;
        }
        if (character == (inferenceos_u8)'\n') {
            (void)inferenceos_console_write("\r\n", 2U);
            if (overflowed) {
                line[0] = '\0';
                return INFERENCEOS_RESULT_NO_SPACE;
            }
            line[length] = '\0';
            return INFERENCEOS_RESULT_OK;
        }
        if (character == (inferenceos_u8)'\b') {
            if (!overflowed && length > 0U) {
                --length;
                line[length] = '\0';
                (void)inferenceos_console_write("\b \b", 3U);
            }
            continue;
        }
        if (character >= 0x20U && character <= 0x7EU) {
            if (length < INFERENCEOS_SHELL_LINE_CAPACITY - 1U && !overflowed) {
                line[length] = (char)character;
                ++length;
                line[length] = '\0';
                (void)inferenceos_console_write_byte(character);
            } else {
                overflowed = true;
            }
        }
    }
}

INFERENCEOS_NORETURN void inferenceos_shell_run(void)
{
    char line[INFERENCEOS_SHELL_LINE_CAPACITY];
    inferenceos_result result;

    result = inferenceos_shell_initialize();
    if (!inferenceos_result_is_success(result)) {
        (void)write_error("shell initialization failed",
            sizeof("shell initialization failed"));
    }
    for (;;) {
        (void)inferenceos_shell_write_literal("InferenceOS>", sizeof("InferenceOS>"));
        result = read_line(line);
        if (result == INFERENCEOS_RESULT_NO_SPACE) {
            (void)write_error("line too long", sizeof("line too long"));
            continue;
        }
        if (!inferenceos_result_is_success(result)) {
            (void)write_error("keyboard input failure",
                sizeof("keyboard input failure"));
            continue;
        }
        (void)inferenceos_shell_execute_line(line);
    }
}
