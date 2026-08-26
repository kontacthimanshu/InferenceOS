#include <inferenceos/test.h>

#include <inferenceos/arch/interrupts.h>
#include <inferenceos/fs/registry.h>
#include <inferenceos/gui/file_explorer.h>

#include <string.h>

enum { REGISTRY_VIEW_ENTRY_COUNT = 3 };

struct registry_file_view_backend {
    struct ios_fs_registry registry;
    struct ios_fs_registry_record_disk storage[4];
    struct ios_fs_registry_source_entry sources[REGISTRY_VIEW_ENTRY_COUNT];
    const char *base_names[REGISTRY_VIEW_ENTRY_COUNT];
    ios_u64 object_handles[REGISTRY_VIEW_ENTRY_COUNT];
    ios_type_icon_capability text_capability;
    ios_type_icon_capability image_capability;
    ios_size shell_calls;
    ios_size registry_lookup_calls;
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

ios_u64 x86_64_interrupt_save_disable(void) { return 0; }
void x86_64_interrupt_restore(ios_u64 previous_flags) { (void)previous_flags; }

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

static void make_source(
    const ios_u8 base[8],
    const ios_u8 extension[IOS_FS_EXTENSION_SIZE],
    ios_u32 directory_cluster,
    ios_u16 primary_slot,
    struct ios_fs_registry_source_entry *entry
)
{
    struct ios_fs_primary value = {
        { ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },
        IOS_FS_ATTRIBUTE_REGULAR, 2, 64
    };
    memset(entry, 0, sizeof(*entry));
    memcpy(value.name, base, 8);
    memcpy(value.name + 8, extension, IOS_FS_EXTENSION_SIZE);
    IOS_TEST_ASSERT_STATUS(ios_fs_primary_encode(&value, &entry->primary), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_companion_encode(value.name, true, &entry->companion), IOS_OK
    );
    entry->directory_cluster = directory_cluster;
    entry->primary_slot = primary_slot;
}

static void initialize_backend(struct registry_file_view_backend *backend, bool enabled)
{
    static const ios_u8 report[8] = { 'R', 'E', 'P', 'O', 'R', 'T', ' ', ' ' };
    static const ios_u8 chart[8] = { 'C', 'H', 'A', 'R', 'T', ' ', ' ', ' ' };
    static const ios_u8 notes[8] = { 'N', 'O', 'T', 'E', 'S', ' ', ' ', ' ' };
    static const ios_u8 txt[3] = { 'T', 'X', 'T' };
    static const ios_u8 png[3] = { 'P', 'N', 'G' };

    memset(backend, 0, sizeof(*backend));
    backend->base_names[0] = "REPORT";
    backend->base_names[1] = "CHART";
    backend->base_names[2] = "NOTES";
    backend->object_handles[0] = 7;
    backend->object_handles[1] = 9;
    backend->object_handles[2] = 11;
    backend->text_capability = UINT64_C(0x77770001);
    backend->image_capability = UINT64_C(0x77770002);
    make_source(report, txt, 20, 2, &backend->sources[0]);
    make_source(chart, png, 20, 4, &backend->sources[1]);
    make_source(notes, txt, 20, 6, &backend->sources[2]);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_initialize(
            &backend->registry, backend->storage,
            IOS_ARRAY_COUNT(backend->storage), enabled
        ),
        IOS_OK
    );
    if (enabled) {
        IOS_TEST_ASSERT_STATUS(
            ios_fs_registry_rebuild(
                &backend->registry, backend->sources,
                IOS_ARRAY_COUNT(backend->sources)
            ),
            IOS_OK
        );
    }
}

