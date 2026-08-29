#ifndef INFERENCEOS_SHELL_H
#define INFERENCEOS_SHELL_H

#include <inferenceos/drivers/framebuffer.h>
#include <inferenceos/gui/desktop.h>
#include <inferenceos/gui/terminal.h>

typedef ios_status (*ios_shell_module_start)(ios_u32 role, void *context);
typedef void (*ios_shell_module_stop)(ios_u32 role, void *context);

struct ios_shell_config {
    const struct ios_boot_info *boot_info;
    struct ios_graphics_surface shadow;
    struct ios_graphics_surface terminal_surface;
    const struct ios_psf2_font *font;
    ios_cui_write cui_write;
    void *cui_write_context;
    void *command_context;
    ios_shell_module_start start_module;
    ios_shell_module_stop stop_module;
    void *module_context;
    bool desktop_module_available;
    bool terminal_module_available;
    bool launch_terminal;
};

struct ios_shell_runtime {
    struct ios_cui_command_registry commands;
    struct ios_cui_console standalone_console;
    struct ios_desktop desktop;
    struct ios_terminal terminal;
    struct ios_shell_config config;
    const char *diagnostic;
    bool cui_usable;
    bool gui_running;
    bool desktop_module_started;
    bool terminal_module_started;
};

ios_status ios_shell_bootstrap(
    struct ios_shell_runtime *runtime, const struct ios_shell_config *config
);
ios_status ios_shell_start_gui(struct ios_shell_runtime *runtime);
void ios_shell_stop_gui(struct ios_shell_runtime *runtime, const char *diagnostic);

#endif
