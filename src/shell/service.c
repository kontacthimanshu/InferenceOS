#include <inferenceos/shell_protocol.h>

#include <inferenceos/runtime.h>

static bool shell_operation_is_file_view(ios_u32 operation)
{
    return operation == IOS_SHELL_DIRECTORY_VIEW
        || operation == IOS_SHELL_TYPE_VIEW
        || operation == IOS_SHELL_SEARCH;
}

static ios_status validate_transport_header(const struct ios_ipc_message_header *header)
{
    if (header->size < sizeof(*header)
        || header->size != sizeof(*header) + header->payload_size
        || header->version != IOS_IPC_ABI_VERSION || header->request_id == 0
        || header->caller_process_id == 0 || header->caller_application_identity == 0
        || header->flags != 0 || header->item_count != 0
        || header->payload_size > IOS_IPC_MAX_PAYLOAD_SIZE) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    if (!shell_operation_is_file_view(header->operation)
        && header->operation != IOS_SHELL_GUI_VIEW) {
        return IOS_ERROR(IOS_E_UNKNOWN_SYSCALL);
    }
    return IOS_OK;
}

static ios_status validate_file_view_request(
    ios_u32 operation, const struct ios_shell_file_view_request *request
)
{
    if (request->size != sizeof(*request)
        || request->version != IOS_SHELL_PROTOCOL_VERSION || request->flags != 0
        || request->directory_handle == IOS_INVALID_HANDLE
        || request->maximum_items == 0
        || request->maximum_items > IOS_SHELL_FILE_VIEW_REPLY_CAPACITY
        || request->reserved16 != 0 || request->reserved32 != 0) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    if (operation == IOS_SHELL_DIRECTORY_VIEW) {
        if (request->type_icon_capability != IOS_INVALID_HANDLE) {
            return IOS_ERROR(IOS_E_PROTOCOL);
        }
    } else if (request->type_icon_capability == IOS_INVALID_HANDLE) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    return IOS_OK;
}

static ios_status validate_gui_view_request(
    const struct ios_shell_gui_view_request *request
)
{
    if (request->size != sizeof(*request)
        || request->version != IOS_SHELL_PROTOCOL_VERSION || request->flags != 0
        || request->window_handle == IOS_INVALID_HANDLE
        || request->view_handle == IOS_INVALID_HANDLE || request->render_sequence == 0) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    return IOS_OK;
}

