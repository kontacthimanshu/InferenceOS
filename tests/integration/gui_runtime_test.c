#include <inferenceos/test.h>

#include <inferenceos/errors.h>

#include <stdbool.h>
#include <string.h>

enum {
    TEST_SCREEN_WIDTH = 12,
    TEST_SCREEN_HEIGHT = 8,
    TEST_SCREEN_STRIDE = 16,
    TEST_INPUT_CAPACITY = 16
};

struct test_rect {
    ios_i32 x;
    ios_i32 y;
    ios_i32 width;
    ios_i32 height;
};

struct test_surface {
    ios_u32 *pixels;
    ios_i32 width;
    ios_i32 height;
    ios_i32 stride;
};

struct test_window {
    struct test_surface surface;
    ios_i32 x;
    ios_i32 y;
    ios_u32 identity;
    bool visible;
};

enum test_input_kind {
    TEST_INPUT_KEY = 1,
    TEST_INPUT_POINTER_MOVE = 2,
    TEST_INPUT_POINTER_BUTTON = 3
};

struct test_input_event {
    enum test_input_kind kind;
    ios_u32 key;
    ios_i32 x;
    ios_i32 y;
    ios_u8 button;
    bool pressed;
};

struct test_input_queue {
    struct test_input_event events[TEST_INPUT_CAPACITY];
    ios_size head;
    ios_size count;
    ios_i32 pointer_x;
    ios_i32 pointer_y;
};

struct test_gui_dependencies {
    bool framebuffer_available;
    bool font_available;
    bool pointer_available;
    bool desktop_module_available;
    bool terminal_module_available;
    bool explorer_module_available;
};

struct test_gui_runtime {
    bool gui_running;
    bool cui_usable;
    bool desktop_present;
    bool terminal_present;
    bool explorer_present;
    const char *diagnostic;
};

static bool clip_rect(
    const struct test_surface *surface,
    struct test_rect *rectangle
)
{
    ios_i32 right = rectangle->x + rectangle->width;
    ios_i32 bottom = rectangle->y + rectangle->height;
    if (rectangle->x < 0) {
        rectangle->x = 0;
    }
    if (rectangle->y < 0) {
        rectangle->y = 0;
    }
    if (right > surface->width) {
        right = surface->width;
    }
    if (bottom > surface->height) {
        bottom = surface->height;
    }
    rectangle->width = right - rectangle->x;
    rectangle->height = bottom - rectangle->y;
    return rectangle->width > 0 && rectangle->height > 0;
}

static void fill_rectangle(
    struct test_surface *surface,
    struct test_rect rectangle,
    ios_u32 color
)
{
    if (!clip_rect(surface, &rectangle)) {
        return;
    }
    for (ios_i32 y = rectangle.y; y < rectangle.y + rectangle.height; ++y) {
        for (ios_i32 x = rectangle.x; x < rectangle.x + rectangle.width; ++x) {
            surface->pixels[y * surface->stride + x] = color;
        }
    }
}

static void draw_border(
    struct test_surface *surface,
    struct test_rect rectangle,
    ios_u32 color
)
{
    fill_rectangle(surface, (struct test_rect){ rectangle.x, rectangle.y, rectangle.width, 1 }, color);
    fill_rectangle(
        surface,
        (struct test_rect){ rectangle.x, rectangle.y + rectangle.height - 1, rectangle.width, 1 },
        color
    );
    fill_rectangle(surface, (struct test_rect){ rectangle.x, rectangle.y, 1, rectangle.height }, color);
    fill_rectangle(
        surface,
        (struct test_rect){ rectangle.x + rectangle.width - 1, rectangle.y, 1, rectangle.height },
        color
    );
}

static void draw_glyph_block(
    struct test_surface *surface,
    ios_i32 x,
    ios_i32 y,
    ios_u8 rows[8],
    ios_u32 color
)
{
    for (ios_i32 row = 0; row < 8; ++row) {
        for (ios_i32 column = 0; column < 8; ++column) {
            if ((rows[row] & (UINT8_C(0x80) >> column)) != 0) {
                fill_rectangle(surface, (struct test_rect){ x + column, y + row, 1, 1 }, color);
            }
        }
    }
}

