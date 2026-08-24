#include <inferenceos/test.h>

#include <inferenceos/arch/interrupts.h>
#include <inferenceos/ipc.h>
#include <inferenceos/shell_protocol.h>

#include <string.h>

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

static void test_shell_message_is_versioned_bounded_and_kernel_identified(void)
{
    const struct ios_shell_file_view_request request = {
        .size = sizeof(request),
        .version = IOS_SHELL_PROTOCOL_VERSION,
        .directory_handle = 1,
        .maximum_items = IOS_SHELL_FILE_VIEW_REPLY_CAPACITY
    };
    struct ios_process shell;
    struct ios_process client;
    struct ios_ipc_message message;
    ios_handle shell_endpoint;
    ios_handle client_endpoint;
    initialize_process(&shell, 10, UINT64_C(0x5348454c4c));
    initialize_process(&client, 20, UINT64_C(0x46494c455850));
    IOS_TEST_ASSERT_STATUS(ipc_initialize(), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ipc_trust_service(IOS_SERVICE_SHELL, shell.application_identity), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ipc_endpoint_create(&shell, 2, &shell_endpoint), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ipc_service_register(&shell, IOS_SERVICE_SHELL, shell_endpoint), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ipc_service_connect(&client, IOS_SERVICE_SHELL, &client_endpoint), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ipc_send(&client, client_endpoint, 77, IOS_SHELL_DIRECTORY_VIEW, 0, 0,
                 &request, (ios_u16)sizeof(request), false), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ipc_receive(&shell, shell_endpoint, &message, false), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_shell_message_validate(&message), IOS_OK);
    IOS_TEST_ASSERT(message.header.version == IOS_IPC_ABI_VERSION);
    IOS_TEST_ASSERT(message.header.size == sizeof(message.header) + sizeof(request));
    IOS_TEST_ASSERT(message.header.request_id == 77 && message.header.operation == 1);
    IOS_TEST_ASSERT(message.header.caller_process_id == client.process_id);
    IOS_TEST_ASSERT(
        message.header.caller_application_identity == client.application_identity);
    IOS_TEST_ASSERT(memcmp(message.payload, &request, sizeof(request)) == 0);
}

static void test_shell_file_and_gui_view_schemas_are_versioned_and_bounded(void)
{
    struct ios_shell_file_view_request file_request = {
        .size = sizeof(file_request),
        .version = IOS_SHELL_PROTOCOL_VERSION,
        .directory_handle = 7,
        .type_icon_capability = 11,
        .maximum_items = IOS_SHELL_FILE_VIEW_REPLY_CAPACITY
    };
    struct ios_shell_file_view_reply file_reply = {
        .size = sizeof(file_reply),
        .version = IOS_SHELL_PROTOCOL_VERSION,
        .status = IOS_OK
    };
    struct ios_shell_gui_view_request gui_request = {
        .size = sizeof(gui_request),
        .version = IOS_SHELL_PROTOCOL_VERSION,
        .window_handle = 13,
        .view_handle = 17,
        .render_sequence = 19
    };
    struct ios_shell_gui_view_reply gui_reply = {
        .size = sizeof(gui_reply),
        .version = IOS_SHELL_PROTOCOL_VERSION,
        .status = IOS_OK,
        .render_sequence = gui_request.render_sequence
    };
    IOS_TEST_ASSERT(IOS_SHELL_DIRECTORY_VIEW == 1);
    IOS_TEST_ASSERT(IOS_SHELL_TYPE_VIEW == 2);
    IOS_TEST_ASSERT(IOS_SHELL_SEARCH == 3);
    IOS_TEST_ASSERT(IOS_SHELL_GUI_VIEW == 4);
    IOS_TEST_ASSERT(file_request.size == 40 && file_request.flags == 0);
    IOS_TEST_ASSERT(file_request.maximum_items == 4);
    IOS_TEST_ASSERT(file_reply.size <= IOS_IPC_MAX_PAYLOAD_SIZE);
    IOS_TEST_ASSERT(file_reply.item_count == 0 && file_reply.flags == 0);
    IOS_TEST_ASSERT(gui_request.size == 32 && gui_request.flags == 0);
    IOS_TEST_ASSERT(gui_reply.size == 24 && gui_reply.render_sequence == 19);
}

