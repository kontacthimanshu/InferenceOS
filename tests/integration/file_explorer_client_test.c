#include <inferenceos/test.h>

#include <inferenceos/arch/interrupts.h>
#include <inferenceos/gui/file_explorer.h>

#include <string.h>

struct broker_backend {
    ios_u64 text_capability;
    ios_size entry_count;
    ios_size file_calls;
    ios_size gui_calls;
    ios_u32 revision;
    ios_status failure;
};

static const char *const first_names[] = {
    "ALPHA", "BRAVO", "CHARLIE", "DELTA", "ECHO", "FOXTROT"
};

static const char *const second_names[] = {
    "GOLF", "HOTEL", "INDIA", "JULIET", "KILO", "LIMA"
};

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

ios_status scheduler_block_current(struct ios_wait_queue *queue)
{
    (void)queue;
    return IOS_ERROR(IOS_E_WOULD_BLOCK);
}

void wait_queue_initialize(struct ios_wait_queue *queue)
{
    if (queue != NULL) *queue = (struct ios_wait_queue){ 0 };
}

struct ios_scheduler_task *wait_queue_wake_one(struct ios_wait_queue *queue)
{
    (void)queue;
    return NULL;
}

ios_size wait_queue_wake_all(struct ios_wait_queue *queue)
{
    (void)queue;
    return 0;
}

static ios_status dispatch_file_view(
    void *context,
    ios_u64 caller_process_id,
    ios_u64 caller_application_identity,
    enum ios_shell_operation operation,
    const struct ios_shell_file_view_request *request,
    struct ios_shell_file_view_reply *reply
)
{
    struct broker_backend *backend = context;
    ios_size source = (ios_size)request->continuation;
    ++backend->file_calls;
    IOS_TEST_ASSERT(caller_process_id == 20);
    IOS_TEST_ASSERT(caller_application_identity == UINT64_C(0x46494c455850));
    if (backend->failure != IOS_OK) return backend->failure;
    while (source < backend->entry_count && reply->item_count < request->maximum_items) {
        const bool is_text = source % 2U == 0;
        const bool include = operation == IOS_SHELL_DIRECTORY_VIEW
            || (is_text && request->type_icon_capability == backend->text_capability);
        if (include) {
            const struct ios_display_safe_source_entry safe = {
                .base_name = backend->revision == 1
                    ? first_names[source] : second_names[source],
                .object_handle = source + 1U,
                .type_icon_capability = is_text ? backend->text_capability : UINT64_C(0x9999),
                .byte_size = 10 + source,
                .allowed_operations = IOS_DISPLAY_SAFE_OPERATION_OPEN,
                .object_kind = IOS_DISPLAY_SAFE_REGULAR_FILE
            };
            IOS_TEST_ASSERT_STATUS(
                ios_display_safe_entry_convert(&safe, &reply->entries[reply->item_count]),
                IOS_OK);
            ++reply->item_count;
        }
        ++source;
    }
    reply->continuation = source < backend->entry_count ? source : 0;
    return IOS_OK;
}

static ios_status dispatch_gui_view(
    void *context,
    ios_u64 caller_process_id,
    ios_u64 caller_application_identity,
    const struct ios_shell_gui_view_request *request,
    struct ios_shell_gui_view_reply *reply
)
{
    struct broker_backend *backend = context;
    ++backend->gui_calls;
    IOS_TEST_ASSERT(caller_process_id == 20);
    IOS_TEST_ASSERT(caller_application_identity == UINT64_C(0x46494c455850));
    IOS_TEST_ASSERT(request->window_handle == 31 && request->view_handle == 33);
    reply->render_sequence = request->render_sequence;
    return backend->failure;
}

static ios_status resolve_icon(
    void *context,
    ios_u64 type_icon_capability,
    ios_u32 object_kind,
    enum ios_presentation_icon *icon
)
{
    const struct broker_backend *backend = context;
    if (object_kind == IOS_DISPLAY_SAFE_DIRECTORY) {
        *icon = IOS_ICON_FOLDER;
    } else {
        *icon = type_icon_capability == backend->text_capability
            ? IOS_ICON_TEXT : IOS_ICON_GENERIC_FILE;
    }
    return IOS_OK;
}

static void initialize_stack(
    struct ios_process *shell_process,
    struct ios_process *file_explorer_process,
    struct ios_shell_service *shell_service,
    struct ios_file_explorer_client *client,
    struct broker_backend *backend
)
{
    initialize_process(shell_process, 10, UINT64_C(0x5348454c4c));
    initialize_process(file_explorer_process, 20, UINT64_C(0x46494c455850));
    *backend = (struct broker_backend){
        .text_capability = UINT64_C(0x77770001),
        .entry_count = 6,
        .revision = 1
    };
    *shell_service = (struct ios_shell_service){ 0 };
    IOS_TEST_ASSERT_STATUS(ipc_initialize(), IOS_OK);
    const struct ios_shell_service_config config = {
        .process = shell_process,
        .queue_depth = 4,
        .dispatch_file_view = dispatch_file_view,
        .dispatch_gui_view = dispatch_gui_view,
        .dispatch_context = backend
    };
    IOS_TEST_ASSERT_STATUS(ios_shell_service_start(shell_service, &config), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_file_explorer_client_initialize(
            client, file_explorer_process, shell_service, resolve_icon, backend
        ), IOS_OK);
}

