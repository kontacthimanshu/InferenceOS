#include <inferenceos/runtime.h>
#include <inferenceos/test.h>
#include <inferenceos/txt_viewer.h>

struct txt_view_observation {
    enum ios_shell_operation operation;
    ios_u64 directory_handle;
    ios_type_icon_capability type_capability;
    ios_status view_status;
    ios_size calls;
};

static ios_status resolve_icon(
    void *context,
    ios_u64 type_icon_capability,
    ios_u32 object_kind,
    enum ios_presentation_icon *icon
)
{
    struct txt_view_observation *observation = context;
    if (observation == NULL || icon == NULL
        || (object_kind == IOS_DISPLAY_SAFE_REGULAR_FILE
            && type_icon_capability != observation->type_capability)
        || (object_kind == IOS_DISPLAY_SAFE_DIRECTORY
            && type_icon_capability != IOS_INVALID_TYPE_ICON_CAPABILITY)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *icon = object_kind == IOS_DISPLAY_SAFE_DIRECTORY ? IOS_ICON_FOLDER : IOS_ICON_TEXT;
    return IOS_OK;
}

ios_status ios_file_explorer_client_initialize(
    struct ios_file_explorer_client *client,
    struct ios_process *process,
    struct ios_shell_service *shell_service,
    ios_file_explorer_resolve_icon_function icon_resolver,
    void *resolve_icon_context
)
{
    if (client == NULL || process == NULL || shell_service == NULL || icon_resolver == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *client = (struct ios_file_explorer_client){
        .process = process,
        .shell_service = shell_service,
        .resolve_icon = icon_resolver,
        .resolve_icon_context = resolve_icon_context,
        .connected = true
    };
    return IOS_OK;
}

void ios_file_explorer_client_disconnect(struct ios_file_explorer_client *client)
{
    if (client != NULL) client->connected = false;
}

ios_status ios_file_explorer_client_file_view(
    struct ios_file_explorer_client *client,
    enum ios_shell_operation operation,
    ios_u64 directory_handle,
    ios_type_icon_capability type_icon_capability,
    struct ios_display_safe_entry *entries,
    ios_size capacity,
    ios_size *entry_count
)
{
    struct txt_view_observation *observation = client->resolve_icon_context;
    if (observation == NULL || entries == NULL || entry_count == NULL || capacity < 2) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    observation->operation = operation;
    observation->directory_handle = directory_handle;
    observation->type_capability = type_icon_capability;
    ++observation->calls;
    if (IOS_FAILED(observation->view_status)) return observation->view_status;
    if (directory_handle == 33) {
        entries[0] = (struct ios_display_safe_entry){
            .size = sizeof(struct ios_display_safe_entry),
            .version = IOS_DISPLAY_SAFE_ENTRY_VERSION,
            .object_handle = 34,
            .type_icon_capability = type_icon_capability,
            .byte_size = 40,
            .allowed_operations = IOS_DISPLAY_SAFE_OPERATION_OPEN
                | IOS_DISPLAY_SAFE_OPERATION_READ,
            .object_kind = IOS_DISPLAY_SAFE_REGULAR_FILE,
            .display_name_length = 5,
            .display_name = "DAILY"
        };
        *entry_count = 1;
        return IOS_OK;
    }
    if (directory_handle != 1) return IOS_ERROR(IOS_E_NOT_FOUND);
    entries[0] = (struct ios_display_safe_entry){
        .size = sizeof(struct ios_display_safe_entry),
        .version = IOS_DISPLAY_SAFE_ENTRY_VERSION,
        .object_handle = 31,
        .type_icon_capability = type_icon_capability,
        .byte_size = 300,
        .allowed_operations = IOS_DISPLAY_SAFE_OPERATION_OPEN | IOS_DISPLAY_SAFE_OPERATION_READ,
        .object_kind = IOS_DISPLAY_SAFE_REGULAR_FILE,
        .display_name_length = 5,
        .display_name = "NOTES"
    };
    entries[1] = (struct ios_display_safe_entry){
        .size = sizeof(struct ios_display_safe_entry),
        .version = IOS_DISPLAY_SAFE_ENTRY_VERSION,
        .object_handle = 33,
        .type_icon_capability = IOS_INVALID_TYPE_ICON_CAPABILITY,
        .allowed_operations = IOS_DISPLAY_SAFE_OPERATION_ENUMERATE,
        .object_kind = IOS_DISPLAY_SAFE_DIRECTORY,
        .display_name_length = 4,
        .display_name = "LOGS"
    };
    *entry_count = 2;
    return IOS_OK;
}

static void initialize_process(struct ios_process *process)
{
    memset(process, 0, sizeof(*process));
    process->process_id = 18;
    process->application_identity = IOS_TXT_VIEWER_APPLICATION_ID;
}

static void test_txt_viewer_explores_folders_and_requests_only_the_txt_type(void)
{
    enum { WIDTH = 640, HEIGHT = 220, GLYPH_BYTES = 128 * 16 };
    static ios_u32 pixels[WIDTH * HEIGHT];
    static ios_u8 glyphs[GLYPH_BYTES];
    struct ios_graphics_surface surface = { pixels, WIDTH, HEIGHT, WIDTH };
    struct ios_psf2_font font = {
        glyphs, sizeof(glyphs), 128, 16, 8, 16, IOS_RASTER_FONT_MONO1
    };
    struct txt_view_observation observation = {
        .type_capability = UINT64_C(0xabc00002),
        .view_status = IOS_OK
    };
    struct ios_process process;
    struct ios_shell_service shell = { 0 };
    struct ios_txt_viewer_application application;
    bool activated;
    ios_u64 activated_handle;
    const struct ios_input_event click = {
        .type = IOS_INPUT_EVENT_POINTER_BUTTON,
        .flags = IOS_INPUT_PRESSED,
        .code = IOS_POINTER_BUTTON_LEFT,
        .x = IOS_FILE_EXPLORER_CELL_WIDTH + 1,
        .y = IOS_FILE_EXPLORER_HEADER_HEIGHT + 1,
        .timestamp_ticks = 100
    };
    const struct ios_input_event enter = {
        .type = IOS_INPUT_EVENT_KEY,
        .flags = IOS_INPUT_PRESSED,
        .code = IOS_KEY_ENTER
    };
    const struct ios_input_event back = {
        .type = IOS_INPUT_EVENT_KEY,
        .flags = IOS_INPUT_PRESSED,
        .code = IOS_KEY_BACKSPACE
    };

    initialize_process(&process);
    IOS_TEST_ASSERT_STATUS(
        ios_txt_viewer_initialize(
            &application,
            &process,
            &shell,
            resolve_icon,
            &observation,
            1,
            observation.type_capability,
            surface,
            &font
        ),
        IOS_OK
    );
    IOS_TEST_ASSERT(observation.calls == 1);
    IOS_TEST_ASSERT(observation.operation == IOS_SHELL_TYPE_VIEW);
    IOS_TEST_ASSERT(observation.directory_handle == 1);
    IOS_TEST_ASSERT(observation.type_capability == application.txt_type_capability);
    IOS_TEST_ASSERT(application.model.entry_count == 2);
    IOS_TEST_ASSERT(strcmp(application.model.entries[0].display_name, "NOTES") == 0);
    IOS_TEST_ASSERT(strchr(application.model.entries[0].display_name, '.') == NULL);
    IOS_TEST_ASSERT(application.model.entries[1].object_kind == IOS_DISPLAY_SAFE_DIRECTORY);
    IOS_TEST_ASSERT(strcmp(application.window.title, "TXT Files App") == 0);
    IOS_TEST_ASSERT(strcmp(application.window.location, "Folder: /") == 0);
    IOS_TEST_ASSERT_STATUS(ios_txt_viewer_render(&application), IOS_OK);

    IOS_TEST_ASSERT_STATUS(
        ios_txt_viewer_handle_input(
            &application, &click, &activated, &activated_handle
        ), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_txt_viewer_handle_input(
            &application, &enter, &activated, &activated_handle
        ), IOS_OK);
    IOS_TEST_ASSERT(activated && activated_handle == 33);
    IOS_TEST_ASSERT(observation.calls == 2 && observation.directory_handle == 33);
    IOS_TEST_ASSERT(application.model.directory_handle == 33);
    IOS_TEST_ASSERT(strcmp(application.model.entries[0].display_name, "DAILY") == 0);
    IOS_TEST_ASSERT(strcmp(application.window.location, "Folder: LOGS") == 0);

    IOS_TEST_ASSERT_STATUS(
        ios_txt_viewer_handle_input(
            &application, &back, &activated, &activated_handle
        ), IOS_OK);
    IOS_TEST_ASSERT(observation.calls == 3 && application.model.directory_handle == 1);
    IOS_TEST_ASSERT(strcmp(application.window.location, "Folder: /") == 0);
    ios_txt_viewer_disconnect(&application);
    IOS_TEST_ASSERT(!application.client.connected);
}

static void test_txt_viewer_opens_empty_when_the_root_mount_is_unavailable(void)
{
    enum { WIDTH = 640, HEIGHT = 220, GLYPH_BYTES = 128 * 16 };
    static ios_u32 pixels[WIDTH * HEIGHT];
    static ios_u8 glyphs[GLYPH_BYTES];
    struct ios_graphics_surface surface = { pixels, WIDTH, HEIGHT, WIDTH };
    struct ios_psf2_font font = {
        glyphs, sizeof(glyphs), 128, 16, 8, 16, IOS_RASTER_FONT_MONO1
    };
    struct txt_view_observation observation = {
        .type_capability = UINT64_C(0xabc00002),
        .view_status = IOS_ERROR(IOS_E_NOT_FOUND)
    };
    struct ios_process process;
    struct ios_shell_service shell = { 0 };
    struct ios_txt_viewer_application application;

    initialize_process(&process);
    IOS_TEST_ASSERT_STATUS(
        ios_txt_viewer_initialize(
            &application,
            &process,
            &shell,
            resolve_icon,
            &observation,
            1,
            observation.type_capability,
            surface,
            &font
        ),
        IOS_OK
    );
    IOS_TEST_ASSERT(observation.operation == IOS_SHELL_TYPE_VIEW);
    IOS_TEST_ASSERT(application.model.entry_count == 0);
    IOS_TEST_ASSERT(application.model.directory_handle == 1);
    IOS_TEST_ASSERT_STATUS(ios_txt_viewer_render(&application), IOS_OK);
    ios_txt_viewer_disconnect(&application);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_txt_viewer_explores_folders_and_requests_only_the_txt_type),
    IOS_TEST_CASE(test_txt_viewer_opens_empty_when_the_root_mount_is_unavailable)
};

const size_t ios_test_case_count = sizeof(ios_test_cases) / sizeof(ios_test_cases[0]);
