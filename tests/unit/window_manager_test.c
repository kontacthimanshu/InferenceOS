#include <inferenceos/test.h>

#include <inferenceos/gui/window.h>

#include <string.h>

enum {
    SCREEN_WIDTH = 12,
    SCREEN_HEIGHT = 8,
    SCREEN_STRIDE = 16
};

static void fill_pixels(ios_u32 *pixels, ios_size count, ios_u32 color)
{
    for (ios_size index = 0; index < count; ++index) pixels[index] = color;
}

static void test_composition_honors_z_order_clipping_dirty_regions_and_pointer(void)
{
    ios_u32 framebuffer_pixels[SCREEN_STRIDE * SCREEN_HEIGHT];
    ios_u32 shadow_pixels[SCREEN_STRIDE * SCREEN_HEIGHT];
    ios_u32 lower_pixels[6 * 4];
    ios_u32 upper_pixels[4 * 3];
    struct ios_window_manager manager;
    struct ios_window_handle lower;
    struct ios_window_handle upper;

    memset(framebuffer_pixels, 0, sizeof(framebuffer_pixels));
    memset(shadow_pixels, 0, sizeof(shadow_pixels));
    fill_pixels(lower_pixels, IOS_ARRAY_COUNT(lower_pixels), UINT32_C(0x000000aa));
    fill_pixels(upper_pixels, IOS_ARRAY_COUNT(upper_pixels), UINT32_C(0x0000aa00));
    IOS_TEST_ASSERT_STATUS(ios_window_manager_initialize(
        &manager,
        (struct ios_graphics_surface){ framebuffer_pixels, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_STRIDE },
        (struct ios_graphics_surface){ shadow_pixels, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_STRIDE },
        UINT32_C(0x00101010), UINT32_C(0x00ffffff)
    ), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_window_create(
        &manager, (struct ios_graphics_surface){ lower_pixels, 6, 4, 6 }, 2, 2, 1, &lower
    ), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_window_create(
        &manager, (struct ios_graphics_surface){ upper_pixels, 4, 3, 4 }, 5, 3, 2, &upper
    ), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_window_compose(&manager), IOS_OK);
    IOS_TEST_ASSERT(framebuffer_pixels[3 + 3 * SCREEN_STRIDE] == UINT32_C(0x000000aa));
    IOS_TEST_ASSERT(framebuffer_pixels[6 + 4 * SCREEN_STRIDE] == UINT32_C(0x0000aa00));
    IOS_TEST_ASSERT(manager.windows[upper.slot].focused);

    IOS_TEST_ASSERT_STATUS(ios_window_raise(&manager, lower), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_window_compose(&manager), IOS_OK);
    IOS_TEST_ASSERT(framebuffer_pixels[6 + 4 * SCREEN_STRIDE] == UINT32_C(0x000000aa));
    IOS_TEST_ASSERT(manager.windows[lower.slot].focused);

    framebuffer_pixels[10 + 7 * SCREEN_STRIDE] = UINT32_C(0x00abcdef);
    IOS_TEST_ASSERT_STATUS(ios_window_invalidate(
        &manager, lower, (struct ios_graphics_rect){ 0, 0, 1, 1 }
    ), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_window_compose(&manager), IOS_OK);
    IOS_TEST_ASSERT(framebuffer_pixels[10 + 7 * SCREEN_STRIDE] == UINT32_C(0x00abcdef));

    IOS_TEST_ASSERT_STATUS(ios_window_move(&manager, upper, -2, -1), IOS_OK);
    ios_window_set_pointer(&manager, 11, 7, true);
    IOS_TEST_ASSERT_STATUS(ios_window_compose(&manager), IOS_OK);
    IOS_TEST_ASSERT(framebuffer_pixels[0] == UINT32_C(0x0000aa00));
    IOS_TEST_ASSERT(framebuffer_pixels[11 + 7 * SCREEN_STRIDE] == UINT32_C(0x00ffffff));
}

static void test_lifecycle_rejects_stale_handles_and_repaints_exposed_area(void)
{
    ios_u32 framebuffer_pixels[SCREEN_WIDTH * SCREEN_HEIGHT];
    ios_u32 shadow_pixels[SCREEN_WIDTH * SCREEN_HEIGHT];
    ios_u32 client_pixels[2 * 2];
    struct ios_window_manager manager;
    struct ios_window_handle old_handle;
    struct ios_window_handle new_handle;

    fill_pixels(client_pixels, IOS_ARRAY_COUNT(client_pixels), UINT32_C(0x000000cc));
    IOS_TEST_ASSERT_STATUS(ios_window_manager_initialize(
        &manager,
        (struct ios_graphics_surface){ framebuffer_pixels, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_WIDTH },
        (struct ios_graphics_surface){ shadow_pixels, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_WIDTH },
        UINT32_C(0x00101010), UINT32_C(0x00ffffff)
    ), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_window_create(
        &manager, (struct ios_graphics_surface){ client_pixels, 2, 2, 2 }, 1, 1, 7, &old_handle
    ), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_window_compose(&manager), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_window_destroy(&manager, old_handle), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_window_compose(&manager), IOS_OK);
    IOS_TEST_ASSERT(framebuffer_pixels[1 + SCREEN_WIDTH] == UINT32_C(0x00101010));
    IOS_TEST_ASSERT_STATUS(
        ios_window_move(&manager, old_handle, 3, 3), IOS_ERROR(IOS_E_BAD_HANDLE)
    );
    IOS_TEST_ASSERT_STATUS(ios_window_create(
        &manager, (struct ios_graphics_surface){ client_pixels, 2, 2, 2 }, 3, 3, 7, &new_handle
    ), IOS_OK);
    IOS_TEST_ASSERT(new_handle.slot == old_handle.slot);
    IOS_TEST_ASSERT(new_handle.generation != old_handle.generation);
    IOS_TEST_ASSERT_STATUS(ios_window_set_visible(&manager, new_handle, false), IOS_OK);
    IOS_TEST_ASSERT(!manager.windows[new_handle.slot].focused);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_composition_honors_z_order_clipping_dirty_regions_and_pointer),
    IOS_TEST_CASE(test_lifecycle_rejects_stale_handles_and_repaints_exposed_area)
};

const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
