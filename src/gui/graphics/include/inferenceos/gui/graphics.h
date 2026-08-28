#ifndef INFERENCEOS_GUI_GRAPHICS_H
#define INFERENCEOS_GUI_GRAPHICS_H

#include <inferenceos/errors.h>

enum {
    IOS_PSF2_HEADER_SIZE = 32,
    IOS_PSF2_FONT_WIDTH = 8,
    IOS_PSF2_FONT_HEIGHT = 16,
    IOS_ALPHA4_HEADER_SIZE = 32,
    IOS_ALPHA4_FONT_WIDTH = 12,
    IOS_ALPHA4_FONT_HEIGHT = 24
};

#define IOS_PSF2_MAGIC UINT32_C(0x864ab572)
#define IOS_ALPHA4_MAGIC UINT32_C(0x34464149)

enum ios_raster_font_format {
    IOS_RASTER_FONT_MONO1 = 0,
    IOS_RASTER_FONT_ALPHA4 = 1
};

struct ios_graphics_rect {
    ios_i32 x;
    ios_i32 y;
    ios_i32 width;
    ios_i32 height;
};

struct ios_graphics_surface {
    ios_u32 *pixels;
    ios_u32 width;
    ios_u32 height;
    ios_u32 stride;
};

struct ios_psf2_font {
    const ios_u8 *glyphs;
    ios_size glyph_data_size;
    ios_u32 glyph_count;
    ios_u32 bytes_per_glyph;
    ios_u32 width;
    ios_u32 height;
    enum ios_raster_font_format format;
};

struct ios_graphics_text_console {
    struct ios_graphics_surface surface;
    const struct ios_psf2_font *font;
    ios_u32 foreground;
    ios_u32 background;
    ios_u32 cursor_column;
    ios_u32 cursor_row;
    bool active;
};

bool ios_graphics_surface_is_valid(const struct ios_graphics_surface *surface);
void ios_graphics_put_pixel(
    struct ios_graphics_surface *surface, ios_i32 x, ios_i32 y, ios_u32 color
);
void ios_graphics_fill_rect(
    struct ios_graphics_surface *surface, struct ios_graphics_rect rectangle, ios_u32 color
);
void ios_graphics_draw_line(
    struct ios_graphics_surface *surface,
    ios_i32 x0, ios_i32 y0, ios_i32 x1, ios_i32 y1,
    ios_u32 color
);
void ios_graphics_draw_border(
    struct ios_graphics_surface *surface, struct ios_graphics_rect rectangle, ios_u32 color
);
void ios_graphics_draw_pointer(
    struct ios_graphics_surface *surface, ios_i32 x, ios_i32 y, ios_u32 color
);

ios_status ios_psf2_open(
    const void *font_data, ios_size font_size, struct ios_psf2_font *font
);
ios_status ios_alpha4_open(
    const void *font_data, ios_size font_size, struct ios_psf2_font *font
);
ios_status ios_graphics_draw_glyph(
    struct ios_graphics_surface *surface,
    const struct ios_psf2_font *font,
    ios_u32 codepoint,
    ios_i32 x,
    ios_i32 y,
    ios_u32 foreground,
    ios_u32 background,
    bool opaque_background
);
ios_status ios_graphics_draw_text(
    struct ios_graphics_surface *surface,
    const struct ios_psf2_font *font,
    const char *text,
    ios_i32 x,
    ios_i32 y,
    ios_u32 foreground,
    ios_u32 background,
    bool opaque_background
);
ios_status ios_graphics_text_console_initialize(
    struct ios_graphics_text_console *console,
    struct ios_graphics_surface surface,
    const struct ios_psf2_font *font,
    ios_u32 foreground,
    ios_u32 background
);
ios_status ios_graphics_text_console_clear(struct ios_graphics_text_console *console);
ios_status ios_graphics_text_console_write(
    struct ios_graphics_text_console *console, const char *text
);

#endif
