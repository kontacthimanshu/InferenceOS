#ifndef INFERENCEOS_GUI_TERMINAL_H
#define INFERENCEOS_GUI_TERMINAL_H

#include <inferenceos/cui.h>
#include <inferenceos/gui/input.h>
#include <inferenceos/gui/window.h>

struct ios_terminal {
    struct ios_window_manager *window_manager;
    struct ios_window_handle window;
    struct ios_graphics_surface surface;
    const struct ios_psf2_font *font;
    struct ios_cui_console console;
    ios_u32 foreground;
    ios_u32 background;
    ios_i32 cursor_column;
    ios_i32 cursor_row;
    bool active;
};

ios_status ios_terminal_start(
    struct ios_terminal *terminal,
    struct ios_window_manager *window_manager,
    struct ios_graphics_surface surface,
    const struct ios_psf2_font *font,
    struct ios_cui_command_registry *registry,
    void *command_context,
    ios_i32 x,
    ios_i32 y
);
ios_status ios_terminal_feed_event(
    struct ios_terminal *terminal, const struct ios_input_event *event
);
void ios_terminal_stop(struct ios_terminal *terminal);

#endif
