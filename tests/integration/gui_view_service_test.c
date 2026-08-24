#include <inferenceos/test.h>

#include <inferenceos/gui_view.h>

#include <string.h>

enum { SCREEN_WIDTH = 8, SCREEN_HEIGHT = 6 };

static const struct ios_process *resolved_process;

_Noreturn void ios_assertion_failed(
    const char *expression, const char *source_file, ios_u32 source_line
)
{
    ios_test_fail(expression, source_file, source_line);
}

ios_u64 x86_64_interrupt_save_disable(void)
{
    return 0;
}

void x86_64_interrupt_restore(ios_u64 previous_flags)
{
    (void)previous_flags;
}

ios_size process_count(void)
{
    return 0;
}

const struct ios_process *process_at(ios_size index)
{
    (void)index;
    return NULL;
}

static const struct ios_process *resolve_process(
    void *context, ios_u64 process_id, ios_u64 application_identity
)
{
    (void)context;
    return resolved_process != NULL && resolved_process->process_id == process_id
        && resolved_process->application_identity == application_identity
        ? resolved_process : NULL;
}

static void initialize_process(
    struct ios_process *process, ios_u64 process_id, ios_u64 application_identity
)
{
    memset(process, 0, sizeof(*process));
    process->process_id = process_id;
    process->application_identity = application_identity;
    process->state = IOS_PROCESS_RUNNABLE;
    IOS_TEST_ASSERT_STATUS(handle_table_initialize(&process->handles, process_id), IOS_OK);
}

static void initialize_manager(
    struct ios_window_manager *manager,
    ios_u32 framebuffer[SCREEN_WIDTH * SCREEN_HEIGHT],
    ios_u32 shadow[SCREEN_WIDTH * SCREEN_HEIGHT]
)
{
    memset(framebuffer, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(*framebuffer));
    memset(shadow, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(*shadow));
    IOS_TEST_ASSERT_STATUS(
        ios_window_manager_initialize(
            manager,
            (struct ios_graphics_surface){
                framebuffer, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_WIDTH
            },
            (struct ios_graphics_surface){ shadow, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_WIDTH },
            UINT32_C(0x00101010), UINT32_C(0x00ffffff)
        ), IOS_OK);
}

static void grant_target(
    struct ios_process *process,
    struct ios_gui_view_target *target,
    ios_handle *window_handle,
    ios_handle *view_handle
)
{
    IOS_TEST_ASSERT_STATUS(
        handle_table_insert(
            &process->handles, target, IOS_OBJECT_WINDOW, IOS_RIGHT_WRITE,
            NULL, NULL, window_handle
        ), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        handle_table_insert(
            &process->handles, target, IOS_OBJECT_CONTENT, IOS_RIGHT_READ,
            NULL, NULL, view_handle
        ), IOS_OK);
}

static void test_owned_capabilities_render_only_through_managed_composition(void)
{
    ios_u32 framebuffer[SCREEN_WIDTH * SCREEN_HEIGHT];
    ios_u32 shadow[SCREEN_WIDTH * SCREEN_HEIGHT];
    ios_u32 client_pixels[4] = {
        UINT32_C(0x000000aa), UINT32_C(0x000000aa),
        UINT32_C(0x000000aa), UINT32_C(0x000000aa)
    };
    struct ios_process process;
    struct ios_window_manager manager;
    struct ios_window_handle native_window;
    struct ios_gui_view_target target;
    struct ios_gui_view_service service;
    struct ios_shell_gui_view_reply reply = { 0 };
    ios_handle window_handle;
    ios_handle view_handle;
    initialize_process(&process, 7, UINT64_C(0x46494c455850));
    initialize_manager(&manager, framebuffer, shadow);
    IOS_TEST_ASSERT_STATUS(
        ios_window_create(
            &manager, (struct ios_graphics_surface){ client_pixels, 2, 2, 2 },
            2, 1, (ios_u32)process.process_id, &native_window
        ), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_gui_view_target_initialize(
            &target, &manager, native_window,
            process.process_id, process.application_identity
        ), IOS_OK);
    grant_target(&process, &target, &window_handle, &view_handle);
    IOS_TEST_ASSERT_STATUS(
        ios_gui_view_service_initialize(&service, resolve_process, NULL), IOS_OK);
    resolved_process = &process;
    const struct ios_shell_gui_view_request request = {
        .size = sizeof(request),
        .version = IOS_SHELL_PROTOCOL_VERSION,
        .window_handle = window_handle,
        .view_handle = view_handle,
        .render_sequence = 1
    };
    IOS_TEST_ASSERT_STATUS(
        ios_gui_view_dispatch(
            &service, process.process_id, process.application_identity, &request, &reply
        ), IOS_OK);
    IOS_TEST_ASSERT(reply.render_sequence == 1 && target.last_render_sequence == 1);
    IOS_TEST_ASSERT(framebuffer[2 + SCREEN_WIDTH] == UINT32_C(0x000000aa));
    IOS_TEST_ASSERT(!manager.dirty_valid);
}

