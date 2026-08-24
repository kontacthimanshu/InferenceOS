#include <inferenceos/gui/window.h>

static bool rectangles_intersect(
    struct ios_graphics_rect left, struct ios_graphics_rect right
)
{
    return (ios_i64)left.x < (ios_i64)right.x + right.width
        && (ios_i64)right.x < (ios_i64)left.x + left.width
        && (ios_i64)left.y < (ios_i64)right.y + right.height
        && (ios_i64)right.y < (ios_i64)left.y + left.height;
}

static void copy_region(
    struct ios_graphics_surface *target,
    const struct ios_graphics_surface *source,
    struct ios_graphics_rect rectangle
)
{
    for (ios_i32 y = rectangle.y; y < rectangle.y + rectangle.height; ++y) {
        for (ios_i32 x = rectangle.x; x < rectangle.x + rectangle.width; ++x) {
            target->pixels[(ios_size)y * target->stride + (ios_size)x]
                = source->pixels[(ios_size)y * source->stride + (ios_size)x];
        }
    }
}

static void composite_window(
    struct ios_graphics_surface *target,
    const struct ios_window *window,
    struct ios_graphics_rect dirty
)
{
    ios_i64 left = window->bounds.x > dirty.x ? window->bounds.x : dirty.x;
    ios_i64 top = window->bounds.y > dirty.y ? window->bounds.y : dirty.y;
    ios_i64 window_right = (ios_i64)window->bounds.x + window->bounds.width;
    ios_i64 window_bottom = (ios_i64)window->bounds.y + window->bounds.height;
    ios_i64 dirty_right = (ios_i64)dirty.x + dirty.width;
    ios_i64 dirty_bottom = (ios_i64)dirty.y + dirty.height;
    ios_i64 right = window_right < dirty_right ? window_right : dirty_right;
    ios_i64 bottom = window_bottom < dirty_bottom ? window_bottom : dirty_bottom;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > target->width) right = target->width;
    if (bottom > target->height) bottom = target->height;
    if (left >= right || top >= bottom) return;
    for (ios_i64 y = top; y < bottom; ++y) {
        for (ios_i64 x = left; x < right; ++x) {
            const ios_size source_x = (ios_size)(x - window->bounds.x);
            const ios_size source_y = (ios_size)(y - window->bounds.y);
            target->pixels[(ios_size)y * target->stride + (ios_size)x]
                = window->surface.pixels[source_y * window->surface.stride + source_x];
        }
    }
}

ios_status ios_window_compose(struct ios_window_manager *manager)
{
    struct ios_graphics_rect dirty;
    if (manager == NULL || !ios_graphics_surface_is_valid(&manager->framebuffer)
        || !ios_graphics_surface_is_valid(&manager->shadow)) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    if (!manager->dirty_valid) return IOS_OK;
    dirty = manager->dirty;
    ios_graphics_fill_rect(&manager->shadow, dirty, manager->desktop_color);
    for (ios_u16 z = 1; z <= manager->window_count; ++z) {
        for (ios_size index = 0; index < IOS_WINDOW_CAPACITY; ++index) {
            const struct ios_window *window = &manager->windows[index];
            if (window->allocated && window->visible && window->z_order == z
                && rectangles_intersect(window->bounds, dirty)) {
                composite_window(&manager->shadow, window, dirty);
            }
        }
    }
    copy_region(&manager->framebuffer, &manager->shadow, dirty);
    if (manager->pointer_visible) {
        const struct ios_graphics_rect pointer_bounds = {
            manager->pointer_x, manager->pointer_y,
            IOS_WINDOW_POINTER_WIDTH, IOS_WINDOW_POINTER_HEIGHT
        };
        if (rectangles_intersect(pointer_bounds, dirty)) {
            ios_graphics_draw_pointer(
                &manager->framebuffer,
                manager->pointer_x,
                manager->pointer_y,
                manager->pointer_color
            );
        }
    }
    manager->dirty_valid = false;
    manager->dirty = (struct ios_graphics_rect){ 0 };
    return IOS_OK;
}