static ios_status append_safe_entry(
    struct registry_file_view_backend *backend,
    ios_size source_index,
    struct ios_shell_file_view_reply *reply
)
{
    if (reply->item_count >= IOS_SHELL_FILE_VIEW_REPLY_CAPACITY) {
        return IOS_ERROR(IOS_E_NO_SPACE);
    }
    const bool text = source_index != 1;
    const struct ios_display_safe_source_entry safe = {
        .base_name = backend->base_names[source_index],
        .object_handle = backend->object_handles[source_index],
        .type_icon_capability = text
            ? backend->text_capability : backend->image_capability,
        .byte_size = 64,
        .allowed_operations = IOS_DISPLAY_SAFE_OPERATION_OPEN
            | IOS_DISPLAY_SAFE_OPERATION_READ,
        .object_kind = IOS_DISPLAY_SAFE_REGULAR_FILE
    };
    ios_status status = ios_display_safe_entry_convert(
        &safe, &reply->entries[reply->item_count]
    );
    if (IOS_SUCCEEDED(status)) ++reply->item_count;
    return status;
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
    struct registry_file_view_backend *backend = context;
    ios_size matches[REGISTRY_VIEW_ENTRY_COUNT] = { SIZE_MAX, SIZE_MAX, SIZE_MAX };
    ios_size match_count = 0;
    static const ios_u8 txt[3] = { 'T', 'X', 'T' };
    static const ios_u8 png[3] = { 'P', 'N', 'G' };

    ++backend->shell_calls;
    IOS_TEST_ASSERT(caller_process_id == 20);
    IOS_TEST_ASSERT(caller_application_identity == UINT64_C(0x46494c455850));
    if (request->directory_handle != 1 || request->continuation != 0) {
        return IOS_ERROR(IOS_E_NOT_FOUND);
    }
    if (operation == IOS_SHELL_DIRECTORY_VIEW) {
        for (ios_size index = 0; index < REGISTRY_VIEW_ENTRY_COUNT; ++index) {
            ios_status status = append_safe_entry(backend, index, reply);
            if (IOS_FAILED(status)) return status;
        }
        return IOS_OK;
    }
    const ios_u8 *extension;
    if (request->type_icon_capability == backend->text_capability) {
        extension = txt;
    } else if (request->type_icon_capability == backend->image_capability) {
        extension = png;
    } else {
        return IOS_ERROR(IOS_E_BAD_HANDLE);
    }
    ++backend->registry_lookup_calls;
    ios_status status = ios_fs_registry_lookup(
        &backend->registry, backend->sources, IOS_ARRAY_COUNT(backend->sources),
        extension, 3, matches, IOS_ARRAY_COUNT(matches), &match_count
    );
    if (IOS_FAILED(status)) return status;
    for (ios_size index = 0; index < match_count; ++index) {
        status = append_safe_entry(backend, matches[index], reply);
        if (IOS_FAILED(status)) return status;
    }
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
    (void)context;
    (void)caller_process_id;
    (void)caller_application_identity;
    reply->render_sequence = request->render_sequence;
    return IOS_OK;
}

static ios_status resolve_icon(
    void *context,
    ios_u64 type_icon_capability,
    ios_u32 object_kind,
    enum ios_presentation_icon *icon
)
{
    const struct registry_file_view_backend *backend = context;
    if (object_kind == IOS_DISPLAY_SAFE_DIRECTORY) {
        *icon = IOS_ICON_FOLDER;
    } else {
        *icon = type_icon_capability == backend->text_capability
            ? IOS_ICON_TEXT : IOS_ICON_IMAGE;
    }
    return IOS_OK;
}

