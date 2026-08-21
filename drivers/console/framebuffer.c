#include <inferenceos/framebuffer.h>

#define GOP_PIXEL_RED_GREEN_BLUE 0U
#define GOP_PIXEL_BLUE_GREEN_RED 1U
#define GOP_PIXEL_BIT_MASK 2U
#define GLYPH_ROWS(a, b, c, d, e, f, g) \
    (UINT64_C(a) | (UINT64_C(b) << 5U) | (UINT64_C(c) << 10U) \
    | (UINT64_C(d) << 15U) | (UINT64_C(e) << 20U) \
    | (UINT64_C(f) << 25U) | (UINT64_C(g) << 30U))

typedef struct framebuffer_state {
    volatile inferenceos_u32 *pixels;
    inferenceos_u64 size;
    inferenceos_u32 width;
    inferenceos_u32 height;
    inferenceos_u32 stride;
    inferenceos_u32 foreground;
    inferenceos_u32 background;
    inferenceos_u32 cursor_column;
    inferenceos_u32 cursor_row;
    inferenceos_u32 columns;
    inferenceos_u32 rows;
    bool available;
} framebuffer_state;

static framebuffer_state framebuffer;

static inferenceos_u64 glyph_encoding(inferenceos_u8 character)
{
    if (character >= (inferenceos_u8)'a' && character <= (inferenceos_u8)'z') {
        character = (inferenceos_u8)(character - (inferenceos_u8)'a' + (inferenceos_u8)'A');
    }
    switch (character) {
    case 'A': return GLYPH_ROWS(14,17,17,31,17,17,17);
    case 'B': return GLYPH_ROWS(30,17,17,30,17,17,30);
    case 'C': return GLYPH_ROWS(14,17,16,16,16,17,14);
    case 'D': return GLYPH_ROWS(30,17,17,17,17,17,30);
    case 'E': return GLYPH_ROWS(31,16,16,30,16,16,31);
    case 'F': return GLYPH_ROWS(31,16,16,30,16,16,16);
    case 'G': return GLYPH_ROWS(14,17,16,23,17,17,15);
    case 'H': return GLYPH_ROWS(17,17,17,31,17,17,17);
    case 'I': return GLYPH_ROWS(14,4,4,4,4,4,14);
    case 'J': return GLYPH_ROWS(7,2,2,2,18,18,12);
    case 'K': return GLYPH_ROWS(17,18,20,24,20,18,17);
    case 'L': return GLYPH_ROWS(16,16,16,16,16,16,31);
    case 'M': return GLYPH_ROWS(17,27,21,21,17,17,17);
    case 'N': return GLYPH_ROWS(17,25,21,19,17,17,17);
    case 'O': return GLYPH_ROWS(14,17,17,17,17,17,14);
    case 'P': return GLYPH_ROWS(30,17,17,30,16,16,16);
    case 'Q': return GLYPH_ROWS(14,17,17,17,21,18,13);
    case 'R': return GLYPH_ROWS(30,17,17,30,20,18,17);
    case 'S': return GLYPH_ROWS(15,16,16,14,1,1,30);
    case 'T': return GLYPH_ROWS(31,4,4,4,4,4,4);
    case 'U': return GLYPH_ROWS(17,17,17,17,17,17,14);
    case 'V': return GLYPH_ROWS(17,17,17,17,17,10,4);
    case 'W': return GLYPH_ROWS(17,17,17,21,21,21,10);
    case 'X': return GLYPH_ROWS(17,17,10,4,10,17,17);
    case 'Y': return GLYPH_ROWS(17,17,10,4,4,4,4);
    case 'Z': return GLYPH_ROWS(31,1,2,4,8,16,31);
    case '0': return GLYPH_ROWS(14,17,19,21,25,17,14);
    case '1': return GLYPH_ROWS(4,12,4,4,4,4,14);
    case '2': return GLYPH_ROWS(14,17,1,2,4,8,31);
    case '3': return GLYPH_ROWS(30,1,1,14,1,1,30);
    case '4': return GLYPH_ROWS(2,6,10,18,31,2,2);
    case '5': return GLYPH_ROWS(31,16,16,30,1,1,30);
    case '6': return GLYPH_ROWS(14,16,16,30,17,17,14);
    case '7': return GLYPH_ROWS(31,1,2,4,8,8,8);
    case '8': return GLYPH_ROWS(14,17,17,14,17,17,14);
    case '9': return GLYPH_ROWS(14,17,17,15,1,1,14);
    case ' ': return 0U;
    case '.': return GLYPH_ROWS(0,0,0,0,0,12,12);
    case ',': return GLYPH_ROWS(0,0,0,0,0,12,8);
    case ':': return GLYPH_ROWS(0,12,12,0,12,12,0);
    case ';': return GLYPH_ROWS(0,12,12,0,12,8,16);
    case '-': return GLYPH_ROWS(0,0,0,31,0,0,0);
    case '_': return GLYPH_ROWS(0,0,0,0,0,0,31);
    case '/': return GLYPH_ROWS(1,2,2,4,8,8,16);
    case '\\': return GLYPH_ROWS(16,8,8,4,2,2,1);
    case '>': return GLYPH_ROWS(16,8,4,2,4,8,16);
    case '<': return GLYPH_ROWS(1,2,4,8,4,2,1);
    case '=': return GLYPH_ROWS(0,31,0,31,0,0,0);
    case '+': return GLYPH_ROWS(0,4,4,31,4,4,0);
    case '!': return GLYPH_ROWS(4,4,4,4,4,0,4);
    case '?': return GLYPH_ROWS(14,17,1,2,4,0,4);
    case '(': return GLYPH_ROWS(2,4,8,8,8,4,2);
    case ')': return GLYPH_ROWS(8,4,2,2,2,4,8);
    case '[': return GLYPH_ROWS(14,8,8,8,8,8,14);
    case ']': return GLYPH_ROWS(14,2,2,2,2,2,14);
    case '#': return GLYPH_ROWS(10,31,10,10,31,10,0);
    case '"': return GLYPH_ROWS(10,10,10,0,0,0,0);
    case '$': return GLYPH_ROWS(4,15,20,14,5,30,4);
    case '%': return GLYPH_ROWS(17,2,4,8,16,17,0);
    case '&': return GLYPH_ROWS(12,18,20,8,21,18,13);
    case '\'': return GLYPH_ROWS(4,4,8,0,0,0,0);
    case '*': return GLYPH_ROWS(0,21,14,31,14,21,0);
    case '@': return GLYPH_ROWS(14,17,23,21,23,16,14);
    case '^': return GLYPH_ROWS(4,10,17,0,0,0,0);
    case '`': return GLYPH_ROWS(8,4,2,0,0,0,0);
    case '{': return GLYPH_ROWS(2,4,4,8,4,4,2);
    case '|': return GLYPH_ROWS(4,4,4,4,4,4,4);
    case '}': return GLYPH_ROWS(8,4,4,2,4,4,8);
    case '~': return GLYPH_ROWS(0,0,9,22,0,0,0);
    default: return GLYPH_ROWS(31,17,1,2,4,0,4);
    }
}

