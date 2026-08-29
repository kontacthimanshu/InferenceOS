#include <inferenceos/shell.h>

static ios_status gui_command(
    ios_size argument_count, const char *const *arguments, struct ios_cui_io *io
)
{
    (void)arguments;
    if (argument_count != 1) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    if (io == NULL || io->shell_context == NULL) return IOS_ERROR(IOS_E_INVALID_STATE);
    return ios_shell_start_gui(io->shell_context);
}

static const struct ios_cui_command gui_descriptor = {
    "gui", "start the graphical desktop", "gui", gui_command
};

ios_status ios_shell_bootstrap(
    struct ios_shell_runtime *runtime, const struct ios_shell_config *config
)
{
    struct ios_cui_io io;
    ios_status status;
    if (runtime == NULL || config == NULL || config->cui_write == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *runtime = (struct ios_shell_runtime){ 0 };
    runtime->config = *config;
    runtime->cui_usable = true;
    runtime->diagnostic = "cui_ready";
    ios_cui_command_registry_initialize(&runtime->commands);
    status = ios_cui_register_core_commands(&runtime->commands);
    if (IOS_FAILED(status)) return status;
    status = ios_cui_command_register(&runtime->commands, &gui_descriptor);
    if (IOS_FAILED(status)) return status;
    io = (struct ios_cui_io){
        .write = config->cui_write,
        .write_context = config->cui_write_context,
        .command_context = config->command_context,
        .registry = NULL,
        .shell_context = runtime
    };
    status = ios_cui_console_initialize(&runtime->standalone_console, &runtime->commands, io);
    if (IOS_FAILED(status)) return status;
    ios_cui_console_prompt(&runtime->standalone_console);
    return IOS_OK;
}

static ios_status start_module(struct ios_shell_runtime *runtime, ios_u32 role)
{
    if (runtime->config.start_module == NULL) return IOS_OK;
    return runtime->config.start_module(role, runtime->config.module_context);
}

ios_status ios_shell_start_gui(struct ios_shell_runtime *runtime)
{
    struct ios_graphics_surface framebuffer;
    ios_status status;
    if (runtime == NULL || !runtime->cui_usable) return IOS_ERROR(IOS_E_INVALID_STATE);
    if (runtime->gui_running) return IOS_ERROR(IOS_E_ALREADY_EXISTS);
    if (!runtime->config.desktop_module_available
        || (runtime->config.launch_terminal
            && !runtime->config.terminal_module_available)) {
        runtime->diagnostic = "gui_unavailable: required module";
        return IOS_ERROR(IOS_E_NOT_FOUND);
    }
    status = ios_gop_framebuffer_open(runtime->config.boot_info, &framebuffer);
    if (IOS_FAILED(status)) {
        runtime->diagnostic = "gui_unavailable: framebuffer";
        return status;
    }
    if (runtime->config.font == NULL || runtime->config.font->glyphs == NULL) {
        runtime->diagnostic = "gui_unavailable: font";
        return IOS_ERROR(IOS_E_NOT_FOUND);
    }
    status = start_module(runtime, IOS_MODULE_ROLE_GUI_DESKTOP);
    if (IOS_FAILED(status)) {
        runtime->diagnostic = "gui_unavailable: desktop module";
        return status;
    }
    runtime->desktop_module_started = true;
    status = ios_desktop_start(&runtime->desktop, framebuffer, runtime->config.shadow);
    if (IOS_FAILED(status)) {
        ios_shell_stop_gui(runtime, "gui_unavailable: desktop");
        return status;
    }
    if (runtime->config.launch_terminal) {
        status = start_module(runtime, IOS_MODULE_ROLE_GUI_TERMINAL);
        if (IOS_FAILED(status)) {
            ios_shell_stop_gui(runtime, "gui_unavailable: terminal module");
            return status;
        }
        runtime->terminal_module_started = true;
        status = ios_terminal_start(
            &runtime->terminal, &runtime->desktop.window_manager,
            runtime->config.terminal_surface, runtime->config.font,
            &runtime->commands, runtime->config.command_context, runtime, 32, 32
        );
        if (IOS_FAILED(status)) {
            ios_shell_stop_gui(runtime, "gui_unavailable: terminal");
            return status;
        }
    }
    runtime->gui_running = true;
    runtime->diagnostic = "gui_ready";
    status = ios_desktop_repaint(&runtime->desktop);
    if (IOS_FAILED(status)) {
        ios_shell_stop_gui(runtime, "gui_unavailable: composition");
        return status;
    }
    return IOS_OK;
}

void ios_shell_stop_gui(struct ios_shell_runtime *runtime, const char *diagnostic)
{
    if (runtime == NULL) return;
    ios_terminal_stop(&runtime->terminal);
    if (runtime->terminal_module_started && runtime->config.stop_module != NULL) {
        runtime->config.stop_module(IOS_MODULE_ROLE_GUI_TERMINAL, runtime->config.module_context);
    }
    runtime->terminal_module_started = false;
    ios_desktop_stop(&runtime->desktop);
    if (runtime->desktop_module_started && runtime->config.stop_module != NULL) {
        runtime->config.stop_module(IOS_MODULE_ROLE_GUI_DESKTOP, runtime->config.module_context);
    }
    runtime->desktop_module_started = false;
    runtime->gui_running = false;
    runtime->cui_usable = true;
    runtime->diagnostic = diagnostic != NULL ? diagnostic : "gui_stopped: cui_ready";
}
