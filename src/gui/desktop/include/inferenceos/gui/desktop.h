#ifndef INFERENCEOS_GUI_DESKTOP_H
#define INFERENCEOS_GUI_DESKTOP_H

#include <inferenceos/gui/window.h>

struct ios_desktop {
    struct ios_window_manager window_manager;
    bool active;
};

ios_status ios_desktop_start(
    struct ios_desktop *desktop,
    struct ios_graphics_surface framebuffer,
    struct ios_graphics_surface shadow
);
ios_status ios_desktop_repaint(struct ios_desktop *desktop);
void ios_desktop_stop(struct ios_desktop *desktop);

#endif
