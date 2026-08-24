#include <inferenceos/test.h>

#include <inferenceos/arch/interrupts.h>
#include <inferenceos/application_bindings.h>
#include <inferenceos/custom_test.h>
#include <inferenceos/ipc.h>
#include <inferenceos/proprietary_adapter.h>
#include <inferenceos/shell_protocol.h>
#include <inferenceos/type_capability.h>

#include <string.h>

enum {
    PROPRIETARY_APPLICATION = 0x50524f50,
    CUSTOM_APPLICATION = 0x43555354,
    PROPRIETARY_TYPE = 0x50544631,
    UNASSIGNED_ADAPTER_OPERATION = 5
};

ios_u64 x86_64_interrupt_save_disable(void) { return 0; }
void x86_64_interrupt_restore(ios_u64 flags) { (void)flags; }

_Noreturn void ios_assertion_failed(
    const char *expression, const char *source_file, ios_u32 source_line)
{
    ios_test_fail(expression, source_file, source_line);
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

static struct ios_ipc_message file_view_message(
    ios_u32 operation, const struct ios_shell_file_view_request *request)
{
    struct ios_ipc_message message = { 0 };
    message.header = (struct ios_ipc_message_header){
        .size = sizeof(message.header) + sizeof(*request),
        .version = IOS_IPC_ABI_VERSION,
        .operation = operation,
        .request_id = 1,
        .caller_process_id = 2,
        .caller_application_identity = CUSTOM_APPLICATION,
        .payload_size = sizeof(*request)
    };
    memcpy(message.payload, request, sizeof(*request));
    return message;
}

static void test_raw_extension_and_hash_filter_payloads_are_rejected(void)
{
    struct ios_shell_file_view_request request = {
        .size = sizeof(request),
        .version = IOS_SHELL_PROTOCOL_VERSION,
        .directory_handle = 1,
        .maximum_items = 1
    };
    struct ios_ipc_message message;

    /* There is no raw-filter field; smuggling one through flags/reserved bytes is invalid. */
    request.flags = UINT32_C(0x545854); /* "TXT" */
    message = file_view_message(IOS_SHELL_DIRECTORY_VIEW, &request);
    IOS_TEST_ASSERT_STATUS(ios_shell_message_validate(&message), IOS_ERROR(IOS_E_PROTOCOL));
    request.flags = 0;
    request.reserved32 = UINT32_C(0x45373731); /* hash-text prefix "E771" */
    message = file_view_message(IOS_SHELL_DIRECTORY_VIEW, &request);
    IOS_TEST_ASSERT_STATUS(ios_shell_message_validate(&message), IOS_ERROR(IOS_E_PROTOCOL));
}

static void test_custom_application_cannot_claim_a_proprietary_type(void)
{
    struct ios_application_binding_registry bindings;
    struct ios_type_catalog catalog;
    struct ios_type_capability_service capabilities;
    struct ios_process proprietary;
    struct ios_process custom;
    ios_type_icon_capability catalog_capability;
    ios_handle proprietary_handle;
    ios_handle custom_handle = IOS_INVALID_HANDLE;
    ios_u64 identity;
    ios_type_icon_capability presentation;

    initialize_process(&proprietary, 10, PROPRIETARY_APPLICATION);
    initialize_process(&custom, 20, CUSTOM_APPLICATION);
    ios_application_bindings_initialize(&bindings);
    IOS_TEST_ASSERT_STATUS(ios_application_bindings_register(
        &bindings, PROPRIETARY_APPLICATION, PROPRIETARY_TYPE), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_type_catalog_initialize(&catalog, 0xa55a), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_type_catalog_register(
        &catalog, PROPRIETARY_TYPE, IOS_ICON_APPLICATION, &catalog_capability), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_type_capability_service_initialize(
        &capabilities, &bindings, &catalog), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_type_capability_mint(
        &capabilities, &proprietary, catalog_capability, &proprietary_handle), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_type_capability_mint(
        &capabilities, &custom, catalog_capability, &custom_handle),
        IOS_ERROR(IOS_E_ACCESS_DENIED));
    IOS_TEST_ASSERT(custom_handle == IOS_INVALID_HANDLE);
    IOS_TEST_ASSERT_STATUS(ios_type_capability_resolve(
        &capabilities, &custom, proprietary_handle, &identity, &presentation),
        IOS_ERROR(IOS_E_BAD_HANDLE));
}

static void test_absent_adapter_is_explicitly_unsupported(void)
{
    const struct ios_shell_file_view_request request = {
        .size = sizeof(request),
        .version = IOS_SHELL_PROTOCOL_VERSION,
        .directory_handle = 1,
        .type_icon_capability = 1,
        .maximum_items = 1
    };
    const struct ios_ipc_message message = file_view_message(
        UNASSIGNED_ADAPTER_OPERATION, &request);
    IOS_TEST_ASSERT_STATUS(
        ios_shell_message_validate(&message), IOS_ERROR(IOS_E_UNKNOWN_SYSCALL));
}

static void test_approved_boundary_uses_reduced_opaque_content_handles(void)
{
    struct ios_process custom;
    ios_u8 content = 0;
    ios_handle handle;
    void *resolved = NULL;

    initialize_process(&custom, 30, CUSTOM_APPLICATION);
    IOS_TEST_ASSERT_STATUS(handle_table_insert(
        &custom.handles, &content, IOS_OBJECT_CONTENT, IOS_RIGHT_READ,
        NULL, NULL, &handle), IOS_OK);
    IOS_TEST_ASSERT_STATUS(handle_table_resolve(
        &custom.handles, handle, IOS_OBJECT_CONTENT, IOS_RIGHT_READ, &resolved), IOS_OK);
    IOS_TEST_ASSERT(resolved == &content);
    IOS_TEST_ASSERT_STATUS(handle_table_resolve(
        &custom.handles, handle, IOS_OBJECT_CONTENT, IOS_RIGHT_WRITE, &resolved),
        IOS_ERROR(IOS_E_ACCESS_DENIED));
    IOS_TEST_ASSERT_STATUS(handle_table_duplicate(
        &custom.handles, handle, IOS_RIGHT_READ, &handle),
        IOS_ERROR(IOS_E_ACCESS_DENIED));
}

static ios_status approved_echo(
    void *context, struct ios_process *process, ios_handle content_handle,
    ios_u32 operation, const ios_u8 *input, ios_size input_size,
    ios_u8 *output, ios_size output_capacity, ios_size *output_size)
{
    void *content = NULL;
    (void)context;
    if (operation != 1 || input_size > output_capacity) return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    IOS_TEST_ASSERT_STATUS(handle_table_resolve(
        &process->handles, content_handle, IOS_OBJECT_CONTENT, IOS_RIGHT_READ, &content), IOS_OK);
    IOS_TEST_ASSERT(content != NULL);
    memcpy(output, input, input_size);
    *output_size = input_size;
    return IOS_OK;
}

static void test_custom_application_denies_queries_then_uses_approved_adapter(void)
{
    struct ios_process shell;
    struct ios_process proprietary;
    struct ios_process custom;
    struct ios_shell_service shell_service = { 0 };
    struct ios_proprietary_adapter_service adapters;
    struct ios_custom_test_application application;
    ios_u8 content = 7;
    ios_handle content_handle;
    static const ios_u8 input[] = { 1, 2, 3 };

    initialize_process(&shell, 100, 0x5348454c);
    initialize_process(&proprietary, 101, PROPRIETARY_APPLICATION);
    initialize_process(&custom, 102, CUSTOM_APPLICATION);
    IOS_TEST_ASSERT_STATUS(ipc_initialize(), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_shell_service_start(
        &shell_service, &(struct ios_shell_service_config){
            .process = &shell, .queue_depth = 4
        }), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_proprietary_adapter_service_initialize(&adapters), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_proprietary_adapter_register(
        &adapters, &proprietary, &(struct ios_proprietary_adapter_descriptor){
            .size = sizeof(struct ios_proprietary_adapter_descriptor),
            .version = IOS_PROPRIETARY_ADAPTER_VERSION,
            .adapter_identity = 0x41445054,
            .proprietary_application_identity = PROPRIETARY_APPLICATION,
            .authorized_caller_identity = CUSTOM_APPLICATION,
            .allowed_operation_mask = UINT64_C(1) << 1,
            .required_content_rights = IOS_RIGHT_READ,
            .invoke = approved_echo
        }), IOS_OK);
    IOS_TEST_ASSERT_STATUS(handle_table_insert(
        &custom.handles, &content, IOS_OBJECT_CONTENT,
        IOS_RIGHT_READ | IOS_RIGHT_TRANSFER, NULL, NULL, &content_handle), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_custom_test_initialize(
        &application, &custom, &shell_service, &adapters,
        0x41445054, content_handle, 1), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_custom_test_run(
        &application, 1, input, sizeof(input)), IOS_OK);
    IOS_TEST_ASSERT(application.raw_extension_probe_status == IOS_ERROR(IOS_E_PROTOCOL));
    IOS_TEST_ASSERT(application.raw_hash_probe_status == IOS_ERROR(IOS_E_PROTOCOL));
    IOS_TEST_ASSERT(application.arbitrary_type_probe_status == IOS_ERROR(IOS_E_NOT_SUPPORTED));
    IOS_TEST_ASSERT(application.adapter_reply.output_size == sizeof(input));
    IOS_TEST_ASSERT(memcmp(application.adapter_reply.output, input, sizeof(input)) == 0);
    ios_custom_test_disconnect(&application);
    IOS_TEST_ASSERT(adapters.active_capability_count == 0);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_raw_extension_and_hash_filter_payloads_are_rejected),
    IOS_TEST_CASE(test_custom_application_cannot_claim_a_proprietary_type),
    IOS_TEST_CASE(test_absent_adapter_is_explicitly_unsupported),
    IOS_TEST_CASE(test_approved_boundary_uses_reduced_opaque_content_handles),
    IOS_TEST_CASE(test_custom_application_denies_queries_then_uses_approved_adapter)
};

const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