static void put_pixel(inferenceos_u32 x, inferenceos_u32 y, inferenceos_u32 color)
{
    const inferenceos_u64 index =
        (inferenceos_u64)y * framebuffer.stride + x;
    framebuffer.pixels[(inferenceos_size)index] = color;
}

static void clear_cell(inferenceos_u32 column, inferenceos_u32 row)
{
    const inferenceos_u32 start_x = column * INFERENCEOS_FRAMEBUFFER_GLYPH_WIDTH;
    const inferenceos_u32 start_y = row * INFERENCEOS_FRAMEBUFFER_GLYPH_HEIGHT;
    for (inferenceos_u32 y = 0U; y < INFERENCEOS_FRAMEBUFFER_GLYPH_HEIGHT; ++y) {
        for (inferenceos_u32 x = 0U; x < INFERENCEOS_FRAMEBUFFER_GLYPH_WIDTH; ++x) {
            put_pixel(start_x + x, start_y + y, framebuffer.background);
        }
    }
}

static void draw_character(inferenceos_u8 character)
{
    const inferenceos_u64 glyph = glyph_encoding(character);
    const inferenceos_u32 start_x =
        framebuffer.cursor_column * INFERENCEOS_FRAMEBUFFER_GLYPH_WIDTH;
    const inferenceos_u32 start_y =
        framebuffer.cursor_row * INFERENCEOS_FRAMEBUFFER_GLYPH_HEIGHT;

    for (inferenceos_u32 y = 0U; y < INFERENCEOS_FRAMEBUFFER_GLYPH_HEIGHT; ++y) {
        const inferenceos_u32 glyph_row = y < 14U ? y / 2U : 7U;
        const inferenceos_u32 bits = glyph_row < 7U
            ? (inferenceos_u32)((glyph >> (glyph_row * 5U)) & UINT64_C(0x1F))
            : 0U;
        for (inferenceos_u32 x = 0U; x < INFERENCEOS_FRAMEBUFFER_GLYPH_WIDTH; ++x) {
            const bool set = x > 0U && x < 6U
                && (bits & (UINT32_C(1) << (5U - x))) != 0U;
            put_pixel(start_x + x, start_y + y,
                set ? framebuffer.foreground : framebuffer.background);
        }
    }
}

static void scroll_one_row(void)
{
    const inferenceos_u32 source_y = INFERENCEOS_FRAMEBUFFER_GLYPH_HEIGHT;
    const inferenceos_u32 retained_height = framebuffer.height - source_y;

    for (inferenceos_u32 y = 0U; y < retained_height; ++y) {
        for (inferenceos_u32 x = 0U; x < framebuffer.width; ++x) {
            const inferenceos_u64 destination =
                (inferenceos_u64)y * framebuffer.stride + x;
            const inferenceos_u64 source =
                (inferenceos_u64)(y + source_y) * framebuffer.stride + x;
            framebuffer.pixels[(inferenceos_size)destination] =
                framebuffer.pixels[(inferenceos_size)source];
        }
    }
    for (inferenceos_u32 y = retained_height; y < framebuffer.height; ++y) {
        for (inferenceos_u32 x = 0U; x < framebuffer.width; ++x) {
            put_pixel(x, y, framebuffer.background);
        }
    }
}