static void compose(
    struct test_surface *screen,
    const struct test_window *windows,
    ios_size window_count,
    struct test_rect dirty,
    ios_u32 desktop_color
)
{
    if (!clip_rect(screen, &dirty)) {
        return;
    }
    fill_rectangle(screen, dirty, desktop_color);
    for (ios_size index = 0; index < window_count; ++index) {
        const struct test_window *window = &windows[index];
        if (!window->visible) {
            continue;
        }
        for (ios_i32 source_y = 0; source_y < window->surface.height; ++source_y) {
            const ios_i32 target_y = window->y + source_y;
            if (target_y < dirty.y || target_y >= dirty.y + dirty.height
                || target_y < 0 || target_y >= screen->height) {
                continue;
            }
            for (ios_i32 source_x = 0; source_x < window->surface.width; ++source_x) {
                const ios_i32 target_x = window->x + source_x;
                if (target_x < dirty.x || target_x >= dirty.x + dirty.width
                    || target_x < 0 || target_x >= screen->width) {
                    continue;
                }
                screen->pixels[target_y * screen->stride + target_x]
                    = window->surface.pixels[source_y * window->surface.stride + source_x];
            }
        }
    }
}

static void render_pointer(
    struct test_surface *screen,
    ios_i32 x,
    ios_i32 y,
    ios_u32 color
)
{
    fill_rectangle(screen, (struct test_rect){ x, y, 1, 3 }, color);
    fill_rectangle(screen, (struct test_rect){ x, y, 3, 1 }, color);
}

static ios_status push_input(
    struct test_input_queue *queue,
    struct test_input_event event
)
{
    if (queue->count == TEST_INPUT_CAPACITY) {
        return IOS_ERROR(IOS_E_NO_SPACE);
    }
    queue->events[(queue->head + queue->count) % TEST_INPUT_CAPACITY] = event;
    ++queue->count;
    return IOS_OK;
}

static ios_status normalize_key(
    struct test_input_queue *queue,
    ios_u8 scan_code,
    bool pressed
)
{
    static const char key_map[] = "\0\x1b" "1234567890-=";
    ios_u32 key;
    if (scan_code >= sizeof(key_map) || key_map[scan_code] == '\0') {
        return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    }
    key = (ios_u8)key_map[scan_code];
    return push_input(queue, (struct test_input_event){
        .kind = TEST_INPUT_KEY, .key = key, .pressed = pressed
    });
}

static ios_status normalize_pointer_move(
    struct test_input_queue *queue,
    ios_i32 delta_x,
    ios_i32 delta_y
)
{
    queue->pointer_x += delta_x;
    queue->pointer_y -= delta_y;
    if (queue->pointer_x < 0) queue->pointer_x = 0;
    if (queue->pointer_y < 0) queue->pointer_y = 0;
    if (queue->pointer_x >= TEST_SCREEN_WIDTH) queue->pointer_x = TEST_SCREEN_WIDTH - 1;
    if (queue->pointer_y >= TEST_SCREEN_HEIGHT) queue->pointer_y = TEST_SCREEN_HEIGHT - 1;
    return push_input(queue, (struct test_input_event){
        .kind = TEST_INPUT_POINTER_MOVE, .x = queue->pointer_x, .y = queue->pointer_y
    });
}

