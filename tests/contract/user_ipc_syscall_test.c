#include <inferenceos/test.h>

#include <inferenceos/ipc.h>
#include <inferenceos/runtime.h>
#include <inferenceos/syscall.h>

static ios_syscall_handler registered_handlers[IOS_SYSCALL_MAX_NUMBER + 1];

_Noreturn void ios_assertion_failed(
    const char *expression,
    const char *source_file,
    ios_u32 source_line
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
    return IOS_ERROR(IOS_E_INVALID_STATE);
}

ios_status scheduler_wake_task(struct ios_scheduler_task *task)
{
    (void)task;
    return IOS_OK;
}

ios_status syscall_register(ios_u64 number, ios_syscall_handler handler)
{
    if (number > IOS_SYSCALL_MAX_NUMBER || handler == NULL
        || registered_handlers[number] != NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    registered_handlers[number] = handler;
    return IOS_OK;
}

ios_status user_copy_from(
    const struct ios_process *process,
    void *kernel_destination,
    ios_uptr user_source,
    ios_size byte_count
)
{
    if (process == NULL || kernel_destination == NULL || user_source == 0) {
        return IOS_ERROR(IOS_E_BAD_ADDRESS);
    }
    memcpy(kernel_destination, (const void *)user_source, byte_count);
    return IOS_OK;
}

ios_status user_copy_to(
    const struct ios_process *process,
    ios_uptr user_destination,
    const void *kernel_source,
    ios_size byte_count
)
{
    if (process == NULL || user_destination == 0 || kernel_source == NULL) {
        return IOS_ERROR(IOS_E_BAD_ADDRESS);
    }
    memcpy((void *)user_destination, kernel_source, byte_count);
    return IOS_OK;
}

ios_status syscall_validate_structure_header(
    const struct ios_process *process,
    ios_uptr user_address,
    ios_u16 minimum_size,
    ios_u16 expected_version,
    struct ios_syscall_structure_header *header
)
{
    ios_status status = user_copy_from(
        process, header, user_address, sizeof(*header)
    );
    if (IOS_FAILED(status)) return status;
    if (header->version != expected_version) {
        return IOS_ERROR(IOS_E_UNSUPPORTED_VERSION);
    }
    return header->size < minimum_size
        ? IOS_ERROR(IOS_E_PROTOCOL) : IOS_OK;
}

static void initialize_process(
    struct ios_process *process,
    ios_u64 process_id,
    ios_u64 application_identity
)
{
    memset(process, 0, sizeof(*process));
    process->process_id = process_id;
    process->application_identity = application_identity;
    process->state = IOS_PROCESS_RUNNABLE;
    IOS_TEST_ASSERT_STATUS(
        handle_table_initialize(&process->handles, process_id), IOS_OK
    );
}

static void initialize_gateway(
    struct ios_process *shell,
    struct ios_process *client,
    ios_handle *shell_endpoint
)
{
    memset(registered_handlers, 0, sizeof(registered_handlers));
    initialize_process(shell, 1, UINT64_C(0x1001));
    initialize_process(client, 2, UINT64_C(0x2001));
    IOS_TEST_ASSERT_STATUS(ipc_initialize(), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ipc_trust_service(IOS_SERVICE_SHELL, shell->application_identity), IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(
        ipc_endpoint_create(shell, 4, shell_endpoint), IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(
        ipc_service_register(shell, IOS_SERVICE_SHELL, *shell_endpoint), IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(syscall_ipc_register(), IOS_OK);
}

static ios_status call_handler(
    ios_u64 number,
    struct ios_process *process,
    void *request
)
{
    const ios_u64 arguments[6] = { (ios_uptr)request, 0, 0, 0, 0, 0 };
    IOS_TEST_ASSERT(number <= IOS_SYSCALL_MAX_NUMBER);
    IOS_TEST_ASSERT(registered_handlers[number] != NULL);
    return registered_handlers[number](process, arguments);
}

static void test_versioned_connect_send_and_receive_round_trip(void)
{
    struct ios_process shell;
    struct ios_process client;
    ios_handle shell_endpoint;
    const ios_u8 payload[] = { 4, 2, 1 };
    struct ios_user_ipc_connect_request connect = {
        .size = sizeof(connect),
        .version = IOS_IPC_ABI_VERSION,
        .service = IOS_SERVICE_SHELL
    };
    struct ios_user_ipc_send_request send = {
        .size = sizeof(send),
        .version = IOS_IPC_ABI_VERSION,
        .request_id = 17,
        .operation = 9,
        .item_count = 1,
        .payload_size = sizeof(payload),
        .payload_address = (ios_uptr)payload
    };
    struct ios_ipc_message message;
    struct ios_user_ipc_receive_request receive = {
        .size = sizeof(receive),
        .version = IOS_IPC_ABI_VERSION,
        .message_address = (ios_uptr)&message,
        .message_capacity = sizeof(message)
    };

    initialize_gateway(&shell, &client, &shell_endpoint);
    IOS_TEST_ASSERT_STATUS(
        call_handler(IOS_SYSCALL_IPC_SERVICE_CONNECT, &client, &connect), IOS_OK
    );
    IOS_TEST_ASSERT(connect.endpoint_handle != IOS_INVALID_HANDLE);
    IOS_TEST_ASSERT(
        connect.service_generation == ipc_service_generation(IOS_SERVICE_SHELL)
    );

    send.endpoint_handle = connect.endpoint_handle;
    IOS_TEST_ASSERT_STATUS(
        call_handler(IOS_SYSCALL_IPC_SEND, &client, &send), IOS_OK
    );
    receive.endpoint_handle = shell_endpoint;
    IOS_TEST_ASSERT_STATUS(
        call_handler(IOS_SYSCALL_IPC_RECEIVE, &shell, &receive), IOS_OK
    );
    IOS_TEST_ASSERT(message.header.size == sizeof(message.header) + sizeof(payload));
    IOS_TEST_ASSERT(message.header.version == IOS_IPC_ABI_VERSION);
    IOS_TEST_ASSERT(message.header.operation == send.operation);
    IOS_TEST_ASSERT(message.header.request_id == send.request_id);
    IOS_TEST_ASSERT(message.header.caller_process_id == client.process_id);
    IOS_TEST_ASSERT(
        message.header.caller_application_identity == client.application_identity
    );
    IOS_TEST_ASSERT(message.header.item_count == send.item_count);
    IOS_TEST_ASSERT(message.header.payload_size == sizeof(payload));
    IOS_TEST_ASSERT(memcmp(message.payload, payload, sizeof(payload)) == 0);
}

static void test_gateway_rejects_versions_reserved_data_and_unbounded_output(void)
{
    struct ios_process shell;
    struct ios_process client;
    ios_handle shell_endpoint;
    struct ios_user_ipc_connect_request connect = {
        .size = sizeof(connect),
        .version = IOS_IPC_ABI_VERSION,
        .service = IOS_SERVICE_SHELL,
        .reserved32 = 1
    };
    struct ios_ipc_message message;
    struct ios_user_ipc_receive_request receive = {
        .size = sizeof(receive),
        .version = IOS_IPC_ABI_VERSION,
        .endpoint_handle = 1,
        .message_address = (ios_uptr)&message,
        .message_capacity = sizeof(message) - 1U
    };

    initialize_gateway(&shell, &client, &shell_endpoint);
    IOS_TEST_ASSERT_STATUS(
        call_handler(IOS_SYSCALL_IPC_SERVICE_CONNECT, &client, &connect),
        IOS_ERROR(IOS_E_PROTOCOL)
    );
    connect.reserved32 = 0;
    connect.version = IOS_IPC_ABI_VERSION + 1U;
    IOS_TEST_ASSERT_STATUS(
        call_handler(IOS_SYSCALL_IPC_SERVICE_CONNECT, &client, &connect),
        IOS_ERROR(IOS_E_UNSUPPORTED_VERSION)
    );
    IOS_TEST_ASSERT_STATUS(
        call_handler(IOS_SYSCALL_IPC_RECEIVE, &shell, &receive),
        IOS_ERROR(IOS_E_PROTOCOL)
    );
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_versioned_connect_send_and_receive_round_trip),
    IOS_TEST_CASE(test_gateway_rejects_versions_reserved_data_and_unbounded_output)
};

const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
