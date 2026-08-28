#ifndef INFERENCEOS_CUI_H
#define INFERENCEOS_CUI_H

#include <inferenceos/errors.h>

enum {
    IOS_CUI_MAX_PAYLOAD = 255,
    IOS_CUI_MAX_ARGUMENTS = 32,
    IOS_CUI_MAX_COMMANDS = 32
};

enum ios_cui_parse_error {
    IOS_CUI_PARSE_OK = 0,
    IOS_CUI_PARSE_LINE_TOO_LONG,
    IOS_CUI_PARSE_INVALID_CHARACTER,
    IOS_CUI_PARSE_INVALID_SYNTAX,
    IOS_CUI_PARSE_TOO_MANY_ARGUMENTS
};

struct ios_cui_parsed_line {
    char storage[IOS_CUI_MAX_PAYLOAD + 1];
    const char *arguments[IOS_CUI_MAX_ARGUMENTS];
    ios_size argument_count;
};

typedef void (*ios_cui_write)(const char *text, void *context);

struct ios_cui_command_registry;

struct ios_cui_io {
    ios_cui_write write;
    void *write_context;
    void *command_context;
    const struct ios_cui_command_registry *registry;
    /* Presentation-owner context; distinct from the shared command service context. */
    void *shell_context;
};

typedef ios_status (*ios_cui_command_handler)(
    ios_size argument_count,
    const char *const *arguments,
    struct ios_cui_io *io
);

struct ios_cui_command {
    const char *name;
    const char *summary;
    const char *usage;
    ios_cui_command_handler handler;
};

struct ios_cui_command_registry {
    const struct ios_cui_command *commands[IOS_CUI_MAX_COMMANDS];
    ios_size command_count;
};

struct ios_cui_console {
    struct ios_cui_command_registry *registry;
    struct ios_cui_io io;
    char line[IOS_CUI_MAX_PAYLOAD + 1];
    ios_size line_length;
};

enum ios_cui_parse_error ios_cui_parse_line(
    const char *input, struct ios_cui_parsed_line *parsed
);
const char *ios_cui_parse_error_symbol(enum ios_cui_parse_error error);

void ios_cui_command_registry_initialize(struct ios_cui_command_registry *registry);
ios_status ios_cui_command_register(
    struct ios_cui_command_registry *registry, const struct ios_cui_command *command
);
ios_status ios_cui_command_dispatch(
    const struct ios_cui_command_registry *registry,
    const struct ios_cui_parsed_line *line,
    struct ios_cui_io *io
);
ios_status ios_cui_register_core_commands(struct ios_cui_command_registry *registry);

ios_status ios_cui_console_initialize(
    struct ios_cui_console *console,
    struct ios_cui_command_registry *registry,
    struct ios_cui_io io
);
void ios_cui_console_prompt(struct ios_cui_console *console);
ios_status ios_cui_console_feed(struct ios_cui_console *console, ios_u32 key_code);

#endif
