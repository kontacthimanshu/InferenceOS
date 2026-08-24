#include <inferenceos/gui/graphics.h>

static ios_u32 read_u32_le(const ios_u8 *bytes)
{
    return (ios_u32)*bytes
        | ((ios_u32)bytes[1] << 8)
        | ((ios_u32)bytes[2] << 16)
        | ((ios_u32)bytes[3] << 24);
}

ios_status ios_psf2_open(
    const void *font_data, ios_size font_size, struct ios_psf2_font *font
)
{
    const ios_u8 *bytes = font_data;
    ios_u32 header_size;
    ios_u32 flags;
    ios_u32 glyph_count;
    ios_u32 bytes_per_glyph;
    ios_u32 height;
    ios_u32 width;
    ios_size glyph_bytes;

    if (font_data == NULL || font == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    *font = (struct ios_psf2_font){ 0 };
    if (font_size < IOS_PSF2_HEADER_SIZE) return IOS_ERROR(IOS_E_CORRUPT);
    if (read_u32_le(bytes) != IOS_PSF2_MAGIC) return IOS_ERROR(IOS_E_CORRUPT);
    if (read_u32_le(bytes + 4) != 0) return IOS_ERROR(IOS_E_UNSUPPORTED_VERSION);
    header_size = read_u32_le(bytes + 8);
    flags = read_u32_le(bytes + 12);
    glyph_count = read_u32_le(bytes + 16);
    bytes_per_glyph = read_u32_le(bytes + 20);
    height = read_u32_le(bytes + 24);
    width = read_u32_le(bytes + 28);
    if (header_size < IOS_PSF2_HEADER_SIZE || header_size > font_size || flags > 1) {
        return IOS_ERROR(IOS_E_CORRUPT);
    }
    if (width != IOS_PSF2_FONT_WIDTH || height != IOS_PSF2_FONT_HEIGHT
        || glyph_count < 128 || bytes_per_glyph != IOS_PSF2_FONT_HEIGHT) {
        return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    }
    if (glyph_count > SIZE_MAX / bytes_per_glyph) return IOS_ERROR(IOS_E_OVERFLOW);
    glyph_bytes = (ios_size)glyph_count * bytes_per_glyph;
    if (glyph_bytes > font_size - header_size) return IOS_ERROR(IOS_E_CORRUPT);

    font->glyphs = bytes + header_size;
    font->glyph_data_size = glyph_bytes;
    font->glyph_count = glyph_count;
    font->bytes_per_glyph = bytes_per_glyph;
    font->width = width;
    font->height = height;
    return IOS_OK;
}

ios_status ios_graphics_draw_glyph(
    struct ios_graphics_surface *surface,
    const struct ios_psf2_font *font,
    ios_u32 codepoint,
    ios_i32 x,
    ios_i32 y,
    ios_u32 foreground,
    ios_u32 background,
    bool opaque_background
)
{
    const ios_u8 *glyph;
    if (!ios_graphics_surface_is_valid(surface) || font == NULL || font->glyphs == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (codepoint >= font->glyph_count) return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    glyph = font->glyphs + (ios_size)codepoint * font->bytes_per_glyph;
    for (ios_u32 row = 0; row < font->height; ++row) {
        const ios_u8 bits = glyph[row];
        for (ios_u32 column = 0; column < font->width; ++column) {
            const bool set = (bits & (UINT8_C(0x80) >> column)) != 0;
            if (set || opaque_background) {
                ios_graphics_put_pixel(
                    surface, x + (ios_i32)column, y + (ios_i32)row,
                    set ? foreground : background
                );
            }
        }
    }
    return IOS_OK;
}

ios_status ios_graphics_draw_text(
    struct ios_graphics_surface *surface,
    const struct ios_psf2_font *font,
    const char *text,
    ios_i32 x,
    ios_i32 y,
    ios_u32 foreground,
    ios_u32 background,
    bool opaque_background
)
{
    ios_i32 cursor_x = x;
    if (text == NULL || !ios_graphics_surface_is_valid(surface)
        || font == NULL || font->glyphs == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    while (*text != '\0') {
        const ios_u8 character = (ios_u8)*text++;
        ios_status status;
        if (character == '\n') {
            cursor_x = x;
            y += (ios_i32)font->height;
            continue;
        }
        status = ios_graphics_draw_glyph(
            surface, font, character, cursor_x, y,
            foreground, background, opaque_background
        );
        if (IOS_FAILED(status)) return status;
        cursor_x += (ios_i32)font->width;
    }
    return IOS_OK;
}
