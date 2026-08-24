#include <inferenceos/gui/graphics.h>

bool ios_graphics_surface_is_valid(const struct ios_graphics_surface *surface)
{
    return surface != NULL && surface->pixels != NULL
        && surface->width != 0 && surface->height != 0
        && surface->stride >= surface->width;
}

void ios_graphics_put_pixel(
    struct ios_graphics_surface *surface, ios_i32 x, ios_i32 y, ios_u32 color
)
{
    if (!ios_graphics_surface_is_valid(surface) || x < 0 || y < 0
        || (ios_u32)x >= surface->width || (ios_u32)y >= surface->height) {
        return;
    }
    surface->pixels[(ios_size)(ios_u32)y * surface->stride + (ios_u32)x] = color;
}

void ios_graphics_fill_rect(
    struct ios_graphics_surface *surface, struct ios_graphics_rect rectangle, ios_u32 color
)
{
    ios_i64 left = rectangle.x;
    ios_i64 top = rectangle.y;
    ios_i64 right = left + rectangle.width;
    ios_i64 bottom = top + rectangle.height;

    if (!ios_graphics_surface_is_valid(surface) || rectangle.width <= 0 || rectangle.height <= 0) {
        return;
    }
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > surface->width) right = surface->width;
    if (bottom > surface->height) bottom = surface->height;
    if (left >= right || top >= bottom) return;

    for (ios_i64 y = top; y < bottom; ++y) {
        for (ios_i64 x = left; x < right; ++x) {
            surface->pixels[(ios_size)y * surface->stride + (ios_size)x] = color;
        }
    }
}

void ios_graphics_draw_line(
    struct ios_graphics_surface *surface,
    ios_i32 x0, ios_i32 y0, ios_i32 x1, ios_i32 y1,
    ios_u32 color
)
{
    ios_i64 x = x0;
    ios_i64 y = y0;
    const ios_i64 target_x = x1;
    const ios_i64 target_y = y1;
    const ios_i64 dx = target_x >= x ? target_x - x : x - target_x;
    const ios_i64 step_x = x < target_x ? 1 : -1;
    const ios_i64 dy_abs = target_y >= y ? target_y - y : y - target_y;
    const ios_i64 step_y = y < target_y ? 1 : -1;
    ios_i64 error = dx - dy_abs;

    if (!ios_graphics_surface_is_valid(surface)) return;
    for (;;) {
        if (x >= 0 && y >= 0 && x < surface->width && y < surface->height) {
            surface->pixels[(ios_size)y * surface->stride + (ios_size)x] = color;
        }
        if (x == target_x && y == target_y) break;
        const ios_i64 doubled_error = error * 2;
        if (doubled_error > -dy_abs) {
            error -= dy_abs;
            x += step_x;
        }
        if (doubled_error < dx) {
            error += dx;
            y += step_y;
        }
    }
}

void ios_graphics_draw_border(
    struct ios_graphics_surface *surface, struct ios_graphics_rect rectangle, ios_u32 color
)
{
    if (rectangle.width <= 0 || rectangle.height <= 0) return;
    ios_graphics_fill_rect(surface, (struct ios_graphics_rect){
        rectangle.x, rectangle.y, rectangle.width, 1
    }, color);
    if (rectangle.height > 1) {
        ios_graphics_fill_rect(surface, (struct ios_graphics_rect){
            rectangle.x, rectangle.y + rectangle.height - 1, rectangle.width, 1
        }, color);
    }
    ios_graphics_fill_rect(surface, (struct ios_graphics_rect){
        rectangle.x, rectangle.y, 1, rectangle.height
    }, color);
    if (rectangle.width > 1) {
        ios_graphics_fill_rect(surface, (struct ios_graphics_rect){
            rectangle.x + rectangle.width - 1, rectangle.y, 1, rectangle.height
        }, color);
    }
}

void ios_graphics_draw_pointer(
    struct ios_graphics_surface *surface, ios_i32 x, ios_i32 y, ios_u32 color
)
{
    static const ios_u8 row_widths[] = { 1, 2, 3, 4, 5, 6, 7, 8, 5, 3, 3, 2 };
    for (ios_size row = 0; row < IOS_ARRAY_COUNT(row_widths); ++row) {
        ios_graphics_fill_rect(surface, (struct ios_graphics_rect){
            x, y + (ios_i32)row, row_widths[row], 1
        }, color);
    }
}