static ios_status gui_start(
    const struct test_gui_dependencies *dependencies,
    struct test_gui_runtime *runtime
)
{
    *runtime = (struct test_gui_runtime){ .cui_usable = true };
    if (!dependencies->framebuffer_available) {
        runtime->diagnostic = "gui_unavailable: framebuffer";
        return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    }
    if (!dependencies->font_available) {
        runtime->diagnostic = "gui_unavailable: font";
        return IOS_ERROR(IOS_E_NOT_FOUND);
    }
    if (!dependencies->pointer_available) {
        runtime->diagnostic = "gui_unavailable: pointer";
        return IOS_ERROR(IOS_E_NOT_FOUND);
    }
    if (!dependencies->desktop_module_available || !dependencies->terminal_module_available) {
        runtime->diagnostic = "gui_unavailable: required module";
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    runtime->gui_running = true;
    runtime->desktop_present = true;
    runtime->terminal_present = true;
    runtime->explorer_present = dependencies->explorer_module_available;
    runtime->diagnostic = "gui_ready";
    return IOS_OK;
}

static void test_graphics_primitives_clip_and_honor_stride(void)
{
    ios_u32 pixels[TEST_SCREEN_STRIDE * TEST_SCREEN_HEIGHT];
    struct test_surface screen = {
        pixels, TEST_SCREEN_WIDTH, TEST_SCREEN_HEIGHT, TEST_SCREEN_STRIDE
    };
    ios_u8 glyph[8] = { 0x18, 0x24, 0x42, 0x7e, 0x42, 0x42, 0x42, 0 };
    memset(pixels, 0xcc, sizeof(pixels));
    fill_rectangle(&screen, (struct test_rect){ -2, -1, 5, 4 }, UINT32_C(0x00112233));
    IOS_TEST_ASSERT(pixels[0] == UINT32_C(0x00112233));
    IOS_TEST_ASSERT(pixels[2 + 2 * TEST_SCREEN_STRIDE] == UINT32_C(0x00112233));
    IOS_TEST_ASSERT(pixels[3] == UINT32_C(0xcccccccc));
    IOS_TEST_ASSERT(pixels[TEST_SCREEN_WIDTH] == UINT32_C(0xcccccccc));

    draw_border(&screen, (struct test_rect){ 4, 2, 5, 4 }, UINT32_C(0x00ffffff));
    IOS_TEST_ASSERT(pixels[4 + 2 * TEST_SCREEN_STRIDE] == UINT32_C(0x00ffffff));
    IOS_TEST_ASSERT(pixels[6 + 3 * TEST_SCREEN_STRIDE] == UINT32_C(0xcccccccc));
    draw_glyph_block(&screen, 3, 0, glyph, UINT32_C(0x0000ff00));
    IOS_TEST_ASSERT(pixels[6] == UINT32_C(0x0000ff00));
}

static void test_compositor_applies_z_order_clipping_dirty_regions_and_pointer(void)
{
    ios_u32 screen_pixels[TEST_SCREEN_STRIDE * TEST_SCREEN_HEIGHT] = { 0 };
    ios_u32 lower_pixels[6 * 4];
    ios_u32 upper_pixels[4 * 3];
    struct test_surface screen = {
        screen_pixels, TEST_SCREEN_WIDTH, TEST_SCREEN_HEIGHT, TEST_SCREEN_STRIDE
    };
    struct test_window windows[2] = {
        { { lower_pixels, 6, 4, 6 }, 2, 2, 1, true },
        { { upper_pixels, 4, 3, 4 }, 5, 3, 2, true }
    };
    for (ios_size index = 0; index < sizeof(lower_pixels) / sizeof(lower_pixels[0]); ++index) {
        lower_pixels[index] = UINT32_C(0x000000aa);
    }
    for (ios_size index = 0; index < sizeof(upper_pixels) / sizeof(upper_pixels[0]); ++index) {
        upper_pixels[index] = UINT32_C(0x0000aa00);
    }
    compose(
        &screen, windows, 2,
        (struct test_rect){ 0, 0, TEST_SCREEN_WIDTH, TEST_SCREEN_HEIGHT },
        UINT32_C(0x00101010)
    );
    IOS_TEST_ASSERT(screen_pixels[3 + 3 * TEST_SCREEN_STRIDE] == UINT32_C(0x000000aa));
    IOS_TEST_ASSERT(screen_pixels[6 + 4 * TEST_SCREEN_STRIDE] == UINT32_C(0x0000aa00));

    windows[1].x = -2;
    windows[1].y = -1;
    compose(&screen, windows, 2, (struct test_rect){ 0, 0, 2, 3 }, UINT32_C(0x00101010));
    IOS_TEST_ASSERT(screen_pixels[0] == UINT32_C(0x0000aa00));
    screen_pixels[10 + 7 * TEST_SCREEN_STRIDE] = UINT32_C(0x00abcdef);
    compose(&screen, windows, 2, (struct test_rect){ 0, 0, 2, 3 }, UINT32_C(0x00101010));
    IOS_TEST_ASSERT(screen_pixels[10 + 7 * TEST_SCREEN_STRIDE] == UINT32_C(0x00abcdef));
    render_pointer(&screen, 11, 7, UINT32_C(0x00ffffff));
    IOS_TEST_ASSERT(screen_pixels[11 + 7 * TEST_SCREEN_STRIDE] == UINT32_C(0x00ffffff));
}

static void test_keyboard_and_pointer_share_normalized_event_queue(void)
{
    struct test_input_queue queue = { 0 };
    IOS_TEST_ASSERT_STATUS(normalize_key(&queue, 2, true), IOS_OK);
    IOS_TEST_ASSERT_STATUS(normalize_pointer_move(&queue, 50, -50), IOS_OK);
    IOS_TEST_ASSERT_STATUS(push_input(&queue, (struct test_input_event){
        .kind = TEST_INPUT_POINTER_BUTTON, .x = queue.pointer_x, .y = queue.pointer_y,
        .button = 1, .pressed = true
    }), IOS_OK);
    IOS_TEST_ASSERT(queue.count == 3);
    IOS_TEST_ASSERT(queue.events[0].kind == TEST_INPUT_KEY && queue.events[0].key == '1');
    IOS_TEST_ASSERT(queue.events[1].kind == TEST_INPUT_POINTER_MOVE);
    IOS_TEST_ASSERT(queue.events[1].x == TEST_SCREEN_WIDTH - 1);
    IOS_TEST_ASSERT(queue.events[1].y == TEST_SCREEN_HEIGHT - 1);
    IOS_TEST_ASSERT(queue.events[2].kind == TEST_INPUT_POINTER_BUTTON);
}

static void test_gui_success_includes_desktop_terminal_and_optional_explorer(void)
{
    const struct test_gui_dependencies dependencies = {
        true, true, true, true, true, true
    };
    struct test_gui_runtime runtime;
    IOS_TEST_ASSERT_STATUS(gui_start(&dependencies, &runtime), IOS_OK);
    IOS_TEST_ASSERT(runtime.gui_running && runtime.cui_usable);
    IOS_TEST_ASSERT(runtime.desktop_present && runtime.terminal_present && runtime.explorer_present);
    IOS_TEST_ASSERT(strcmp(runtime.diagnostic, "gui_ready") == 0);
}

static void test_each_gui_initialization_failure_preserves_cui_recovery(void)
{
    struct test_gui_dependencies dependencies = { true, true, true, true, true, false };
    struct test_gui_runtime runtime;
    IOS_TEST_ASSERT_STATUS(gui_start(&dependencies, &runtime), IOS_OK);
    IOS_TEST_ASSERT(runtime.gui_running && !runtime.explorer_present);

    for (ios_size failure = 0; failure < 4; ++failure) {
        dependencies = (struct test_gui_dependencies){ true, true, true, true, true, true };
        if (failure == 0) dependencies.framebuffer_available = false;
        if (failure == 1) dependencies.font_available = false;
        if (failure == 2) dependencies.pointer_available = false;
        if (failure == 3) dependencies.terminal_module_available = false;
        IOS_TEST_ASSERT(IOS_FAILED(gui_start(&dependencies, &runtime)));
        IOS_TEST_ASSERT(!runtime.gui_running && runtime.cui_usable);
        IOS_TEST_ASSERT(runtime.diagnostic != NULL);
        IOS_TEST_ASSERT(strncmp(runtime.diagnostic, "gui_unavailable:", 16) == 0);
    }
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_graphics_primitives_clip_and_honor_stride),
    IOS_TEST_CASE(test_compositor_applies_z_order_clipping_dirty_regions_and_pointer),
    IOS_TEST_CASE(test_keyboard_and_pointer_share_normalized_event_queue),
    IOS_TEST_CASE(test_gui_success_includes_desktop_terminal_and_optional_explorer),
    IOS_TEST_CASE(test_each_gui_initialization_failure_preserves_cui_recovery)
};

const size_t ios_test_case_count = sizeof(ios_test_cases) / sizeof(ios_test_cases[0]);
