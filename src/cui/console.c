#include <inferenceos/cui.h>

#include <inferenceos/runtime.h>

static void write_error(
    struct ios_cui_console *console, const char *symbol, const char *message
)
{
    console->io.write("error: ", console->io.write_context);
    console->io.write(symbol, console->io.write_context);
    console->io.write(": ", console->io.write_context);
    console->io.write(message, console->io.write_context);
    console->io.write("\n", console->io.write_context);
}

static const char *status_symbol(ios_status status)
{
    switch (status) {
    case IOS_ERROR(IOS_E_INVALID_ARGUMENT): return "invalid_arguments";
    case IOS_ERROR(IOS_E_OUT_OF_RANGE): return "out_of_range";
    case IOS_ERROR(IOS_E_OVERFLOW): return "overflow";
    case IOS_ERROR(IOS_E_NOT_SUPPORTED): return "not_supported";
    case IOS_ERROR(IOS_E_NOT_FOUND): return "not_found";
    case IOS_ERROR(IOS_E_ALREADY_EXISTS): return "already_exists";
    case IOS_ERROR(IOS_E_NO_MEMORY): return "no_memory";
    case IOS_ERROR(IOS_E_NO_SPACE): return "no_space";
    case IOS_ERROR(IOS_E_ACCESS_DENIED): return "access_denied";
    case IOS_ERROR(IOS_E_BUSY): return "busy";
    case IOS_ERROR(IOS_E_WOULD_BLOCK): return "would_block";
    case IOS_ERROR(IOS_E_TIMEOUT): return "timeout";
    case IOS_ERROR(IOS_E_IO): return "io_error";
    case IOS_ERROR(IOS_E_CORRUPT): return "corrupt";
    case IOS_ERROR(IOS_E_READ_ONLY): return "read_only";
    case IOS_ERROR(IOS_E_NOT_EMPTY): return "not_empty";
    case IOS_ERROR(IOS_E_PROTOCOL): return "protocol_error";
    case IOS_ERROR(IOS_E_INVALID_STATE): return "invalid_state";
    case IOS_ERROR(IOS_E_UNEXPECTED_FORMAT): return "unexpected_format";
    default: return "command_failed";
    }
}

static const char *status_message(ios_status status)
{
    switch (status) {
    case IOS_ERROR(IOS_E_INVALID_ARGUMENT): return "check command syntax with help";
    case IOS_ERROR(IOS_E_NOT_SUPPORTED): return "operation is not supported";
    case IOS_ERROR(IOS_E_NOT_FOUND): return "path or object was not found";
    case IOS_ERROR(IOS_E_ALREADY_EXISTS): return "path or object already exists";
    case IOS_ERROR(IOS_E_NO_SPACE): return "filesystem or command capacity is full";
    case IOS_ERROR(IOS_E_ACCESS_DENIED): return "operation is not authorized";
    case IOS_ERROR(IOS_E_BUSY): return "object is currently in use";
    case IOS_ERROR(IOS_E_IO): return "storage operation failed";
    case IOS_ERROR(IOS_E_CORRUPT): return "filesystem metadata is invalid";
    case IOS_ERROR(IOS_E_READ_ONLY): return "filesystem is read-only";
    case IOS_ERROR(IOS_E_NOT_EMPTY): return "directory is not empty";
    case IOS_ERROR(IOS_E_INVALID_STATE): return "required service or mount is unavailable";
    case IOS_ERROR(IOS_E_UNEXPECTED_FORMAT): return "file is not in the expected text format";
    default: return "command rejected";
    }
}

static const struct ios_cui_command *find_command(
    const struct ios_cui_command_registry *registry, const char *name
)
{
    if (registry == NULL || name == NULL) return NULL;
    for (ios_size index = 0; index < registry->command_count; ++index) {
        if (strcmp(registry->commands[index]->name, name) == 0) {
            return registry->commands[index];
        }
    }
    return NULL;
}

static void write_usage(
    struct ios_cui_console *console, const struct ios_cui_command *command
)
{
    if (command == NULL || command->usage == NULL) return;
    console->io.write("usage: ", console->io.write_context);
    console->io.write(command->usage, console->io.write_context);
    console->io.write("\n", console->io.write_context);
}

ios_status ios_cui_console_initialize(
    struct ios_cui_console *console,
    struct ios_cui_command_registry *registry,
    struct ios_cui_io io
)
{
    if (console == NULL || registry == NULL || io.write == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *console = (struct ios_cui_console){ 0 };
    console->registry = registry;
    console->io = io;
    return IOS_OK;
}

void ios_cui_console_prompt(struct ios_cui_console *console)
{
    if (console != NULL && console->io.write != NULL) {
        console->io.write("InferenceOS> ", console->io.write_context);
    }
}

static ios_status execute_line(struct ios_cui_console *console)
{
    struct ios_cui_parsed_line parsed;
    const enum ios_cui_parse_error parse_status = ios_cui_parse_line(console->line, &parsed);
    ios_status status;
    if (parse_status != IOS_CUI_PARSE_OK) {
        write_error(console, ios_cui_parse_error_symbol(parse_status), "command line rejected");
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    status = ios_cui_command_dispatch(console->registry, &parsed, &console->io);
    if (status == IOS_ERROR(IOS_E_NOT_FOUND)) {
        const bool known = find_command(console->registry, parsed.arguments[0]) != NULL;
        write_error(
            console,
            known ? status_symbol(status) : "command_not_found",
            known ? status_message(status) : "unknown command"
        );
    } else if (IOS_FAILED(status)) {
        write_error(console, status_symbol(status), status_message(status));
        if (status == IOS_ERROR(IOS_E_INVALID_ARGUMENT)) {
            write_usage(console, find_command(console->registry, parsed.arguments[0]));
        }
    }
    return status;
}

ios_status ios_cui_console_feed(struct ios_cui_console *console, ios_u32 key_code)
{
    ios_status status = IOS_OK;
    char echoed[2];
    if (console == NULL || console->registry == NULL || console->io.write == NULL) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    if (key_code == '\b') {
        if (console->line_length != 0) {
            --console->line_length;
            console->line[console->line_length] = '\0';
            console->io.write("\b \b", console->io.write_context);
        }
        return IOS_OK;
    }
    if (key_code == '\r' || key_code == '\n') {
        console->io.write("\n", console->io.write_context);
        status = execute_line(console);
        console->line_length = 0;
        *console->line = '\0';
        ios_cui_console_prompt(console);
        return status;
    }
    if (key_code < 0x20 || key_code > 0x7e) {
        write_error(console, "invalid_character", "input rejected");
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (console->line_length == IOS_CUI_MAX_PAYLOAD) {
        write_error(console, "line_too_long", "input rejected");
        return IOS_ERROR(IOS_E_NO_SPACE);
    }
    console->line[console->line_length++] = (char)key_code;
    console->line[console->line_length] = '\0';
    *echoed = (char)key_code;
    echoed[1] = '\0';
    console->io.write(echoed, console->io.write_context);
    return IOS_OK;
}
