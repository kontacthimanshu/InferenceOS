#include <inferenceos/syscall.h>

#include <inferenceos/ipc.h>

static bool unused_arguments_are_zero(const ios_u64 arguments[6])
{
    for (ios_size index = 1; index < 6; ++index) {
        if (arguments[index] != 0) return false;
    }
    return true;
}

static ios_status copy_versioned_request(
    struct ios_process *process,
    ios_uptr user_address,
    void *request,
    ios_u16 request_size
)
{
    struct ios_syscall_structure_header header;
    ios_status status = syscall_validate_structure_header(
        process, user_address, request_size, IOS_IPC_ABI_VERSION, &header
    );
    if (IOS_FAILED(status)) return status;
    if (header.size != request_size) return IOS_ERROR(IOS_E_PROTOCOL);
    return user_copy_from(process, request, user_address, request_size);
}

static ios_status ipc_service_connect_syscall(
    struct ios_process *process,
    const ios_u64 arguments[6]
)
{
    struct ios_user_ipc_connect_request request;
    ios_status status;
    if (!unused_arguments_are_zero(arguments)) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    status = copy_versioned_request(
        process, (ios_uptr)arguments[0], &request, sizeof(request)
    );
    if (IOS_FAILED(status)) return status;
    if (request.flags != 0 || request.reserved32 != 0
        || request.endpoint_handle != 0 || request.service_generation != 0) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    status = ipc_service_connect(
        process, (enum ios_trusted_service)request.service, &request.endpoint_handle
    );
    if (IOS_FAILED(status)) return status;
    request.service_generation = ipc_service_generation(
        (enum ios_trusted_service)request.service
    );
    if (request.service_generation == 0) {
        (void)handle_table_close(&process->handles, request.endpoint_handle);
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    status = user_copy_to(process, (ios_uptr)arguments[0], &request, sizeof(request));
    if (IOS_FAILED(status)) {
        (void)handle_table_close(&process->handles, request.endpoint_handle);
    }
    return status;
}

static ios_status ipc_send_syscall(
    struct ios_process *process,
    const ios_u64 arguments[6]
)
{
    struct ios_user_ipc_send_request request;
    ios_u8 payload[IOS_IPC_MAX_PAYLOAD_SIZE];
    ios_status status;
    if (!unused_arguments_are_zero(arguments)) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    status = copy_versioned_request(
        process, (ios_uptr)arguments[0], &request, sizeof(request)
    );
    if (IOS_FAILED(status)) return status;
    if (request.flags != 0 || request.message_flags != 0 || request.reserved32 != 0
        || request.endpoint_handle == 0 || request.request_id == 0 || request.operation == 0
        || request.item_count > IOS_IPC_MAX_ITEM_COUNT
        || request.payload_size > IOS_IPC_MAX_PAYLOAD_SIZE
        || (request.payload_size == 0) != (request.payload_address == 0)) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    if (request.payload_size != 0) {
        status = user_copy_from(
            process, payload, request.payload_address, request.payload_size
        );
        if (IOS_FAILED(status)) return status;
    }
    return ipc_send(
        process, request.endpoint_handle, request.request_id, request.operation,
        request.message_flags, request.item_count,
        request.payload_size == 0 ? NULL : payload, request.payload_size, false
    );
}

static ios_status ipc_receive_syscall(
    struct ios_process *process,
    const ios_u64 arguments[6]
)
{
    struct ios_user_ipc_receive_request request;
    struct ios_ipc_message message;
    ios_status status;
    if (!unused_arguments_are_zero(arguments)) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    status = copy_versioned_request(
        process, (ios_uptr)arguments[0], &request, sizeof(request)
    );
    if (IOS_FAILED(status)) return status;
    if (request.flags != 0 || request.reserved32 != 0 || request.endpoint_handle == 0
        || request.message_address == 0
        || request.message_capacity < sizeof(message)) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    status = ipc_receive(process, request.endpoint_handle, &message, false);
    if (IOS_FAILED(status)) return status;
    return user_copy_to(
        process, request.message_address, &message, sizeof(message)
    );
}

ios_status syscall_ipc_register(void)
{
    ios_status status = syscall_register(
        IOS_SYSCALL_IPC_SERVICE_CONNECT, ipc_service_connect_syscall
    );
    if (IOS_FAILED(status)) return status;
    status = syscall_register(IOS_SYSCALL_IPC_SEND, ipc_send_syscall);
    if (IOS_FAILED(status)) return status;
    return syscall_register(IOS_SYSCALL_IPC_RECEIVE, ipc_receive_syscall);
}