static void newline(void)
{
    framebuffer.cursor_column = 0U;
    ++framebuffer.cursor_row;
    if (framebuffer.cursor_row >= framebuffer.rows) {
        scroll_one_row();
        framebuffer.cursor_row = framebuffer.rows - 1U;
    }
}

inferenceos_result inferenceos_framebuffer_initialize(
    const inferenceos_framebuffer_config *config
)
{
    inferenceos_u64 pixel_count;
    inferenceos_u64 required_size;
    inferenceos_u64 framebuffer_end;
    inferenceos_u32 foreground = UINT32_C(0x00FFFFFF);

    framebuffer.available = false;
    if (config == NULL || config->base == 0U || config->width < 8U
        || config->height < 16U || config->pixels_per_scan_line < config->width) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    if (config->pixel_format > GOP_PIXEL_BIT_MASK) {
        return INFERENCEOS_RESULT_UNSUPPORTED;
    }
    if (config->pixel_format == GOP_PIXEL_BIT_MASK) {
        if (config->masks.red == 0U || config->masks.green == 0U
            || config->masks.blue == 0U
            || (config->masks.red & config->masks.green) != 0U
            || (config->masks.red & config->masks.blue) != 0U
            || (config->masks.green & config->masks.blue) != 0U) {
            return INFERENCEOS_RESULT_UNSUPPORTED;
        }
        foreground = config->masks.red | config->masks.green | config->masks.blue;
    }
    if (!inferenceos_checked_mul_u64(config->pixels_per_scan_line,
            config->height, &pixel_count)
        || !inferenceos_checked_mul_u64(pixel_count, sizeof(inferenceos_u32),
            &required_size)
        || required_size > config->size
        || !inferenceos_checked_add_u64(config->base, required_size,
            &framebuffer_end)) {
        return INFERENCEOS_RESULT_OVERFLOW;
    }
    (void)framebuffer_end;
    framebuffer.pixels = (volatile inferenceos_u32 *)(inferenceos_uptr)config->base;
    framebuffer.size = config->size;
    framebuffer.width = config->width;
    framebuffer.height = config->height;
    framebuffer.stride = config->pixels_per_scan_line;
    framebuffer.foreground = foreground;
    framebuffer.background = 0U;
    framebuffer.cursor_column = 0U;
    framebuffer.cursor_row = 0U;
    framebuffer.columns = config->width / INFERENCEOS_FRAMEBUFFER_GLYPH_WIDTH;
    framebuffer.rows = config->height / INFERENCEOS_FRAMEBUFFER_GLYPH_HEIGHT;
    framebuffer.available = true;
    return inferenceos_framebuffer_clear();
}

bool inferenceos_framebuffer_is_available(void)
{
    return framebuffer.available;
}

inferenceos_result inferenceos_framebuffer_write_byte(inferenceos_u8 byte)
{
    if (!framebuffer.available) {
        return INFERENCEOS_RESULT_NOT_READY;
    }
    if (byte == (inferenceos_u8)'\r') {
        framebuffer.cursor_column = 0U;
    } else if (byte == (inferenceos_u8)'\n') {
        newline();
    } else if (byte == (inferenceos_u8)'\b') {
        bool moved = false;
        if (framebuffer.cursor_column > 0U) {
            --framebuffer.cursor_column;
            moved = true;
        } else if (framebuffer.cursor_row > 0U) {
            --framebuffer.cursor_row;
            framebuffer.cursor_column = framebuffer.columns - 1U;
            moved = true;
        }
        if (moved) {
            clear_cell(framebuffer.cursor_column, framebuffer.cursor_row);
        }
    } else if (byte == (inferenceos_u8)'\t') {
        do {
            inferenceos_result result = inferenceos_framebuffer_write_byte((inferenceos_u8)' ');
            if (!inferenceos_result_is_success(result)) {
                return result;
            }
        } while ((framebuffer.cursor_column & 3U) != 0U);
    } else if (byte >= 0x20U && byte <= 0x7EU) {
        draw_character(byte);
        ++framebuffer.cursor_column;
        if (framebuffer.cursor_column >= framebuffer.columns) {
            newline();
        }
    } else {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    return INFERENCEOS_RESULT_OK;
}

inferenceos_result inferenceos_framebuffer_clear(void)
{
    if (!framebuffer.available) {
        return INFERENCEOS_RESULT_NOT_READY;
    }
    for (inferenceos_u32 y = 0U; y < framebuffer.height; ++y) {
        for (inferenceos_u32 x = 0U; x < framebuffer.width; ++x) {
            put_pixel(x, y, framebuffer.background);
        }
    }
    framebuffer.cursor_column = 0U;
    framebuffer.cursor_row = 0U;
    return INFERENCEOS_RESULT_OK;
}
