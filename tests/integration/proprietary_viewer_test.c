#include <inferenceos/test.h>

#include <inferenceos/arch/interrupts.h>
#include <inferenceos/ipc.h>
#include <inferenceos/application_bindings.h>
#include <inferenceos/proprietary_service.h>
#include <inferenceos/proprietary_test.h>
#include <inferenceos/shell_protocol.h>

#include <string.h>

enum {
    SHELL_APPLICATION_ID = 0x5348454c,
    REPORT_VIEWER_APPLICATION_ID = 0x52505456,
    UNTRUSTED_APPLICATION_ID = 0x554e5452
};

#define REPORT_TYPE_CAPABILITY UINT64_C(0x0000000600a50001)
#define FORGED_TYPE_CAPABILITY UINT64_C(0x0000000600a50002)

struct proprietary_dispatch_context {
    ios_size calls;
    ios_u64 caller_process_id;
    ios_u64 caller_application_identity;
};

struct production_context {
    struct ios_process *viewer;
    ios_u64 expected_type;
    ios_u8 content;
    ios_size enumerations;
};

static ios_status resolve_production_process(
    void *opaque, ios_u64 process_id, ios_u64 application_identity,
    struct ios_process **process)
{
    struct production_context *context = opaque;
    if (context->viewer->process_id != process_id
        || context->viewer->application_identity != application_identity) {
        return IOS_ERROR(IOS_E_ACCESS_DENIED);
    }
    *process = context->viewer;
    return IOS_OK;
}

