#ifndef INFERENCEOS_SHELL_H
#define INFERENCEOS_SHELL_H

#include <inferenceos/base.h>
#include <inferenceos/result.h>

#define INFERENCEOS_SHELL_LINE_CAPACITY 256U
#define INFERENCEOS_SHELL_MAX_ARGUMENTS 8U
#define INFERENCEOS_SHELL_MAX_COMMANDS 32U
#define INFERENCEOS_SHELL_NO_REMAINDER UINT8_MAX

typedef inferenceos_result (*inferenceos_shell_command_handler)(
    inferenceos_size argument_count,
    const char *const arguments[]
);

typedef struct inferenceos_shell_command {
    const char *name;
    const char *usage;
    const char *description;
    inferenceos_u8 minimum_arguments;
    inferenceos_u8 maximum_arguments;
    /* When not NO_REMAINDER, arguments at this zero-based index and beyond
     * become one unsplit argument. This supports future write/append text. */
    inferenceos_u8 remainder_argument;
    inferenceos_shell_command_handler handler;
} inferenceos_shell_command;

inferenceos_result inferenceos_shell_initialize(void);
inferenceos_result inferenceos_shell_register_command(
    const inferenceos_shell_command *command
);
inferenceos_result inferenceos_shell_execute_line(char *line);
INFERENCEOS_NORETURN void inferenceos_shell_run(void);

inferenceos_result inferenceos_shell_write_literal(
    const char *text,
    inferenceos_size capacity
);
inferenceos_result inferenceos_shell_print_help(void);
inferenceos_result inferenceos_shell_register_core_commands(void);

#endif
