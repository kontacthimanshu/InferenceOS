#include <inferenceos/gui/desktop.h>

enum {
    DESKTOP_COLOR = UINT32_C(0x001b365d),
    POINTER_COLOR = UINT32_C(0x00ffffff)
};

ios_status ios_desktop_start(
    struct ios_desktop *desktop,
    struct ios_graphics_surface framebuffer,
    struct ios_graphics_surface shadow
)
{
    ios_status status;
    if (desktop == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    *desktop = (struct ios_desktop){ 0 };
    status = ios_window_manager_initialize(
        &desktop->window_manager, framebuffer, shadow, DESKTOP_COLOR, POINTER_COLOR
    );
    if (IOS_FAILED(status)) return status;
    desktop->active = true;
    ios_window_set_pointer(&desktop->window_manager, 0, 0, true);
    return ios_desktop_repaint(desktop);
}

ios_status ios_desktop_repaint(struct ios_desktop *desktop)
{
    if (desktop == NULL || !desktop->active) return IOS_ERROR(IOS_E_INVALID_STATE);
    return ios_window_compose(&desktop->window_manager);
}

void ios_desktop_stop(struct ios_desktop *desktop)
{
    if (desktop == NULL || !desktop->active) return;
    ios_window_set_pointer(&desktop->window_manager, 0, 0, false);
    (void)ios_window_compose(&desktop->window_manager);
    desktop->active = false;
}