static ios_status enumerate_production_files(
    void *opaque, ios_u64 directory_handle, ios_u64 internal_type_identity,
    ios_u64 continuation, struct ios_proprietary_match *matches,
    ios_size capacity, ios_size *match_count, ios_u64 *next_continuation)
{
    struct production_context *context = opaque;
    ++context->enumerations;
    if (directory_handle == 0 || continuation != 0 || capacity == 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    matches[0] = (struct ios_proprietary_match){
        .base_name = "REPORT",
        .content = &context->content,
        .internal_type_identity = internal_type_identity,
        .byte_size = 15,
        .generic_attributes = IOS_DISPLAY_SAFE_ATTRIBUTE_READ_ONLY
    };
    IOS_TEST_ASSERT(internal_type_identity == context->expected_type);
    *match_count = 1;
    *next_continuation = 0;
    return IOS_OK;
}

_Noreturn void ios_assertion_failed(
    const char *expression, const char *source_file, ios_u32 source_line)
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

static void initialize_process(
    struct ios_process *process, ios_u64 process_id, ios_u64 application_identity)
{
    memset(process, 0, sizeof(*process));
    process->process_id = process_id;
    process->application_identity = application_identity;
    process->state = IOS_PROCESS_RUNNABLE;
    IOS_TEST_ASSERT_STATUS(handle_table_initialize(&process->handles, process_id), IOS_OK);
}

static ios_status proprietary_dispatch(
    void *opaque,
    ios_u64 caller_process_id,
    ios_u64 caller_application_identity,
    enum ios_shell_operation operation,
    const struct ios_shell_file_view_request *request,
    struct ios_shell_file_view_reply *reply)
{
    struct proprietary_dispatch_context *context = opaque;
    ++context->calls;
    context->caller_process_id = caller_process_id;
    context->caller_application_identity = caller_application_identity;

    if (operation != IOS_SHELL_TYPE_VIEW) return IOS_ERROR(IOS_E_ACCESS_DENIED);
    if (caller_application_identity != REPORT_VIEWER_APPLICATION_ID) {
        return IOS_ERROR(IOS_E_ACCESS_DENIED);
    }
    if (request->type_icon_capability != REPORT_TYPE_CAPABILITY) {
        return IOS_ERROR(IOS_E_BAD_HANDLE);
    }

    reply->entries[0] = (struct ios_display_safe_entry){
        .size = sizeof(reply->entries[0]),
        .version = IOS_DISPLAY_SAFE_ENTRY_VERSION,
        .object_handle = UINT64_C(0x0000000200b70001),
        .type_icon_capability = REPORT_TYPE_CAPABILITY,
        .byte_size = 15,
        .allowed_operations = IOS_DISPLAY_SAFE_OPERATION_OPEN
                            | IOS_DISPLAY_SAFE_OPERATION_READ,
        .object_kind = IOS_DISPLAY_SAFE_REGULAR_FILE,
        .display_name_length = 6,
        .display_name = "REPORT"
    };
    reply->item_count = 1;
    return IOS_OK;
}

static void initialize_shell(
    struct ios_shell_service *service,
    struct ios_process *shell,
    struct proprietary_dispatch_context *context)
{
    const struct ios_shell_service_config config = {
        .process = shell,
        .queue_depth = 4,
        .dispatch_file_view = proprietary_dispatch,
        .dispatch_context = context
    };
    IOS_TEST_ASSERT_STATUS(ipc_initialize(), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_shell_service_start(service, &config), IOS_OK);
}

static struct ios_shell_file_view_request viewer_request(ios_u64 capability)
{
    return (struct ios_shell_file_view_request){
        .size = sizeof(struct ios_shell_file_view_request),
        .version = IOS_SHELL_PROTOCOL_VERSION,
        .directory_handle = 1,
        .type_icon_capability = capability,
        .maximum_items = IOS_SHELL_FILE_VIEW_REPLY_CAPACITY
    };
}

static bool contains_bytes(
    const void *haystack, ios_size haystack_length,
    const void *needle, ios_size needle_length)
{
    const ios_u8 *bytes = haystack;
    const ios_u8 *pattern = needle;
    if (needle_length == 0 || needle_length > haystack_length) return false;
    for (ios_size offset = 0; offset <= haystack_length - needle_length; ++offset) {
        if (memcmp(bytes + offset, pattern, needle_length) == 0) return true;
    }
    return false;
}

static void test_trusted_viewer_receives_only_display_safe_bound_files(void)
{
    static const char canonical_name[11] = {
        'R', 'E', 'P', 'O', 'R', 'T', ' ', ' ', 'T', 'X', 'T'
    };
    static const char extension[] = "TXT";
    static const char extension_hash[] = "E771F04F";
    struct proprietary_dispatch_context context = { 0 };
    struct ios_shell_service service = { 0 };
    struct ios_process shell;
    struct ios_process viewer;
    struct ios_shell_dispatch_result result;
    struct ios_shell_file_view_request request = viewer_request(REPORT_TYPE_CAPABILITY);
    ios_handle shell_channel;

    initialize_process(&shell, 1, SHELL_APPLICATION_ID);
    initialize_process(&viewer, 2, REPORT_VIEWER_APPLICATION_ID);
    initialize_shell(&service, &shell, &context);
    IOS_TEST_ASSERT_STATUS(
        ipc_service_connect(&viewer, IOS_SERVICE_SHELL, &shell_channel), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_shell_service_exchange(
            &service, &viewer, shell_channel, 41, IOS_SHELL_TYPE_VIEW,
            &request, sizeof(request), &result),
        IOS_OK);

    IOS_TEST_ASSERT(context.calls == 1);
    IOS_TEST_ASSERT(context.caller_process_id == viewer.process_id);
    IOS_TEST_ASSERT(
        context.caller_application_identity == REPORT_VIEWER_APPLICATION_ID);
    IOS_TEST_ASSERT(result.item_count == 1);
    IOS_TEST_ASSERT(strcmp(result.payload.file_view.entries[0].display_name, "REPORT") == 0);
    IOS_TEST_ASSERT(result.payload.file_view.entries[0].object_handle != IOS_INVALID_HANDLE);
    IOS_TEST_ASSERT(!contains_bytes(
        &result.payload.file_view, sizeof(result.payload.file_view),
        canonical_name, sizeof(canonical_name)));
    IOS_TEST_ASSERT(!contains_bytes(
        &result.payload.file_view, sizeof(result.payload.file_view),
        extension, sizeof(extension) - 1));
    IOS_TEST_ASSERT(!contains_bytes(
        &result.payload.file_view, sizeof(result.payload.file_view),
        extension_hash, sizeof(extension_hash) - 1));
}

static void test_forged_type_capability_returns_no_files(void)
{
    struct proprietary_dispatch_context context = { 0 };
    struct ios_shell_service service = { 0 };
    struct ios_process shell;
    struct ios_process viewer;
    struct ios_shell_dispatch_result result;
    struct ios_shell_file_view_request request = viewer_request(FORGED_TYPE_CAPABILITY);
    ios_handle shell_channel;

    initialize_process(&shell, 10, SHELL_APPLICATION_ID);
    initialize_process(&viewer, 20, REPORT_VIEWER_APPLICATION_ID);
    initialize_shell(&service, &shell, &context);
    IOS_TEST_ASSERT_STATUS(
        ipc_service_connect(&viewer, IOS_SERVICE_SHELL, &shell_channel), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_shell_service_exchange(
            &service, &viewer, shell_channel, 42, IOS_SHELL_TYPE_VIEW,
            &request, sizeof(request), &result),
        IOS_ERROR(IOS_E_BAD_HANDLE));
    IOS_TEST_ASSERT(result.item_count == 0);
    IOS_TEST_ASSERT(result.payload.file_view.item_count == 0);
}

static void test_valid_token_cannot_be_reused_by_an_unbound_application(void)
{
    struct proprietary_dispatch_context context = { 0 };
    struct ios_shell_service service = { 0 };
    struct ios_process shell;
    struct ios_process untrusted;
    struct ios_shell_dispatch_result result;
    struct ios_shell_file_view_request request = viewer_request(REPORT_TYPE_CAPABILITY);
    ios_handle shell_channel;

    initialize_process(&shell, 100, SHELL_APPLICATION_ID);
    initialize_process(&untrusted, 200, UNTRUSTED_APPLICATION_ID);
    initialize_shell(&service, &shell, &context);
    IOS_TEST_ASSERT_STATUS(
        ipc_service_connect(&untrusted, IOS_SERVICE_SHELL, &shell_channel), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_shell_service_exchange(
            &service, &untrusted, shell_channel, 43, IOS_SHELL_TYPE_VIEW,
            &request, sizeof(request), &result),
        IOS_ERROR(IOS_E_ACCESS_DENIED));
    IOS_TEST_ASSERT(context.caller_application_identity == UNTRUSTED_APPLICATION_ID);
    IOS_TEST_ASSERT(result.item_count == 0);
}

static void test_production_service_mints_read_only_content_handle(void)
{
    enum { TYPE_TEXT = 0x545854 };
    struct ios_application_binding_registry bindings;
    struct ios_type_catalog catalog;
    struct ios_type_capability_service capabilities;
    struct ios_proprietary_service proprietary;
    struct ios_proprietary_test_application application;
    struct ios_shell_service shell_service = { 0 };
    struct ios_process shell;
    struct ios_process viewer;
    struct production_context context;
    struct ios_shell_file_view_reply reply = { 0 };
    struct ios_shell_file_view_request request;
    ios_type_icon_capability catalog_capability;
    ios_handle type_handle;
    void *content = NULL;

    initialize_process(&viewer, 300, REPORT_VIEWER_APPLICATION_ID);
    initialize_process(&shell, 301, SHELL_APPLICATION_ID);
    ios_application_bindings_initialize(&bindings);
    IOS_TEST_ASSERT_STATUS(ios_application_bindings_register(
        &bindings, REPORT_VIEWER_APPLICATION_ID, TYPE_TEXT), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_type_catalog_initialize(&catalog, 0xa55a), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_type_catalog_register(
        &catalog, TYPE_TEXT, IOS_ICON_TEXT, &catalog_capability), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_type_capability_service_initialize(
        &capabilities, &bindings, &catalog), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_type_capability_mint(
        &capabilities, &viewer, catalog_capability, &type_handle), IOS_OK);
    context = (struct production_context){ .viewer = &viewer, .expected_type = TYPE_TEXT };
    IOS_TEST_ASSERT_STATUS(ios_proprietary_service_initialize(
        &proprietary, &(struct ios_proprietary_service_config){
            .type_capabilities = &capabilities,
            .resolve_process = resolve_production_process,
            .enumerate = enumerate_production_files,
            .context = &context
        }), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ipc_initialize(), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_shell_service_start(
        &shell_service, &(struct ios_shell_service_config){
            .process = &shell,
            .queue_depth = 4,
            .dispatch_file_view = ios_proprietary_service_dispatch,
            .dispatch_context = &proprietary
        }), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_proprietary_test_initialize(
        &application, &viewer, &shell_service, 1, type_handle), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_proprietary_test_run(&application), IOS_OK);
    IOS_TEST_ASSERT(context.enumerations == 1 && application.entry_count == 1);
    IOS_TEST_ASSERT(strcmp(application.entries[0].display_name, "REPORT") == 0);
    IOS_TEST_ASSERT(application.entries[0].type_icon_capability == catalog_capability);
    IOS_TEST_ASSERT(application.entries[0].allowed_operations
        == (IOS_DISPLAY_SAFE_OPERATION_OPEN | IOS_DISPLAY_SAFE_OPERATION_READ));
    IOS_TEST_ASSERT_STATUS(handle_table_resolve(
        &viewer.handles, application.entries[0].object_handle,
        IOS_OBJECT_CONTENT, IOS_RIGHT_READ, &content), IOS_OK);
    IOS_TEST_ASSERT(content == &context.content);
    IOS_TEST_ASSERT_STATUS(handle_table_resolve(
        &viewer.handles, application.entries[0].object_handle,
        IOS_OBJECT_CONTENT, IOS_RIGHT_WRITE, &content), IOS_ERROR(IOS_E_ACCESS_DENIED));
    request = viewer_request(type_handle);
    request.type_icon_capability ^= UINT64_C(1) << 24;
    IOS_TEST_ASSERT_STATUS(ios_proprietary_service_dispatch(
        &proprietary, viewer.process_id, viewer.application_identity,
        IOS_SHELL_TYPE_VIEW, &request, &reply), IOS_ERROR(IOS_E_BAD_HANDLE));
    IOS_TEST_ASSERT(reply.item_count == 0 && context.enumerations == 1);
    ios_proprietary_test_disconnect(&application);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_trusted_viewer_receives_only_display_safe_bound_files),
    IOS_TEST_CASE(test_forged_type_capability_returns_no_files),
    IOS_TEST_CASE(test_valid_token_cannot_be_reused_by_an_unbound_application),
    IOS_TEST_CASE(test_production_service_mints_read_only_content_handle)
};

const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
