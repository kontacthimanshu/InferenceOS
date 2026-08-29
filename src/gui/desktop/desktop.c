#include <inferenceos/gui/desktop.h>

enum {
    DESKTOP_COLOR = UINT32_C(0x001b365d),
    POINTER_COLOR = UINT32_C(0x00ffffff),
    CLOSE_BACKGROUND_COLOR = UINT32_C(0x00b3261e),
    CLOSE_FOREGROUND_COLOR = UINT32_C(0x00ffffff),
    CLOSE_BORDER_COLOR = UINT32_C(0x0068100c)
};

static void render_close_button(struct ios_desktop *desktop)
{
    struct ios_graphics_surface surface = {
        desktop->close_button_pixels,
        IOS_DESKTOP_CLOSE_BUTTON_SIZE,
        IOS_DESKTOP_CLOSE_BUTTON_SIZE,
        IOS_DESKTOP_CLOSE_BUTTON_SIZE
    };
    const ios_i32 last = IOS_DESKTOP_CLOSE_BUTTON_SIZE - 1;

    ios_graphics_fill_rect(
        &surface,
        (struct ios_graphics_rect){
            0, 0, IOS_DESKTOP_CLOSE_BUTTON_SIZE, IOS_DESKTOP_CLOSE_BUTTON_SIZE
        },
        CLOSE_BACKGROUND_COLOR
    );
    ios_graphics_draw_border(
        &surface,
        (struct ios_graphics_rect){
            0, 0, IOS_DESKTOP_CLOSE_BUTTON_SIZE, IOS_DESKTOP_CLOSE_BUTTON_SIZE
        },
        CLOSE_BORDER_COLOR
    );
    ios_graphics_draw_line(&surface, 6, 6, last - 6, last - 6, CLOSE_FOREGROUND_COLOR);
    ios_graphics_draw_line(&surface, last - 6, 6, 6, last - 6, CLOSE_FOREGROUND_COLOR);
}

ios_status ios_desktop_start(
    struct ios_desktop *desktop,
    struct ios_graphics_surface framebuffer,
    struct ios_graphics_surface shadow
)
{
    struct ios_graphics_surface close_surface;
    ios_i32 close_x;
    ios_status status;
    if (desktop == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    *desktop = (struct ios_desktop){ 0 };
    status = ios_window_manager_initialize(
        &desktop->window_manager, framebuffer, shadow, DESKTOP_COLOR, POINTER_COLOR
    );
    if (IOS_FAILED(status)) return status;
    render_close_button(desktop);
    close_x = (ios_i32)framebuffer.width
        - IOS_DESKTOP_CLOSE_BUTTON_MARGIN - IOS_DESKTOP_CLOSE_BUTTON_SIZE;
    if (close_x < 0) close_x = 0;
    desktop->close_button_bounds = (struct ios_graphics_rect){
        close_x,
        framebuffer.height > IOS_DESKTOP_CLOSE_BUTTON_MARGIN
            ? IOS_DESKTOP_CLOSE_BUTTON_MARGIN : 0,
        IOS_DESKTOP_CLOSE_BUTTON_SIZE,
        IOS_DESKTOP_CLOSE_BUTTON_SIZE
    };
    close_surface = (struct ios_graphics_surface){
        desktop->close_button_pixels,
        IOS_DESKTOP_CLOSE_BUTTON_SIZE,
        IOS_DESKTOP_CLOSE_BUTTON_SIZE,
        IOS_DESKTOP_CLOSE_BUTTON_SIZE
    };
    status = ios_window_create(
        &desktop->window_manager,
        close_surface,
        desktop->close_button_bounds.x,
        desktop->close_button_bounds.y,
        UINT32_MAX,
        &desktop->close_button
    );
    if (IOS_FAILED(status)) return status;
    desktop->active = true;
    ios_window_set_pointer(&desktop->window_manager, 0, 0, true);
    return ios_desktop_repaint(desktop);
}

ios_status ios_desktop_handle_input(
    struct ios_desktop *desktop,
    const struct ios_input_event *event,
    bool *close_requested
)
{
    const struct ios_graphics_rect *bounds;

    if (desktop == NULL || event == NULL || close_requested == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *close_requested = false;
    if (!desktop->active) return IOS_ERROR(IOS_E_INVALID_STATE);
    if (event->structure_size != sizeof(*event)
        || event->structure_version != IOS_INPUT_EVENT_VERSION) {
        return IOS_ERROR(IOS_E_UNSUPPORTED_VERSION);
    }
    if (event->type == IOS_INPUT_EVENT_KEY
        && event->code == IOS_KEY_ESCAPE
        && (event->flags & IOS_INPUT_PRESSED) != 0) {
        *close_requested = true;
        return IOS_OK;
    }
    if (event->type != IOS_INPUT_EVENT_POINTER_BUTTON
        || event->code != IOS_POINTER_BUTTON_LEFT
        || (event->flags & IOS_INPUT_PRESSED) == 0) return IOS_OK;

    bounds = &desktop->close_button_bounds;
    *close_requested = event->x >= bounds->x && event->y >= bounds->y
        && (ios_i64)event->x < (ios_i64)bounds->x + bounds->width
        && (ios_i64)event->y < (ios_i64)bounds->y + bounds->height;
    return IOS_OK;
}

ios_status ios_desktop_repaint(struct ios_desktop *desktop)
{
    if (desktop == NULL || !desktop->active) return IOS_ERROR(IOS_E_INVALID_STATE);
    return ios_window_compose(&desktop->window_manager);
}

void ios_desktop_stop(struct ios_desktop *desktop)
{
    if (desktop == NULL || !desktop->active) return;
    (void)ios_window_destroy(&desktop->window_manager, desktop->close_button);
    ios_window_set_pointer(&desktop->window_manager, 0, 0, false);
    (void)ios_window_compose(&desktop->window_manager);
    desktop->active = false;
}
