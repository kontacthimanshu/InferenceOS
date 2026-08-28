#include <inferenceos/gui/terminal.h>

static void terminal_write(const char *text, void *context)
{
    struct ios_terminal *terminal = context;
    (void)ios_graphics_text_console_write(&terminal->text_console, text);
    (void)ios_window_invalidate(
        terminal->window_manager, terminal->window,
        (struct ios_graphics_rect){
            0, 0,
            (ios_i32)terminal->text_console.surface.width,
            (ios_i32)terminal->text_console.surface.height
        }
    );
}

ios_status ios_terminal_start(
    struct ios_terminal *terminal,
    struct ios_window_manager *window_manager,
    struct ios_graphics_surface surface,
    const struct ios_psf2_font *font,
    struct ios_cui_command_registry *registry,
    void *command_context,
    void *shell_context,
    ios_i32 x,
    ios_i32 y
)
{
    ios_status status;
    struct ios_cui_io io;
    if (terminal == NULL || window_manager == NULL || registry == NULL
        || font == NULL || font->glyphs == NULL || !ios_graphics_surface_is_valid(&surface)
        || surface.width < font->width || surface.height < font->height
        || surface.width > INT32_MAX || surface.height > INT32_MAX) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *terminal = (struct ios_terminal){ 0 };
    terminal->window_manager = window_manager;
    status = ios_graphics_text_console_initialize(
        &terminal->text_console, surface, font,
        UINT32_C(0x00f0f0f0), UINT32_C(0x00101010)
    );
    if (IOS_FAILED(status)) return status;
    status = ios_window_create(window_manager, surface, x, y, 1, &terminal->window);
    if (IOS_FAILED(status)) return status;
    io = (struct ios_cui_io){
        .write = terminal_write,
        .write_context = terminal,
        .command_context = command_context,
        .registry = NULL,
        .shell_context = shell_context
    };
    status = ios_cui_console_initialize(&terminal->console, registry, io);
    if (IOS_FAILED(status)) {
        (void)ios_window_destroy(window_manager, terminal->window);
        return status;
    }
    terminal->active = true;
    ios_cui_console_prompt(&terminal->console);
    return IOS_OK;
}

ios_status ios_terminal_feed_event(
    struct ios_terminal *terminal, const struct ios_input_event *event
)
{
    ios_u32 key;
    if (terminal == NULL || !terminal->active || event == NULL) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    if (event->structure_size != sizeof(*event)
        || event->structure_version != IOS_INPUT_EVENT_VERSION) {
        return IOS_ERROR(IOS_E_UNSUPPORTED_VERSION);
    }
    if (event->type != IOS_INPUT_EVENT_KEY || (event->flags & IOS_INPUT_PRESSED) == 0) {
        return IOS_OK;
    }
    key = event->text != 0 ? event->text : event->code;
    return ios_cui_console_feed(&terminal->console, key);
}

void ios_terminal_stop(struct ios_terminal *terminal)
{
    if (terminal == NULL || !terminal->active) return;
    (void)ios_window_destroy(terminal->window_manager, terminal->window);
    terminal->active = false;
}
