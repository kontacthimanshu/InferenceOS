#include <inferenceos/gui/window.h>

static struct ios_window *find_window(
    struct ios_window_manager *manager, struct ios_window_handle handle
)
{
    struct ios_window *window;
    if (manager == NULL || handle.slot >= IOS_WINDOW_CAPACITY || handle.generation == 0) {
        return NULL;
    }
    window = &manager->windows[handle.slot];
    if (!window->allocated || window->generation != handle.generation) return NULL;
    return window;
}

static struct ios_graphics_rect screen_rect(const struct ios_window_manager *manager)
{
    return (struct ios_graphics_rect){
        0, 0, (ios_i32)manager->framebuffer.width, (ios_i32)manager->framebuffer.height
    };
}

static bool clip_to_screen(
    const struct ios_window_manager *manager, struct ios_graphics_rect *rectangle
)
{
    ios_i64 left = rectangle->x;
    ios_i64 top = rectangle->y;
    ios_i64 right = left + rectangle->width;
    ios_i64 bottom = top + rectangle->height;
    if (rectangle->width <= 0 || rectangle->height <= 0) return false;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > manager->framebuffer.width) right = manager->framebuffer.width;
    if (bottom > manager->framebuffer.height) bottom = manager->framebuffer.height;
    if (left >= right || top >= bottom) return false;
    rectangle->x = (ios_i32)left;
    rectangle->y = (ios_i32)top;
    rectangle->width = (ios_i32)(right - left);
    rectangle->height = (ios_i32)(bottom - top);
    return true;
}

static void add_dirty(
    struct ios_window_manager *manager, struct ios_graphics_rect rectangle
)
{
    ios_i64 right;
    ios_i64 bottom;
    ios_i64 dirty_right;
    ios_i64 dirty_bottom;
    ios_i64 left;
    ios_i64 top;
    if (manager == NULL || !clip_to_screen(manager, &rectangle)) return;
    if (!manager->dirty_valid) {
        manager->dirty = rectangle;
        manager->dirty_valid = true;
        return;
    }
    right = (ios_i64)rectangle.x + rectangle.width;
    bottom = (ios_i64)rectangle.y + rectangle.height;
    dirty_right = (ios_i64)manager->dirty.x + manager->dirty.width;
    dirty_bottom = (ios_i64)manager->dirty.y + manager->dirty.height;
    left = rectangle.x < manager->dirty.x ? rectangle.x : manager->dirty.x;
    top = rectangle.y < manager->dirty.y ? rectangle.y : manager->dirty.y;
    if (dirty_right > right) right = dirty_right;
    if (dirty_bottom > bottom) bottom = dirty_bottom;
    manager->dirty = (struct ios_graphics_rect){
        (ios_i32)left, (ios_i32)top, (ios_i32)(right - left), (ios_i32)(bottom - top)
    };
}

static void invalidate_pointer(struct ios_window_manager *manager)
{
    add_dirty(manager, (struct ios_graphics_rect){
        manager->pointer_x, manager->pointer_y,
        IOS_WINDOW_POINTER_WIDTH, IOS_WINDOW_POINTER_HEIGHT
    });
}

static void select_top_focus(struct ios_window_manager *manager)
{
    struct ios_window *top = NULL;
    for (ios_size index = 0; index < IOS_WINDOW_CAPACITY; ++index) {
        struct ios_window *window = &manager->windows[index];
        window->focused = false;
        if (window->allocated && window->visible
            && (top == NULL || window->z_order > top->z_order)) {
            top = window;
        }
    }
    if (top != NULL) top->focused = true;
}

