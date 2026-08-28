#ifndef INFERENCEOS_GUI_DESKTOP_H
#define INFERENCEOS_GUI_DESKTOP_H

#include <inferenceos/gui/input.h>
#include <inferenceos/gui/window.h>

enum {
    IOS_DESKTOP_CLOSE_BUTTON_SIZE = 24,
    IOS_DESKTOP_CLOSE_BUTTON_MARGIN = 8
};

struct ios_desktop {
    struct ios_window_manager window_manager;
    struct ios_window_handle close_button;
    struct ios_graphics_rect close_button_bounds;
    ios_u32 close_button_pixels[
        IOS_DESKTOP_CLOSE_BUTTON_SIZE * IOS_DESKTOP_CLOSE_BUTTON_SIZE
    ];
    bool active;
};

ios_status ios_desktop_start(
    struct ios_desktop *desktop,
    struct ios_graphics_surface framebuffer,
    struct ios_graphics_surface shadow
);
ios_status ios_desktop_repaint(struct ios_desktop *desktop);
ios_status ios_desktop_handle_input(
    struct ios_desktop *desktop,
    const struct ios_input_event *event,
    bool *close_requested
);
void ios_desktop_stop(struct ios_desktop *desktop);

#endif