static void test_shell_contract_rejects_versions_flags_and_inconsistent_bounds(void)
{
    struct ios_shell_file_view_request request = {
        .size = sizeof(request),
        .version = IOS_SHELL_PROTOCOL_VERSION,
        .directory_handle = 7,
        .maximum_items = 1
    };
    struct ios_ipc_message message = { .header = {
        .size = sizeof(message.header) + sizeof(request),
        .version = IOS_IPC_ABI_VERSION,
        .operation = IOS_SHELL_DIRECTORY_VIEW,
        .request_id = 1,
        .caller_process_id = 2,
        .caller_application_identity = 3,
        .payload_size = sizeof(request)
    } };
    memcpy(message.payload, &request, sizeof(request));
    IOS_TEST_ASSERT_STATUS(ios_shell_message_validate(&message), IOS_OK);
    message.header.version = IOS_IPC_ABI_VERSION + 1U;
    IOS_TEST_ASSERT_STATUS(ios_shell_message_validate(&message), IOS_ERROR(IOS_E_PROTOCOL));
    message.header.version = IOS_IPC_ABI_VERSION;
    message.header.flags = 1;
    IOS_TEST_ASSERT_STATUS(ios_shell_message_validate(&message), IOS_ERROR(IOS_E_PROTOCOL));
    message.header.flags = 0;
    message.header.payload_size = sizeof(request) - 1U;
    IOS_TEST_ASSERT_STATUS(ios_shell_message_validate(&message), IOS_ERROR(IOS_E_PROTOCOL));
    message.header.payload_size = sizeof(request);
    request.reserved32 = 1;
    memcpy(message.payload, &request, sizeof(request));
    IOS_TEST_ASSERT_STATUS(ios_shell_message_validate(&message), IOS_ERROR(IOS_E_PROTOCOL));
}

static void test_ipc_send_rejects_unbounded_or_ambiguous_requests(void)
{
    struct ios_process process;
    ios_u8 byte = 0;
    initialize_process(&process, 30, 40);
    IOS_TEST_ASSERT_STATUS(ipc_initialize(), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ipc_send(&process, IOS_INVALID_HANDLE, 0, 1, 0, 0, NULL, 0, false),
        IOS_ERROR(IOS_E_PROTOCOL));
    IOS_TEST_ASSERT_STATUS(
        ipc_send(&process, IOS_INVALID_HANDLE, 1, 0, 0, 0, NULL, 0, false),
        IOS_ERROR(IOS_E_PROTOCOL));
    IOS_TEST_ASSERT_STATUS(
        ipc_send(&process, IOS_INVALID_HANDLE, 1, 1, 1, 0, NULL, 0, false),
        IOS_ERROR(IOS_E_PROTOCOL));
    IOS_TEST_ASSERT_STATUS(
        ipc_send(&process, IOS_INVALID_HANDLE, 1, 1, 0,
                 IOS_IPC_MAX_ITEM_COUNT + 1U, NULL, 0, false),
        IOS_ERROR(IOS_E_PROTOCOL));
    IOS_TEST_ASSERT_STATUS(
        ipc_send(&process, IOS_INVALID_HANDLE, 1, 1, 0, 0,
                 &byte, IOS_IPC_MAX_PAYLOAD_SIZE + 1U, false),
        IOS_ERROR(IOS_E_PROTOCOL));
    IOS_TEST_ASSERT_STATUS(
        ipc_send(&process, IOS_INVALID_HANDLE, 1, 1, 0, 0, NULL, 1, false),
        IOS_ERROR(IOS_E_PROTOCOL));
}

struct dispatch_observation {
    ios_u64 caller_process_id;
    ios_u64 caller_application_identity;
    enum ios_shell_operation operation;
    ios_size file_calls;
    ios_size gui_calls;
};

static ios_status dispatch_file_view(
    void *context,
    ios_u64 caller_process_id,
    ios_u64 caller_application_identity,
    enum ios_shell_operation operation,
    const struct ios_shell_file_view_request *request,
    struct ios_shell_file_view_reply *reply
)
{
    struct dispatch_observation *observation = context;
    observation->caller_process_id = caller_process_id;
    observation->caller_application_identity = caller_application_identity;
    observation->operation = operation;
    ++observation->file_calls;
    IOS_TEST_ASSERT(request->directory_handle == 7);
    reply->item_count = 1;
    reply->entries[0] = (struct ios_display_safe_entry){
        .size = sizeof(reply->entries[0]),
        .version = IOS_DISPLAY_SAFE_ENTRY_VERSION,
        .object_handle = 9,
        .type_icon_capability = 11,
        .byte_size = 13,
        .allowed_operations = IOS_DISPLAY_SAFE_OPERATION_OPEN,
        .object_kind = IOS_DISPLAY_SAFE_REGULAR_FILE,
        .display_name_length = 6
    };
    memcpy(reply->entries[0].display_name, "REPORT", 7);
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
    struct dispatch_observation *observation = context;
    observation->caller_process_id = caller_process_id;
    observation->caller_application_identity = caller_application_identity;
    ++observation->gui_calls;
    IOS_TEST_ASSERT(request->window_handle == 17 && request->view_handle == 19);
    IOS_TEST_ASSERT(reply->render_sequence == request->render_sequence);
    return IOS_OK;
}