ios_status ios_window_manager_initialize(
    struct ios_window_manager *manager,
    struct ios_graphics_surface framebuffer,
    struct ios_graphics_surface shadow,
    ios_u32 desktop_color,
    ios_u32 pointer_color
)
{
    if (manager == NULL || !ios_graphics_surface_is_valid(&framebuffer)
        || !ios_graphics_surface_is_valid(&shadow)
        || shadow.width != framebuffer.width || shadow.height != framebuffer.height
        || framebuffer.width > INT32_MAX || framebuffer.height > INT32_MAX) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *manager = (struct ios_window_manager){ 0 };
    manager->framebuffer = framebuffer;
    manager->shadow = shadow;
    manager->desktop_color = desktop_color;
    manager->pointer_color = pointer_color;
    add_dirty(manager, screen_rect(manager));
    return IOS_OK;
}

ios_status ios_window_create(
    struct ios_window_manager *manager,
    struct ios_graphics_surface client_surface,
    ios_i32 x,
    ios_i32 y,
    ios_u32 owner,
    struct ios_window_handle *handle
)
{
    ios_size free_slot = IOS_WINDOW_CAPACITY;
    ios_u16 highest_z = 0;
    if (manager == NULL || handle == NULL || !ios_graphics_surface_is_valid(&client_surface)
        || client_surface.width > INT32_MAX || client_surface.height > INT32_MAX) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    for (ios_size index = 0; index < IOS_WINDOW_CAPACITY; ++index) {
        if (!manager->windows[index].allocated && free_slot == IOS_WINDOW_CAPACITY) free_slot = index;
        if (manager->windows[index].allocated && manager->windows[index].z_order > highest_z) {
            highest_z = manager->windows[index].z_order;
        }
    }
    if (free_slot == IOS_WINDOW_CAPACITY) return IOS_ERROR(IOS_E_NO_SPACE);
    struct ios_window *window = &manager->windows[free_slot];
    ++window->generation;
    if (window->generation == 0) ++window->generation;
    window->surface = client_surface;
    window->bounds = (struct ios_graphics_rect){
        x, y, (ios_i32)client_surface.width, (ios_i32)client_surface.height
    };
    window->owner = owner;
    window->z_order = (ios_u16)(highest_z + 1);
    window->allocated = true;
    window->visible = true;
    ++manager->window_count;
    select_top_focus(manager);
    add_dirty(manager, window->bounds);
    *handle = (struct ios_window_handle){ (ios_u16)free_slot, window->generation };
    return IOS_OK;
}

ios_status ios_window_destroy(
    struct ios_window_manager *manager, struct ios_window_handle handle
)
{
    struct ios_window *window = find_window(manager, handle);
    ios_u16 removed_z;
    if (window == NULL) return IOS_ERROR(IOS_E_BAD_HANDLE);
    removed_z = window->z_order;
    if (window->visible) add_dirty(manager, window->bounds);
    window->allocated = false;
    window->visible = false;
    window->focused = false;
    --manager->window_count;
    for (ios_size index = 0; index < IOS_WINDOW_CAPACITY; ++index) {
        if (manager->windows[index].allocated
            && manager->windows[index].z_order > removed_z) {
            --manager->windows[index].z_order;
        }
    }
    select_top_focus(manager);
    return IOS_OK;
}

ios_status ios_window_move(
    struct ios_window_manager *manager,
    struct ios_window_handle handle,
    ios_i32 x,
    ios_i32 y
)
{
    struct ios_window *window = find_window(manager, handle);
    if (window == NULL) return IOS_ERROR(IOS_E_BAD_HANDLE);
    if (window->visible) add_dirty(manager, window->bounds);
    window->bounds.x = x;
    window->bounds.y = y;
    if (window->visible) add_dirty(manager, window->bounds);
    return IOS_OK;
}

ios_status ios_window_set_visible(
    struct ios_window_manager *manager, struct ios_window_handle handle, bool visible
)
{
    struct ios_window *window = find_window(manager, handle);
    if (window == NULL) return IOS_ERROR(IOS_E_BAD_HANDLE);
    if (window->visible != visible) {
        add_dirty(manager, window->bounds);
        window->visible = visible;
        select_top_focus(manager);
    }
    return IOS_OK;
}

