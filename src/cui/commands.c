#include <inferenceos/cui.h>

#include <inferenceos/runtime.h>

static ios_status help_command(
    ios_size argument_count, const char *const *arguments, struct ios_cui_io *io
);
static ios_status version_command(
    ios_size argument_count, const char *const *arguments, struct ios_cui_io *io
);
static ios_status clear_command(
    ios_size argument_count, const char *const *arguments, struct ios_cui_io *io
);

static const struct ios_cui_command help_descriptor = {
    "help", "list available commands", help_command
};
static const struct ios_cui_command version_descriptor = {
    "version", "show the InferenceOS version", version_command
};
static const struct ios_cui_command clear_descriptor = {
    "clear", "clear the console", clear_command
};

void ios_cui_command_registry_initialize(struct ios_cui_command_registry *registry)
{
    if (registry != NULL) *registry = (struct ios_cui_command_registry){ 0 };
}

ios_status ios_cui_command_register(
    struct ios_cui_command_registry *registry, const struct ios_cui_command *command
)
{
    if (registry == NULL || command == NULL || command->name == NULL
        || *command->name == '\0' || command->summary == NULL || command->handler == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    for (ios_size index = 0; index < registry->command_count; ++index) {
        if (strcmp(registry->commands[index]->name, command->name) == 0) {
            return IOS_ERROR(IOS_E_ALREADY_EXISTS);
        }
    }
    if (registry->command_count == IOS_CUI_MAX_COMMANDS) return IOS_ERROR(IOS_E_NO_SPACE);
    registry->commands[registry->command_count++] = command;
    return IOS_OK;
}

ios_status ios_cui_command_dispatch(
    const struct ios_cui_command_registry *registry,
    const struct ios_cui_parsed_line *line,
    struct ios_cui_io *io
)
{
    if (registry == NULL || line == NULL || io == NULL || io->write == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (line->argument_count == 0) return IOS_OK;
    io->registry = registry;
    for (ios_size index = 0; index < registry->command_count; ++index) {
        if (strcmp(registry->commands[index]->name, *line->arguments) == 0) {
            return registry->commands[index]->handler(
                line->argument_count, line->arguments, io
            );
        }
    }
    return IOS_ERROR(IOS_E_NOT_FOUND);
}

ios_status ios_cui_register_core_commands(struct ios_cui_command_registry *registry)
{
    ios_status status = ios_cui_command_register(registry, &help_descriptor);
    if (IOS_FAILED(status)) return status;
    status = ios_cui_command_register(registry, &version_descriptor);
    if (IOS_FAILED(status)) return status;
    return ios_cui_command_register(registry, &clear_descriptor);
}

static ios_status require_no_arguments(ios_size argument_count)
{
    return argument_count == 1 ? IOS_OK : IOS_ERROR(IOS_E_INVALID_ARGUMENT);
}

static ios_status help_command(
    ios_size argument_count, const char *const *arguments, struct ios_cui_io *io
)
{
    const struct ios_cui_command_registry *registry = io->registry;
    (void)arguments;
    if (argument_count != 1 || registry == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    for (ios_size index = 0; index < registry->command_count; ++index) {
        io->write(registry->commands[index]->name, io->write_context);
        io->write(" - ", io->write_context);
        io->write(registry->commands[index]->summary, io->write_context);
        io->write("\n", io->write_context);
    }
    return IOS_OK;
}

static ios_status version_command(
    ios_size argument_count, const char *const *arguments, struct ios_cui_io *io
)
{
    (void)arguments;
    ios_status status = require_no_arguments(argument_count);
    if (IOS_FAILED(status)) return status;
    io->write("InferenceOS 0.1.0\n", io->write_context);
    return IOS_OK;
}

static ios_status clear_command(
    ios_size argument_count, const char *const *arguments, struct ios_cui_io *io
)
{
    (void)arguments;
    ios_status status = require_no_arguments(argument_count);
    if (IOS_FAILED(status)) return status;
    io->write("\x1b[2J\x1b[H", io->write_context);
    return IOS_OK;
}
