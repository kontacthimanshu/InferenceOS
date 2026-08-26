#include <inferenceos/user/runtime.h>

ios_status ios_user_abi_query(struct ios_syscall_abi_info *information)
{
    if (information == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    return ios_user_syscall6(
        IOS_SYSCALL_ABI_INFO, (ios_uptr)information, 0, 0, 0, 0, 0
    );
}

_Noreturn void ios_user_process_exit(ios_i64 exit_status)
{
    (void)ios_user_syscall6(
        IOS_SYSCALL_PROCESS_EXIT, (ios_u64)exit_status, 0, 0, 0, 0, 0
    );
    for (;;) { }
}

ios_status ios_user_ipc_service_connect(
    enum ios_trusted_service service,
    struct ios_user_ipc_connection *connection
)
{
    struct ios_user_ipc_connect_request request = {
        .size = sizeof(request),
        .version = IOS_IPC_ABI_VERSION,
        .service = (ios_u32)service
    };
    ios_status status;
    if (connection == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    *connection = (struct ios_user_ipc_connection){ 0 };
    status = ios_user_syscall6(
        IOS_SYSCALL_IPC_SERVICE_CONNECT, (ios_uptr)&request, 0, 0, 0, 0, 0
    );
    if (IOS_SUCCEEDED(status)) {
        connection->endpoint_handle = request.endpoint_handle;
        connection->service_generation = request.service_generation;
    }
    return status;
}

ios_status ios_user_ipc_send(
    ios_u64 endpoint_handle,
    ios_u64 request_id,
    ios_u32 operation,
    ios_u16 item_count,
    const void *payload,
    ios_u16 payload_size
)
{
    const struct ios_user_ipc_send_request request = {
        .size = sizeof(request),
        .version = IOS_IPC_ABI_VERSION,
        .endpoint_handle = endpoint_handle,
        .request_id = request_id,
        .operation = operation,
        .item_count = item_count,
        .payload_size = payload_size,
        .payload_address = (ios_uptr)payload
    };
    return ios_user_syscall6(
        IOS_SYSCALL_IPC_SEND, (ios_uptr)&request, 0, 0, 0, 0, 0
    );
}

ios_status ios_user_ipc_receive(
    ios_u64 endpoint_handle,
    struct ios_ipc_message *message
)
{
    const struct ios_user_ipc_receive_request request = {
        .size = sizeof(request),
        .version = IOS_IPC_ABI_VERSION,
        .endpoint_handle = endpoint_handle,
        .message_address = (ios_uptr)message,
        .message_capacity = sizeof(*message)
    };
    if (message == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    return ios_user_syscall6(
        IOS_SYSCALL_IPC_RECEIVE, (ios_uptr)&request, 0, 0, 0, 0, 0
    );
}