ios_status ios_shell_message_validate(const struct ios_ipc_message *message)
{
    ios_status status;
    if (message == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    status = validate_transport_header(&message->header);
    if (IOS_FAILED(status)) return status;
    if (shell_operation_is_file_view(message->header.operation)) {
        struct ios_shell_file_view_request request;
        if (message->header.payload_size != sizeof(request)) {
            return IOS_ERROR(IOS_E_PROTOCOL);
        }
        memcpy(&request, message->payload, sizeof(request));
        return validate_file_view_request(message->header.operation, &request);
    }
    struct ios_shell_gui_view_request request;
    if (message->header.payload_size != sizeof(request)) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    memcpy(&request, message->payload, sizeof(request));
    return validate_gui_view_request(&request);
}

static ios_status validate_safe_entry(const struct ios_display_safe_entry *entry)
{
    if (entry->size != sizeof(*entry) || entry->version != IOS_DISPLAY_SAFE_ENTRY_VERSION
        || entry->object_handle == IOS_INVALID_HANDLE
        || entry->display_name_length == 0
        || entry->display_name_length >= IOS_DISPLAY_SAFE_NAME_CAPACITY
        || entry->display_name[entry->display_name_length] != '\0'
        || (entry->object_kind != IOS_DISPLAY_SAFE_REGULAR_FILE
            && entry->object_kind != IOS_DISPLAY_SAFE_DIRECTORY)) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    for (ios_size index = 0; index < IOS_ARRAY_COUNT(entry->reserved); ++index) {
        if (entry->reserved[index] != 0) return IOS_ERROR(IOS_E_PROTOCOL);
    }
    return IOS_OK;
}

ios_status ios_shell_service_start(
    struct ios_shell_service *service,
    const struct ios_shell_service_config *config
)
{
    ios_status status;
    if (service == NULL || config == NULL || config->process == NULL
        || config->process->process_id == 0 || config->process->application_identity == 0
        || config->queue_depth == 0 || config->queue_depth > IOS_IPC_MAX_QUEUE_DEPTH) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (service->running) return IOS_ERROR(IOS_E_ALREADY_EXISTS);
    *service = (struct ios_shell_service){ .config = *config };
    status = ipc_trust_service(IOS_SERVICE_SHELL, config->process->application_identity);
    if (IOS_FAILED(status)) return status;
    status = ipc_endpoint_create(config->process, config->queue_depth, &service->endpoint_handle);
    if (IOS_FAILED(status)) return status;
    status = ipc_service_register(
        config->process, IOS_SERVICE_SHELL, service->endpoint_handle
    );
    if (IOS_FAILED(status)) {
        (void)handle_table_close(&config->process->handles, service->endpoint_handle);
        service->endpoint_handle = IOS_INVALID_HANDLE;
        return status;
    }
    service->generation = ipc_service_generation(IOS_SERVICE_SHELL);
    service->running = true;
    return IOS_OK;
}

ios_status ios_shell_service_stop(struct ios_shell_service *service)
{
    ios_status status;
    if (service == NULL || !service->running || service->config.process == NULL) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    status = ipc_service_unregister(service->config.process, IOS_SERVICE_SHELL);
    if (IOS_FAILED(status)) return status;
    status = handle_table_close(
        &service->config.process->handles, service->endpoint_handle
    );
    service->endpoint_handle = IOS_INVALID_HANDLE;
    service->generation = ipc_service_generation(IOS_SERVICE_SHELL);
    service->running = false;
    return status;
}

ios_status ios_shell_service_restart(struct ios_shell_service *service)
{
    struct ios_shell_service_config config;
    ios_status status;
    if (service == NULL || !service->running) return IOS_ERROR(IOS_E_INVALID_STATE);
    config = service->config;
    status = ios_shell_service_stop(service);
    if (IOS_FAILED(status)) return status;
    return ios_shell_service_start(service, &config);
}

static ios_status dispatch_file_view(
    struct ios_shell_service *service,
    const struct ios_ipc_message *message,
    struct ios_shell_dispatch_result *result
)
{
    struct ios_shell_file_view_request request;
    struct ios_shell_file_view_reply *reply = &result->payload.file_view;
    ios_status status;
    memcpy(&request, message->payload, sizeof(request));
    *reply = (struct ios_shell_file_view_reply){
        .size = sizeof(*reply), .version = IOS_SHELL_PROTOCOL_VERSION
    };
    if (service->config.dispatch_file_view == NULL) {
        status = IOS_ERROR(IOS_E_NOT_SUPPORTED);
    } else {
        status = service->config.dispatch_file_view(
            service->config.dispatch_context,
            message->header.caller_process_id,
            message->header.caller_application_identity,
            (enum ios_shell_operation)message->header.operation,
            &request,
            reply
        );
    }
    reply->status = status;
    if (reply->size != sizeof(*reply) || reply->version != IOS_SHELL_PROTOCOL_VERSION
        || reply->flags != 0 || reply->reserved16 != 0 || reply->reserved32 != 0
        || reply->item_count > request.maximum_items
        || reply->item_count > IOS_SHELL_FILE_VIEW_REPLY_CAPACITY) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    for (ios_size index = 0; index < reply->item_count; ++index) {
        ios_status entry_status = validate_safe_entry(&reply->entries[index]);
        if (IOS_FAILED(entry_status)) return entry_status;
    }
    result->operation = message->header.operation;
    result->item_count = reply->item_count;
    result->payload_size = sizeof(*reply);
    return status;
}

static ios_status dispatch_gui_view(
    struct ios_shell_service *service,
    const struct ios_ipc_message *message,
    struct ios_shell_dispatch_result *result
)
{
    struct ios_shell_gui_view_request request;
    struct ios_shell_gui_view_reply *reply = &result->payload.gui_view;
    ios_status status;
    memcpy(&request, message->payload, sizeof(request));
    *reply = (struct ios_shell_gui_view_reply){
        .size = sizeof(*reply),
        .version = IOS_SHELL_PROTOCOL_VERSION,
        .render_sequence = request.render_sequence
    };
    status = service->config.dispatch_gui_view == NULL
        ? IOS_ERROR(IOS_E_NOT_SUPPORTED)
        : service->config.dispatch_gui_view(
            service->config.dispatch_context,
            message->header.caller_process_id,
            message->header.caller_application_identity,
            &request,
            reply
        );
    reply->status = status;
    if (reply->size != sizeof(*reply) || reply->version != IOS_SHELL_PROTOCOL_VERSION
        || reply->flags != 0 || reply->render_sequence != request.render_sequence) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    result->operation = message->header.operation;
    result->item_count = 0;
    result->payload_size = sizeof(*reply);
    return status;
}

ios_status ios_shell_service_dispatch(
    struct ios_shell_service *service,
    const struct ios_ipc_message *message,
    struct ios_shell_dispatch_result *result
)
{
    ios_status status;
    if (service == NULL || result == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    if (!service->running) return IOS_ERROR(IOS_E_INVALID_STATE);
    status = ios_shell_message_validate(message);
    if (IOS_FAILED(status)) return status;
    *result = (struct ios_shell_dispatch_result){ 0 };
    result->request_id = message->header.request_id;
    return shell_operation_is_file_view(message->header.operation)
        ? dispatch_file_view(service, message, result)
        : dispatch_gui_view(service, message, result);
}

ios_status ios_shell_service_receive_and_dispatch(
    struct ios_shell_service *service,
    struct ios_shell_dispatch_result *result,
    bool block
)
{
    struct ios_ipc_message message;
    ios_status status;
    if (service == NULL || result == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    if (!service->running) return IOS_ERROR(IOS_E_INVALID_STATE);
    status = ipc_receive(
        service->config.process, service->endpoint_handle, &message, block
    );
    if (IOS_FAILED(status)) return status;
    return ios_shell_service_dispatch(service, &message, result);
}

ios_status ios_shell_service_exchange(
    struct ios_shell_service *service,
    struct ios_process *client,
    ios_handle shell_channel,
    ios_u64 request_id,
    enum ios_shell_operation operation,
    const void *payload,
    ios_u16 payload_size,
    struct ios_shell_dispatch_result *result
)
{
    ios_status status;
    if (service == NULL || client == NULL || result == NULL || !service->running) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    *result = (struct ios_shell_dispatch_result){ 0 };
    status = ipc_send(
        client, shell_channel, request_id, (ios_u32)operation, 0, 0,
        payload, payload_size, false
    );
    if (IOS_FAILED(status)) return status;
    status = ios_shell_service_receive_and_dispatch(service, result, false);
    if (result->request_id != request_id) return IOS_ERROR(IOS_E_PROTOCOL);
    return status;
}
