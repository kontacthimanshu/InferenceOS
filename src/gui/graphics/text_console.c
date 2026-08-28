#include <inferenceos/gui/graphics.h>

#include <inferenceos/runtime.h>

static void text_console_scroll(struct ios_graphics_text_console *console)
{
    const ios_size row_pixels = (ios_size)console->surface.stride * console->font->height;
    const ios_size retained_rows = console->surface.height - console->font->height;

    memmove(
        console->surface.pixels,
        console->surface.pixels + row_pixels,
        retained_rows * console->surface.stride * sizeof(ios_u32)
    );
    ios_graphics_fill_rect(
        &console->surface,
        (struct ios_graphics_rect){
            0,
            (ios_i32)retained_rows,
            (ios_i32)console->surface.width,
            (ios_i32)console->font->height
        },
        console->background
    );
}

static void text_console_newline(struct ios_graphics_text_console *console)
{
    console->cursor_column = 0;
    ++console->cursor_row;
    if ((console->cursor_row + 1) * console->font->height > console->surface.height) {
        text_console_scroll(console);
        --console->cursor_row;
    }
}

ios_status ios_graphics_text_console_initialize(
    struct ios_graphics_text_console *console,
    struct ios_graphics_surface surface,
    const struct ios_psf2_font *font,
    ios_u32 foreground,
    ios_u32 background
)
{
    if (console == NULL || font == NULL || font->glyphs == NULL
        || font->width == 0 || font->height == 0
        || !ios_graphics_surface_is_valid(&surface)
        || surface.width < font->width || surface.height < font->height
        || surface.width > INT32_MAX || surface.height > INT32_MAX
        || surface.stride > SIZE_MAX / sizeof(ios_u32) / surface.height) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }

    *console = (struct ios_graphics_text_console){
        .surface = surface,
        .font = font,
        .foreground = foreground,
        .background = background,
        .active = true
    };
    return ios_graphics_text_console_clear(console);
}

ios_status ios_graphics_text_console_clear(struct ios_graphics_text_console *console)
{
    if (console == NULL || !console->active) return IOS_ERROR(IOS_E_INVALID_STATE);
    ios_graphics_fill_rect(
        &console->surface,
        (struct ios_graphics_rect){
            0, 0, (ios_i32)console->surface.width, (ios_i32)console->surface.height
        },
        console->background
    );
    console->cursor_column = 0;
    console->cursor_row = 0;
    return IOS_OK;
}

ios_status ios_graphics_text_console_write(
    struct ios_graphics_text_console *console, const char *text
)
{
    ios_u32 columns;

    if (console == NULL || !console->active) return IOS_ERROR(IOS_E_INVALID_STATE);
    if (text == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    if (strcmp(text, "\x1b[2J\x1b[H") == 0) {
        return ios_graphics_text_console_clear(console);
    }

    columns = console->surface.width / console->font->width;
    while (*text != '\0') {
        const ios_u8 character = (ios_u8)*text++;
        ios_status status;

        if (character == '\n') {
            text_console_newline(console);
            continue;
        }
        if (character == '\r') {
            console->cursor_column = 0;
            continue;
        }
        if (character == '\b') {
            if (console->cursor_column > 0) --console->cursor_column;
            continue;
        }
        if (character < 0x20 || character > 0x7e) continue;
        if (console->cursor_column >= columns) text_console_newline(console);
        status = ios_graphics_draw_glyph(
            &console->surface,
            console->font,
            character,
            (ios_i32)(console->cursor_column * console->font->width),
            (ios_i32)(console->cursor_row * console->font->height),
            console->foreground,
            console->background,
            true
        );
        if (IOS_FAILED(status)) return status;
        ++console->cursor_column;
    }
    return IOS_OK;
}