static void test_cross_target_and_cross_process_capabilities_are_rejected(void)
{
    ios_u32 framebuffer[SCREEN_WIDTH * SCREEN_HEIGHT];
    ios_u32 shadow[SCREEN_WIDTH * SCREEN_HEIGHT];
    ios_u32 pixels[2] = { 1, 2 };
    struct ios_process owner;
    struct ios_process attacker;
    struct ios_window_manager manager;
    struct ios_window_handle first_window;
    struct ios_window_handle second_window;
    struct ios_gui_view_target first_target;
    struct ios_gui_view_target second_target;
    struct ios_gui_view_service service;
    struct ios_shell_gui_view_reply reply = { 0 };
    ios_handle first_window_handle;
    ios_handle first_view_handle;
    ios_handle second_window_handle;
    ios_handle second_view_handle;
    initialize_process(&owner, 11, 101);
    initialize_process(&attacker, 12, 102);
    initialize_manager(&manager, framebuffer, shadow);
    IOS_TEST_ASSERT_STATUS(
        ios_window_create(
            &manager, (struct ios_graphics_surface){ pixels, 1, 1, 1 },
            0, 0, 11, &first_window
        ), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_window_create(
            &manager, (struct ios_graphics_surface){ pixels + 1, 1, 1, 1 },
            1, 0, 11, &second_window
        ), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_gui_view_target_initialize(&first_target, &manager, first_window, 11, 101), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_gui_view_target_initialize(&second_target, &manager, second_window, 11, 101), IOS_OK);
    grant_target(&owner, &first_target, &first_window_handle, &first_view_handle);
    grant_target(&owner, &second_target, &second_window_handle, &second_view_handle);
    IOS_TEST_ASSERT_STATUS(
        ios_gui_view_service_initialize(&service, resolve_process, NULL), IOS_OK);
    resolved_process = &owner;
    struct ios_shell_gui_view_request request = {
        .size = sizeof(request),
        .version = IOS_SHELL_PROTOCOL_VERSION,
        .window_handle = first_window_handle,
        .view_handle = second_view_handle,
        .render_sequence = 1
    };
    IOS_TEST_ASSERT_STATUS(
        ios_gui_view_dispatch(&service, 11, 101, &request, &reply),
        IOS_ERROR(IOS_E_ACCESS_DENIED));
    resolved_process = &attacker;
    request.view_handle = first_view_handle;
    IOS_TEST_ASSERT_STATUS(
        ios_gui_view_dispatch(&service, 12, 102, &request, &reply),
        IOS_ERROR(IOS_E_BAD_HANDLE));
    (void)second_window_handle;
}

static void test_replayed_sequence_and_stale_native_window_are_rejected(void)
{
    ios_u32 framebuffer[SCREEN_WIDTH * SCREEN_HEIGHT];
    ios_u32 shadow[SCREEN_WIDTH * SCREEN_HEIGHT];
    ios_u32 pixel = 3;
    struct ios_process process;
    struct ios_window_manager manager;
    struct ios_window_handle native_window;
    struct ios_gui_view_target target;
    struct ios_gui_view_service service;
    struct ios_shell_gui_view_reply reply = { 0 };
    ios_handle window_handle;
    ios_handle view_handle;
    initialize_process(&process, 21, 201);
    initialize_manager(&manager, framebuffer, shadow);
    IOS_TEST_ASSERT_STATUS(
        ios_window_create(
            &manager, (struct ios_graphics_surface){ &pixel, 1, 1, 1 },
            0, 0, 21, &native_window
        ), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_gui_view_target_initialize(&target, &manager, native_window, 21, 201), IOS_OK);
    grant_target(&process, &target, &window_handle, &view_handle);
    IOS_TEST_ASSERT_STATUS(
        ios_gui_view_service_initialize(&service, resolve_process, NULL), IOS_OK);
    resolved_process = &process;
    struct ios_shell_gui_view_request request = {
        .size = sizeof(request),
        .version = IOS_SHELL_PROTOCOL_VERSION,
        .window_handle = window_handle,
        .view_handle = view_handle,
        .render_sequence = 1
    };
    IOS_TEST_ASSERT_STATUS(
        ios_gui_view_dispatch(&service, 21, 201, &request, &reply), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_gui_view_dispatch(&service, 21, 201, &request, &reply),
        IOS_ERROR(IOS_E_INVALID_STATE));
    IOS_TEST_ASSERT_STATUS(ios_window_destroy(&manager, native_window), IOS_OK);
    request.render_sequence = 2;
    IOS_TEST_ASSERT_STATUS(
        ios_gui_view_dispatch(&service, 21, 201, &request, &reply),
        IOS_ERROR(IOS_E_BAD_HANDLE));
    IOS_TEST_ASSERT(target.last_render_sequence == 1);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_owned_capabilities_render_only_through_managed_composition),
    IOS_TEST_CASE(test_cross_target_and_cross_process_capabilities_are_rejected),
    IOS_TEST_CASE(test_replayed_sequence_and_stale_native_window_are_rejected)
};

const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
