#include <inferenceos/test.h>

#include <inferenceos/boot_info.h>
#include <inferenceos/drivers/framebuffer.h>
#include <inferenceos/gui/graphics.h>

#include <string.h>

enum {
    TEST_WIDTH = 12,
    TEST_HEIGHT = 20,
    TEST_STRIDE = 16,
    CONSOLE_WIDTH = 24,
    CONSOLE_HEIGHT = 32,
    CONSOLE_STRIDE = 28,
    TEST_FONT_GLYPHS = 128,
    TEST_FONT_SIZE = IOS_PSF2_HEADER_SIZE + TEST_FONT_GLYPHS * IOS_PSF2_FONT_HEIGHT,
    TEST_ALPHA4_GLYPH_BYTES = IOS_ALPHA4_FONT_WIDTH * IOS_ALPHA4_FONT_HEIGHT / 2,
    TEST_ALPHA4_FONT_SIZE = IOS_ALPHA4_HEADER_SIZE
        + TEST_FONT_GLYPHS * TEST_ALPHA4_GLYPH_BYTES
};

static void write_u32_le(ios_u8 *bytes, ios_u32 value)
{
    *bytes = (ios_u8)value;
    bytes[1] = (ios_u8)(value >> 8);
    bytes[2] = (ios_u8)(value >> 16);
    bytes[3] = (ios_u8)(value >> 24);
}

static void make_test_font(ios_u8 *bytes)
{
    memset(bytes, 0, TEST_FONT_SIZE);
    write_u32_le(bytes, IOS_PSF2_MAGIC);
    write_u32_le(bytes + 4, 0);
    write_u32_le(bytes + 8, IOS_PSF2_HEADER_SIZE);
    write_u32_le(bytes + 12, 0);
    write_u32_le(bytes + 16, TEST_FONT_GLYPHS);
    write_u32_le(bytes + 20, IOS_PSF2_FONT_HEIGHT);
    write_u32_le(bytes + 24, IOS_PSF2_FONT_HEIGHT);
    write_u32_le(bytes + 28, IOS_PSF2_FONT_WIDTH);
    bytes[IOS_PSF2_HEADER_SIZE + 'A' * IOS_PSF2_FONT_HEIGHT] = UINT8_C(0x80);
    bytes[IOS_PSF2_HEADER_SIZE + 'B' * IOS_PSF2_FONT_HEIGHT] = UINT8_C(0x40);
    bytes[IOS_PSF2_HEADER_SIZE + 'C' * IOS_PSF2_FONT_HEIGHT] = UINT8_C(0x20);
}

static void make_test_alpha4_font(ios_u8 *bytes)
{
    memset(bytes, 0, TEST_ALPHA4_FONT_SIZE);
    write_u32_le(bytes, IOS_ALPHA4_MAGIC);
    write_u32_le(bytes + 4, 0);
    write_u32_le(bytes + 8, IOS_ALPHA4_HEADER_SIZE);
    write_u32_le(bytes + 12, 0);
    write_u32_le(bytes + 16, TEST_FONT_GLYPHS);
    write_u32_le(bytes + 20, TEST_ALPHA4_GLYPH_BYTES);
    write_u32_le(bytes + 24, IOS_ALPHA4_FONT_HEIGHT);
    write_u32_le(bytes + 28, IOS_ALPHA4_FONT_WIDTH);
    bytes[IOS_ALPHA4_HEADER_SIZE + 'A' * TEST_ALPHA4_GLYPH_BYTES] = UINT8_C(0xf8);
}

static void test_text_console_renders_erases_and_clears(void)
{
    const ios_u32 foreground = UINT32_C(0x00f0f0f0);
    const ios_u32 background = UINT32_C(0x00101010);
    ios_u8 font_data[TEST_FONT_SIZE];
    ios_u32 pixels[CONSOLE_STRIDE * CONSOLE_HEIGHT];
    struct ios_graphics_surface surface = {
        pixels, CONSOLE_WIDTH, CONSOLE_HEIGHT, CONSOLE_STRIDE
    };
    struct ios_psf2_font font;
    struct ios_graphics_text_console console;

    make_test_font(font_data);
    IOS_TEST_ASSERT_STATUS(ios_psf2_open(font_data, sizeof(font_data), &font), IOS_OK);
    memset(pixels, 0xcc, sizeof(pixels));
    IOS_TEST_ASSERT_STATUS(
        ios_graphics_text_console_initialize(
            &console, surface, &font, foreground, background
        ),
        IOS_OK
    );
    IOS_TEST_ASSERT(console.active && console.cursor_column == 0 && console.cursor_row == 0);
    IOS_TEST_ASSERT(pixels[0] == background);
    IOS_TEST_ASSERT(pixels[CONSOLE_WIDTH] == UINT32_C(0xcccccccc));

    IOS_TEST_ASSERT_STATUS(ios_graphics_text_console_write(&console, "AB"), IOS_OK);
    IOS_TEST_ASSERT(pixels[0] == foreground);
    IOS_TEST_ASSERT(pixels[8] == background && pixels[9] == foreground);
    IOS_TEST_ASSERT(console.cursor_column == 2 && console.cursor_row == 0);

    IOS_TEST_ASSERT_STATUS(ios_graphics_text_console_write(&console, "\b \b"), IOS_OK);
    IOS_TEST_ASSERT(pixels[9] == background);
    IOS_TEST_ASSERT(console.cursor_column == 1 && console.cursor_row == 0);

    IOS_TEST_ASSERT_STATUS(
        ios_graphics_text_console_write(&console, "\x1b[2J\x1b[H"), IOS_OK
    );
    IOS_TEST_ASSERT(console.cursor_column == 0 && console.cursor_row == 0);
    IOS_TEST_ASSERT(pixels[0] == background && pixels[9] == background);
}