static void request_text_view(
    bool registry_enabled,
    struct ios_display_safe_entry output[REGISTRY_VIEW_ENTRY_COUNT],
    ios_size *output_count,
    ios_size *registry_lookup_calls,
    enum ios_fs_registry_health *health
)
{
    struct ios_process shell_process;
    struct ios_process explorer_process;
    struct ios_shell_service shell_service = { 0 };
    struct ios_file_explorer_client client;
    struct registry_file_view_backend backend;

    initialize_process(&shell_process, 10, UINT64_C(0x5348454c4c));
    initialize_process(&explorer_process, 20, UINT64_C(0x46494c455850));
    initialize_backend(&backend, registry_enabled);
    IOS_TEST_ASSERT_STATUS(ipc_initialize(), IOS_OK);
    const struct ios_shell_service_config config = {
        .process = &shell_process,
        .queue_depth = 4,
        .dispatch_file_view = dispatch_file_view,
        .dispatch_gui_view = dispatch_gui_view,
        .dispatch_context = &backend
    };
    IOS_TEST_ASSERT_STATUS(ios_shell_service_start(&shell_service, &config), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_file_explorer_client_initialize(
            &client, &explorer_process, &shell_service, resolve_icon, &backend
        ),
        IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(
        ios_file_explorer_client_file_view(
            &client, IOS_SHELL_TYPE_VIEW, 1, backend.text_capability,
            output, REGISTRY_VIEW_ENTRY_COUNT, output_count
        ),
        IOS_OK
    );
    IOS_TEST_ASSERT(backend.shell_calls == 1);
    *registry_lookup_calls = backend.registry_lookup_calls;
    *health = ios_fs_registry_health(&backend.registry);
    ios_file_explorer_client_disconnect(&client);
    IOS_TEST_ASSERT_STATUS(ios_shell_service_stop(&shell_service), IOS_OK);
}

static void assert_text_view_is_safe(
    const struct ios_display_safe_entry *entries, ios_size count
)
{
    IOS_TEST_ASSERT(count == 2);
    IOS_TEST_ASSERT(strcmp(entries[0].display_name, "REPORT") == 0);
    IOS_TEST_ASSERT(strcmp(entries[1].display_name, "NOTES") == 0);
    for (ios_size index = 0; index < count; ++index) {
        IOS_TEST_ASSERT(entries[index].version == IOS_DISPLAY_SAFE_ENTRY_VERSION);
        IOS_TEST_ASSERT(strchr(entries[index].display_name, '.') == NULL);
        IOS_TEST_ASSERT(strstr(entries[index].display_name, "TXT") == NULL);
        IOS_TEST_ASSERT(entries[index].type_icon_capability != UINT64_C(0xe771f04f));
    }
}

static void test_enabled_registry_type_view_traverses_shell_and_returns_safe_entries(void)
{
    struct ios_display_safe_entry entries[REGISTRY_VIEW_ENTRY_COUNT];
    ios_size count = 0;
    ios_size lookup_calls = 0;
    enum ios_fs_registry_health health;

    request_text_view(true, entries, &count, &lookup_calls, &health);
    IOS_TEST_ASSERT(lookup_calls == 1);
    IOS_TEST_ASSERT(health == IOS_FS_REGISTRY_HEALTHY);
    assert_text_view_is_safe(entries, count);
}

static void test_enabled_and_disabled_modes_return_identical_shell_views(void)
{
    struct ios_display_safe_entry enabled_entries[REGISTRY_VIEW_ENTRY_COUNT];
    struct ios_display_safe_entry disabled_entries[REGISTRY_VIEW_ENTRY_COUNT];
    ios_size enabled_count = 0;
    ios_size disabled_count = 0;
    ios_size enabled_calls = 0;
    ios_size disabled_calls = 0;
    enum ios_fs_registry_health enabled_health;
    enum ios_fs_registry_health disabled_health;

    request_text_view(
        true, enabled_entries, &enabled_count, &enabled_calls, &enabled_health
    );
    request_text_view(
        false, disabled_entries, &disabled_count, &disabled_calls, &disabled_health
    );
    IOS_TEST_ASSERT(enabled_calls == 1 && disabled_calls == 1);
    IOS_TEST_ASSERT(enabled_health == IOS_FS_REGISTRY_HEALTHY);
    IOS_TEST_ASSERT(disabled_health == IOS_FS_REGISTRY_DISABLED);
    IOS_TEST_ASSERT(enabled_count == disabled_count);
    IOS_TEST_ASSERT(memcmp(
        enabled_entries, disabled_entries,
        enabled_count * sizeof(*enabled_entries)
    ) == 0);
    assert_text_view_is_safe(enabled_entries, enabled_count);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(
        test_enabled_registry_type_view_traverses_shell_and_returns_safe_entries
    ),
    IOS_TEST_CASE(test_enabled_and_disabled_modes_return_identical_shell_views)
};

const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