ios_status ios_window_raise(
    struct ios_window_manager *manager, struct ios_window_handle handle
)
{
    struct ios_window *window = find_window(manager, handle);
    ios_u16 top_z = 0;
    if (window == NULL) return IOS_ERROR(IOS_E_BAD_HANDLE);
    for (ios_size index = 0; index < IOS_WINDOW_CAPACITY; ++index) {
        if (manager->windows[index].allocated && manager->windows[index].z_order > top_z) {
            top_z = manager->windows[index].z_order;
        }
    }
    if (window->z_order != top_z) {
        for (ios_size index = 0; index < IOS_WINDOW_CAPACITY; ++index) {
            struct ios_window *other = &manager->windows[index];
            if (other->allocated && other->z_order > window->z_order) --other->z_order;
        }
        window->z_order = top_z;
        add_dirty(manager, window->bounds);
    }
    select_top_focus(manager);
    return IOS_OK;
}

ios_status ios_window_invalidate(
    struct ios_window_manager *manager,
    struct ios_window_handle handle,
    struct ios_graphics_rect client_rectangle
)
{
    struct ios_window *window = find_window(manager, handle);
    ios_i64 right;
    ios_i64 bottom;
    ios_i64 translated_x;
    ios_i64 translated_y;
    if (window == NULL) return IOS_ERROR(IOS_E_BAD_HANDLE);
    if (client_rectangle.width <= 0 || client_rectangle.height <= 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    right = (ios_i64)client_rectangle.x + client_rectangle.width;
    bottom = (ios_i64)client_rectangle.y + client_rectangle.height;
    if (client_rectangle.x < 0) client_rectangle.x = 0;
    if (client_rectangle.y < 0) client_rectangle.y = 0;
    if (right > window->surface.width) right = window->surface.width;
    if (bottom > window->surface.height) bottom = window->surface.height;
    if (client_rectangle.x >= right || client_rectangle.y >= bottom) return IOS_OK;
    client_rectangle.width = (ios_i32)(right - client_rectangle.x);
    client_rectangle.height = (ios_i32)(bottom - client_rectangle.y);
    translated_x = (ios_i64)client_rectangle.x + window->bounds.x;
    translated_y = (ios_i64)client_rectangle.y + window->bounds.y;
    if (translated_x > INT32_MAX) translated_x = INT32_MAX;
    if (translated_x < INT32_MIN) translated_x = INT32_MIN;
    if (translated_y > INT32_MAX) translated_y = INT32_MAX;
    if (translated_y < INT32_MIN) translated_y = INT32_MIN;
    client_rectangle.x = (ios_i32)translated_x;
    client_rectangle.y = (ios_i32)translated_y;
    if (window->visible) add_dirty(manager, client_rectangle);
    return IOS_OK;
}

ios_status ios_window_render_owned(
    struct ios_window_manager *manager,
    struct ios_window_handle handle,
    ios_u32 owner
)
{
    struct ios_window *window = find_window(manager, handle);
    ios_status status;
    if (window == NULL) return IOS_ERROR(IOS_E_BAD_HANDLE);
    if (owner == 0 || window->owner != owner) return IOS_ERROR(IOS_E_ACCESS_DENIED);
    status = ios_window_invalidate(
        manager, handle,
        (struct ios_graphics_rect){
            0, 0, (ios_i32)window->surface.width, (ios_i32)window->surface.height
        }
    );
    return IOS_FAILED(status) ? status : ios_window_compose(manager);
}

void ios_window_invalidate_desktop(
    struct ios_window_manager *manager, struct ios_graphics_rect rectangle
)
{
    add_dirty(manager, rectangle);
}

void ios_window_set_pointer(
    struct ios_window_manager *manager, ios_i32 x, ios_i32 y, bool visible
)
{
    if (manager == NULL) return;
    if (manager->pointer_visible) invalidate_pointer(manager);
    manager->pointer_x = x;
    manager->pointer_y = y;
    manager->pointer_visible = visible;
    if (manager->pointer_visible) invalidate_pointer(manager);
}