static void test_shell_service_validates_and_dispatches_file_and_gui_requests(void)
{
    struct ios_process shell;
    struct ios_process client;
    struct ios_shell_service service = { 0 };
    struct dispatch_observation observation = { 0 };
    struct ios_shell_dispatch_result result;
    ios_handle channel;
    const struct ios_shell_service_config config = {
        .process = &shell,
        .queue_depth = 2,
        .dispatch_file_view = dispatch_file_view,
        .dispatch_gui_view = dispatch_gui_view,
        .dispatch_context = &observation
    };
    const struct ios_shell_file_view_request file_request = {
        .size = sizeof(file_request),
        .version = IOS_SHELL_PROTOCOL_VERSION,
        .directory_handle = 7,
        .maximum_items = 1
    };
    const struct ios_shell_gui_view_request gui_request = {
        .size = sizeof(gui_request),
        .version = IOS_SHELL_PROTOCOL_VERSION,
        .window_handle = 17,
        .view_handle = 19,
        .render_sequence = 23
    };
    initialize_process(&shell, 31, UINT64_C(0x5348454c4c));
    initialize_process(&client, 32, UINT64_C(0x46494c455850));
    IOS_TEST_ASSERT_STATUS(ipc_initialize(), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_shell_service_start(&service, &config), IOS_OK);
    IOS_TEST_ASSERT(service.running && service.generation != 0);
    IOS_TEST_ASSERT_STATUS(
        ipc_service_connect(&client, IOS_SERVICE_SHELL, &channel), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ipc_send(&client, channel, 1, IOS_SHELL_DIRECTORY_VIEW, 0, 0,
                 &file_request, sizeof(file_request), false), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_shell_service_receive_and_dispatch(&service, &result, false), IOS_OK);
    IOS_TEST_ASSERT(result.operation == IOS_SHELL_DIRECTORY_VIEW);
    IOS_TEST_ASSERT(result.item_count == 1);
    IOS_TEST_ASSERT(strcmp(result.payload.file_view.entries[0].display_name, "REPORT") == 0);
    IOS_TEST_ASSERT(observation.file_calls == 1);
    IOS_TEST_ASSERT(observation.caller_process_id == client.process_id);
    IOS_TEST_ASSERT(observation.caller_application_identity == client.application_identity);
    IOS_TEST_ASSERT_STATUS(
        ipc_send(&client, channel, 2, IOS_SHELL_GUI_VIEW, 0, 0,
                 &gui_request, sizeof(gui_request), false), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_shell_service_receive_and_dispatch(&service, &result, false), IOS_OK);
    IOS_TEST_ASSERT(result.operation == IOS_SHELL_GUI_VIEW);
    IOS_TEST_ASSERT(result.payload.gui_view.render_sequence == 23);
    IOS_TEST_ASSERT(observation.gui_calls == 1);
    IOS_TEST_ASSERT_STATUS(ios_shell_service_stop(&service), IOS_OK);
}

static void test_shell_restart_advances_generation_and_revokes_old_channel(void)
{
    struct ios_process shell;
    struct ios_process client;
    struct ios_shell_service service = { 0 };
    ios_handle stale_channel;
    ios_handle replacement_channel;
    const struct ios_shell_file_view_request request = {
        .size = sizeof(request),
        .version = IOS_SHELL_PROTOCOL_VERSION,
        .directory_handle = 1,
        .maximum_items = 1
    };
    initialize_process(&shell, 41, UINT64_C(0x5348454c4c));
    initialize_process(&client, 43, UINT64_C(0x434c49454e54));
    IOS_TEST_ASSERT_STATUS(ipc_initialize(), IOS_OK);
    const ios_u64 initial_generation = ipc_service_generation(IOS_SERVICE_SHELL);
    const struct ios_shell_service_config config = { .process = &shell, .queue_depth = 2 };
    IOS_TEST_ASSERT_STATUS(ios_shell_service_start(&service, &config), IOS_OK);
    const ios_u64 registered_generation = service.generation;
    IOS_TEST_ASSERT(registered_generation != initial_generation);
    IOS_TEST_ASSERT_STATUS(
        ipc_service_connect(&client, IOS_SERVICE_SHELL, &stale_channel), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_shell_service_restart(&service), IOS_OK);
    IOS_TEST_ASSERT(service.generation != registered_generation);
    IOS_TEST_ASSERT_STATUS(
        ipc_send(&client, stale_channel, 1, IOS_SHELL_DIRECTORY_VIEW, 0, 0,
                 &request, sizeof(request), false),
        IOS_ERROR(IOS_E_INVALID_STATE));
    IOS_TEST_ASSERT_STATUS(
        ipc_service_connect(&client, IOS_SERVICE_SHELL, &replacement_channel), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ipc_send(&client, replacement_channel, 2, IOS_SHELL_DIRECTORY_VIEW, 0, 0,
                 &request, sizeof(request), false), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_shell_service_stop(&service), IOS_OK);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_shell_message_is_versioned_bounded_and_kernel_identified),
    IOS_TEST_CASE(test_shell_file_and_gui_view_schemas_are_versioned_and_bounded),
    IOS_TEST_CASE(test_shell_contract_rejects_versions_flags_and_inconsistent_bounds),
    IOS_TEST_CASE(test_ipc_send_rejects_unbounded_or_ambiguous_requests),
    IOS_TEST_CASE(test_shell_service_validates_and_dispatches_file_and_gui_requests),
    IOS_TEST_CASE(test_shell_restart_advances_generation_and_revokes_old_channel)
};

const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
