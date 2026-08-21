#include <inferenceos/console.h>
#include <inferenceos/shell.h>

static inferenceos_result command_help(
    inferenceos_size argument_count,
    const char *const arguments[]
)
{
    (void)argument_count;
    (void)arguments;
    return inferenceos_shell_print_help();
}

static inferenceos_result command_version(
    inferenceos_size argument_count,
    const char *const arguments[]
)
{
    (void)argument_count;
    (void)arguments;
    return inferenceos_shell_write_literal(
        "InferenceOS 0.1.0-dev\r\n", sizeof("InferenceOS 0.1.0-dev\r\n")
    );
}

static inferenceos_result command_clear(
    inferenceos_size argument_count,
    const char *const arguments[]
)
{
    (void)argument_count;
    (void)arguments;
    return inferenceos_console_clear();
}

static const inferenceos_shell_command core_commands[] = {
    {
        "help", "help", "list available commands",
        0U, 0U, INFERENCEOS_SHELL_NO_REMAINDER, command_help
    },
    {
        "version", "version", "show kernel build identity",
        0U, 0U, INFERENCEOS_SHELL_NO_REMAINDER, command_version
    },
    {
        "clear", "clear", "clear character displays",
        0U, 0U, INFERENCEOS_SHELL_NO_REMAINDER, command_clear
    }
};

inferenceos_result inferenceos_shell_register_core_commands(void)
{
    for (inferenceos_size index = 0U;
         index < INFERENCEOS_ARRAY_COUNT(core_commands);
         ++index) {
        const inferenceos_result result = inferenceos_shell_register_command(
            &core_commands[index]
        );
        if (!inferenceos_result_is_success(result)) {
            return result;
        }
    }
    return INFERENCEOS_RESULT_OK;
}