static void test_text_console_wraps_and_scrolls(void)
{
    const ios_u32 foreground = UINT32_C(0x00ffffff);
    const ios_u32 background = UINT32_C(0x00000000);
    ios_u8 font_data[TEST_FONT_SIZE];
    ios_u32 pixels[CONSOLE_STRIDE * CONSOLE_HEIGHT];
    struct ios_graphics_surface surface = {
        pixels, CONSOLE_WIDTH, CONSOLE_HEIGHT, CONSOLE_STRIDE
    };
    struct ios_psf2_font font;
    struct ios_graphics_text_console console;

    make_test_font(font_data);
    IOS_TEST_ASSERT_STATUS(ios_psf2_open(font_data, sizeof(font_data), &font), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_graphics_text_console_initialize(
            &console, surface, &font, foreground, background
        ),
        IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(
        ios_graphics_text_console_write(&console, "A\nB\nC"), IOS_OK
    );
    IOS_TEST_ASSERT(pixels[0] == background && pixels[1] == foreground);
    IOS_TEST_ASSERT(pixels[CONSOLE_STRIDE * IOS_PSF2_FONT_HEIGHT] == background);
    IOS_TEST_ASSERT(
        pixels[CONSOLE_STRIDE * IOS_PSF2_FONT_HEIGHT + 2] == foreground
    );
    IOS_TEST_ASSERT(console.cursor_column == 1 && console.cursor_row == 1);
}

static void test_gop_open_validates_contract_and_preserves_stride(void)
{
    ios_u32 pixels[TEST_STRIDE * TEST_HEIGHT];
    struct ios_boot_info boot_info = {
        .structure_size = sizeof(struct ios_boot_info),
        .version = IOS_BOOT_INFO_VERSION,
        .framebuffer_address = (ios_uptr)pixels,
        .framebuffer_size = sizeof(pixels),
        .framebuffer_width = TEST_WIDTH,
        .framebuffer_height = TEST_HEIGHT,
        .framebuffer_stride = TEST_STRIDE,
        .framebuffer_format = IOS_BOOT_FRAMEBUFFER_BGRX8888
    };
    struct ios_graphics_surface surface;

    IOS_TEST_ASSERT_STATUS(ios_gop_framebuffer_open(&boot_info, &surface), IOS_OK);
    IOS_TEST_ASSERT(surface.pixels == pixels);
    IOS_TEST_ASSERT(surface.width == TEST_WIDTH && surface.stride == TEST_STRIDE);

    boot_info.framebuffer_size = sizeof(pixels) - 1;
    IOS_TEST_ASSERT_STATUS(
        ios_gop_framebuffer_open(&boot_info, &surface), IOS_ERROR(IOS_E_OUT_OF_RANGE)
    );
    IOS_TEST_ASSERT(surface.pixels == NULL);
    boot_info.framebuffer_size = sizeof(pixels);
    boot_info.framebuffer_format = 99;
    IOS_TEST_ASSERT_STATUS(
        ios_gop_framebuffer_open(&boot_info, &surface), IOS_ERROR(IOS_E_NOT_SUPPORTED)
    );
}

static void test_primitives_clip_and_do_not_write_stride_padding(void)
{
    ios_u32 pixels[TEST_STRIDE * TEST_HEIGHT];
    struct ios_graphics_surface surface = { pixels, TEST_WIDTH, TEST_HEIGHT, TEST_STRIDE };
    memset(pixels, 0xcc, sizeof(pixels));

    ios_graphics_fill_rect(
        &surface, (struct ios_graphics_rect){ -2, -1, 5, 4 }, UINT32_C(0x00112233)
    );
    IOS_TEST_ASSERT(pixels[0] == UINT32_C(0x00112233));
    IOS_TEST_ASSERT(pixels[2 + 2 * TEST_STRIDE] == UINT32_C(0x00112233));
    IOS_TEST_ASSERT(pixels[3] == UINT32_C(0xcccccccc));
    IOS_TEST_ASSERT(pixels[TEST_WIDTH] == UINT32_C(0xcccccccc));

    ios_graphics_draw_border(
        &surface, (struct ios_graphics_rect){ 4, 2, 5, 4 }, UINT32_C(0x00ffffff)
    );
    IOS_TEST_ASSERT(pixels[4 + 2 * TEST_STRIDE] == UINT32_C(0x00ffffff));
    IOS_TEST_ASSERT(pixels[6 + 3 * TEST_STRIDE] == UINT32_C(0xcccccccc));
    ios_graphics_draw_line(&surface, -2, 10, 3, 10, UINT32_C(0x0000ff00));
    IOS_TEST_ASSERT(pixels[3 + 10 * TEST_STRIDE] == UINT32_C(0x0000ff00));
    ios_graphics_draw_pointer(&surface, 11, 19, UINT32_C(0x00abcdef));
    IOS_TEST_ASSERT(pixels[11 + 19 * TEST_STRIDE] == UINT32_C(0x00abcdef));
}

static void test_psf2_validation_and_text_rendering(void)
{
    ios_u8 font_data[TEST_FONT_SIZE];
    ios_u32 pixels[TEST_STRIDE * TEST_HEIGHT];
    struct ios_graphics_surface surface = { pixels, TEST_WIDTH, TEST_HEIGHT, TEST_STRIDE };
    struct ios_psf2_font font;

    make_test_font(font_data);
    IOS_TEST_ASSERT_STATUS(ios_psf2_open(font_data, sizeof(font_data), &font), IOS_OK);
    memset(pixels, 0, sizeof(pixels));
    IOS_TEST_ASSERT_STATUS(
        ios_graphics_draw_text(
            &surface, &font, "A", 2, 1,
            UINT32_C(0x00ffffff), UINT32_C(0x00112233), true
        ),
        IOS_OK
    );
    IOS_TEST_ASSERT(pixels[2 + TEST_STRIDE] == UINT32_C(0x00ffffff));
    IOS_TEST_ASSERT(pixels[3 + TEST_STRIDE] == UINT32_C(0x00112233));
    IOS_TEST_ASSERT(pixels[TEST_WIDTH + TEST_STRIDE] == 0);

    IOS_TEST_ASSERT_STATUS(
        ios_psf2_open(font_data, sizeof(font_data) - 1, &font), IOS_ERROR(IOS_E_CORRUPT)
    );
    write_u32_le(font_data + 28, 9);
    IOS_TEST_ASSERT_STATUS(
        ios_psf2_open(font_data, sizeof(font_data), &font), IOS_ERROR(IOS_E_NOT_SUPPORTED)
    );
    write_u32_le(font_data, 0);
    IOS_TEST_ASSERT_STATUS(
        ios_psf2_open(font_data, sizeof(font_data), &font), IOS_ERROR(IOS_E_CORRUPT)
    );
}

static void test_alpha4_validation_and_blended_rendering(void)
{
    const ios_u32 foreground = UINT32_C(0x00f0c080);
    const ios_u32 background = UINT32_C(0x00102040);
    ios_u8 font_data[TEST_ALPHA4_FONT_SIZE];
    ios_u32 pixels[IOS_ALPHA4_FONT_WIDTH * IOS_ALPHA4_FONT_HEIGHT];
    struct ios_graphics_surface surface = {
        pixels, IOS_ALPHA4_FONT_WIDTH, IOS_ALPHA4_FONT_HEIGHT,
        IOS_ALPHA4_FONT_WIDTH
    };
    struct ios_psf2_font font;

    make_test_alpha4_font(font_data);
    IOS_TEST_ASSERT_STATUS(
        ios_alpha4_open(font_data, sizeof(font_data), &font), IOS_OK
    );
    IOS_TEST_ASSERT(font.format == IOS_RASTER_FONT_ALPHA4);
    IOS_TEST_ASSERT(
        font.width == IOS_ALPHA4_FONT_WIDTH && font.height == IOS_ALPHA4_FONT_HEIGHT
    );
    memset(pixels, 0, sizeof(pixels));
    IOS_TEST_ASSERT_STATUS(
        ios_graphics_draw_glyph(
            &surface, &font, 'A', 0, 0, foreground, background, true
        ),
        IOS_OK
    );
    IOS_TEST_ASSERT(pixels[0] == foreground);
    IOS_TEST_ASSERT(pixels[1] == UINT32_C(0x00877562));
    IOS_TEST_ASSERT(pixels[2] == background);

    IOS_TEST_ASSERT_STATUS(
        ios_alpha4_open(font_data, sizeof(font_data) - 1, &font),
        IOS_ERROR(IOS_E_CORRUPT)
    );
    write_u32_le(font_data + 4, 1);
    IOS_TEST_ASSERT_STATUS(
        ios_alpha4_open(font_data, sizeof(font_data), &font),
        IOS_ERROR(IOS_E_UNSUPPORTED_VERSION)
    );
    write_u32_le(font_data, 0);
    IOS_TEST_ASSERT_STATUS(
        ios_alpha4_open(font_data, sizeof(font_data), &font),
        IOS_ERROR(IOS_E_CORRUPT)
    );
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_gop_open_validates_contract_and_preserves_stride),
    IOS_TEST_CASE(test_primitives_clip_and_do_not_write_stride_padding),
    IOS_TEST_CASE(test_psf2_validation_and_text_rendering),
    IOS_TEST_CASE(test_alpha4_validation_and_blended_rendering),
    IOS_TEST_CASE(test_text_console_renders_erases_and_clears),
    IOS_TEST_CASE(test_text_console_wraps_and_scrolls)
};

const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
