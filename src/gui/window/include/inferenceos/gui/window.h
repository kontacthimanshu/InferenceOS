#ifndef INFERENCEOS_GUI_WINDOW_H
#define INFERENCEOS_GUI_WINDOW_H

#include <inferenceos/gui/graphics.h>

enum {
    IOS_WINDOW_CAPACITY = 16,
    IOS_WINDOW_POINTER_WIDTH = 8,
    IOS_WINDOW_POINTER_HEIGHT = 12
};

struct ios_window_handle {
    ios_u16 slot;
    ios_u16 generation;
};

struct ios_window {
    struct ios_graphics_surface surface;
    struct ios_graphics_rect bounds;
    ios_u32 owner;
    ios_u16 generation;
    ios_u16 z_order;
    bool allocated;
    bool visible;
    bool focused;
};

struct ios_window_manager {
    struct ios_graphics_surface framebuffer;
    struct ios_graphics_surface shadow;
    struct ios_window windows[IOS_WINDOW_CAPACITY];
    struct ios_graphics_rect dirty;
    ios_u32 desktop_color;
    ios_u32 pointer_color;
    ios_i32 pointer_x;
    ios_i32 pointer_y;
    ios_u16 window_count;
    bool dirty_valid;
    bool pointer_visible;
};

ios_status ios_window_manager_initialize(
    struct ios_window_manager *manager,
    struct ios_graphics_surface framebuffer,
    struct ios_graphics_surface shadow,
    ios_u32 desktop_color,
    ios_u32 pointer_color
);
ios_status ios_window_create(
    struct ios_window_manager *manager,
    struct ios_graphics_surface client_surface,
    ios_i32 x,
    ios_i32 y,
    ios_u32 owner,
    struct ios_window_handle *handle
);
ios_status ios_window_destroy(
    struct ios_window_manager *manager, struct ios_window_handle handle
);
ios_status ios_window_move(
    struct ios_window_manager *manager,
    struct ios_window_handle handle,
    ios_i32 x,
    ios_i32 y
);
ios_status ios_window_set_visible(
    struct ios_window_manager *manager, struct ios_window_handle handle, bool visible
);
ios_status ios_window_raise(
    struct ios_window_manager *manager, struct ios_window_handle handle
);
ios_status ios_window_invalidate(
    struct ios_window_manager *manager,
    struct ios_window_handle handle,
    struct ios_graphics_rect client_rectangle
);
ios_status ios_window_render_owned(
    struct ios_window_manager *manager,
    struct ios_window_handle handle,
    ios_u32 owner
);
void ios_window_invalidate_desktop(
    struct ios_window_manager *manager, struct ios_graphics_rect rectangle
);
void ios_window_set_pointer(
    struct ios_window_manager *manager, ios_i32 x, ios_i32 y, bool visible
);
ios_status ios_window_compose(struct ios_window_manager *manager);

#endif
