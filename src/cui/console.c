#include <inferenceos/cui.h>

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
        write_error(console, "command_not_found", "unknown command");
    } else if (IOS_FAILED(status)) {
        write_error(console, "command_failed", "command rejected");
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