static void test_model_initialization_paginates_through_shell_ipc(void)
{
    struct ios_process shell_process;
    struct ios_process file_explorer_process;
    struct ios_shell_service shell_service;
    struct ios_file_explorer_client client;
    struct ios_file_explorer_model model;
    struct broker_backend backend;
    initialize_stack(
        &shell_process, &file_explorer_process, &shell_service, &client, &backend);
    IOS_TEST_ASSERT_STATUS(
        ios_file_explorer_model_initialize(
            &model, ios_file_explorer_client_provider(&client), 1
        ), IOS_OK);
    IOS_TEST_ASSERT(model.entry_count == 6);
    IOS_TEST_ASSERT(backend.file_calls == 2);
    IOS_TEST_ASSERT(strcmp(model.entries[0].display_name, "ALPHA") == 0);
    IOS_TEST_ASSERT(strcmp(model.entries[5].display_name, "FOXTROT") == 0);
    IOS_TEST_ASSERT(client.shell_generation == shell_service.generation);
    ios_file_explorer_client_disconnect(&client);
    IOS_TEST_ASSERT_STATUS(ios_shell_service_stop(&shell_service), IOS_OK);
}

static void test_refresh_reconnects_after_restart_and_is_failure_atomic(void)
{
    struct ios_process shell_process;
    struct ios_process file_explorer_process;
    struct ios_shell_service shell_service;
    struct ios_file_explorer_client client;
    struct ios_file_explorer_model model;
    struct broker_backend backend;
    initialize_stack(
        &shell_process, &file_explorer_process, &shell_service, &client, &backend);
    IOS_TEST_ASSERT_STATUS(
        ios_file_explorer_model_initialize(
            &model, ios_file_explorer_client_provider(&client), 1
        ), IOS_OK);
    const ios_handle stale_channel = client.shell_channel;
    const ios_u64 old_generation = client.shell_generation;
    backend.revision = 2;
    IOS_TEST_ASSERT_STATUS(ios_shell_service_restart(&shell_service), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_file_explorer_client_refresh(&client, &model), IOS_OK);
    IOS_TEST_ASSERT(client.shell_generation != old_generation);
    IOS_TEST_ASSERT(client.shell_channel != stale_channel);
    IOS_TEST_ASSERT(strcmp(model.entries[0].display_name, "GOLF") == 0);
    backend.failure = IOS_ERROR(IOS_E_IO);
    IOS_TEST_ASSERT_STATUS(
        ios_file_explorer_client_refresh(&client, &model), IOS_ERROR(IOS_E_IO));
    IOS_TEST_ASSERT(model.entry_count == 6);
    IOS_TEST_ASSERT(strcmp(model.entries[0].display_name, "GOLF") == 0);
    ios_file_explorer_client_disconnect(&client);
    IOS_TEST_ASSERT_STATUS(ios_shell_service_stop(&shell_service), IOS_OK);
}

static void test_type_search_and_render_use_versioned_shell_operations(void)
{
    struct ios_process shell_process;
    struct ios_process file_explorer_process;
    struct ios_shell_service shell_service;
    struct ios_file_explorer_client client;
    struct broker_backend backend;
    struct ios_display_safe_entry entries[6];
    ios_size entry_count;
    initialize_stack(
        &shell_process, &file_explorer_process, &shell_service, &client, &backend);
    IOS_TEST_ASSERT_STATUS(
        ios_file_explorer_client_file_view(
            &client, IOS_SHELL_TYPE_VIEW, 1, backend.text_capability,
            entries, IOS_ARRAY_COUNT(entries), &entry_count
        ), IOS_OK);
    IOS_TEST_ASSERT(entry_count == 3);
    IOS_TEST_ASSERT(strcmp(entries[0].display_name, "ALPHA") == 0);
    IOS_TEST_ASSERT(strcmp(entries[2].display_name, "ECHO") == 0);
    IOS_TEST_ASSERT_STATUS(
        ios_file_explorer_client_file_view(
            &client, IOS_SHELL_SEARCH, 1, backend.text_capability,
            entries, IOS_ARRAY_COUNT(entries), &entry_count
        ), IOS_OK);
    IOS_TEST_ASSERT(entry_count == 3);
    IOS_TEST_ASSERT_STATUS(ios_file_explorer_client_render(&client, 31, 33), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_file_explorer_client_render(&client, 31, 33), IOS_OK);
    IOS_TEST_ASSERT(backend.gui_calls == 2 && client.next_render_sequence == 3);
    ios_file_explorer_client_disconnect(&client);
    IOS_TEST_ASSERT_STATUS(ios_shell_service_stop(&shell_service), IOS_OK);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_model_initialization_paginates_through_shell_ipc),
    IOS_TEST_CASE(test_refresh_reconnects_after_restart_and_is_failure_atomic),
    IOS_TEST_CASE(test_type_search_and_render_use_versioned_shell_operations)
};

const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
