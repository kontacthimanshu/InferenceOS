#include <inferenceos/gui/file_explorer.h>

#include <inferenceos/runtime.h>

enum { IOS_FILE_EXPLORER_MAX_REPLY_PAGES = 64 };

static void disconnect_channel(struct ios_file_explorer_client *client)
{
    if (client->connected) {
        (void)handle_table_close(&client->process->handles, client->shell_channel);
    }
    client->shell_channel = IOS_INVALID_HANDLE;
    client->shell_generation = 0;
    client->connected = false;
}

static ios_status connect_channel(struct ios_file_explorer_client *client)
{
    ios_status status;
    disconnect_channel(client);
    status = ipc_service_connect(
        client->process, IOS_SERVICE_SHELL, &client->shell_channel
    );
    if (IOS_FAILED(status)) return status;
    client->shell_generation = ipc_service_generation(IOS_SERVICE_SHELL);
    if (client->shell_generation == 0) {
        disconnect_channel(client);
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    client->connected = true;
    return IOS_OK;
}

static ios_status next_request_id(
    struct ios_file_explorer_client *client, ios_u64 *request_id
)
{
    if (client->next_request_id == 0 || client->next_request_id == UINT64_MAX) {
        return IOS_ERROR(IOS_E_OVERFLOW);
    }
    *request_id = client->next_request_id++;
    return IOS_OK;
}

static ios_status exchange(
    struct ios_file_explorer_client *client,
    enum ios_shell_operation operation,
    const void *payload,
    ios_u16 payload_size,
    struct ios_shell_dispatch_result *result
)
{
    ios_u64 request_id;
    ios_status status = next_request_id(client, &request_id);
    if (IOS_FAILED(status)) return status;
    if (!client->connected
        || client->shell_generation != ipc_service_generation(IOS_SERVICE_SHELL)) {
        status = connect_channel(client);
        if (IOS_FAILED(status)) return status;
    }
    status = ios_shell_service_exchange(
        client->shell_service, client->process, client->shell_channel,
        request_id, operation, payload, payload_size, result
    );
    if (status != IOS_ERROR(IOS_E_INVALID_STATE)) return status;
    status = connect_channel(client);
    if (IOS_FAILED(status)) return status;
    return ios_shell_service_exchange(
        client->shell_service, client->process, client->shell_channel,
        request_id, operation, payload, payload_size, result
    );
}

ios_status ios_file_explorer_client_initialize(
    struct ios_file_explorer_client *client,
    struct ios_process *process,
    struct ios_shell_service *shell_service,
    ios_file_explorer_resolve_icon_function resolve_icon,
    void *resolve_icon_context
)
{
    if (client == NULL || process == NULL || shell_service == NULL || resolve_icon == NULL
        || process->process_id == 0 || process->application_identity == 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *client = (struct ios_file_explorer_client){
        .process = process,
        .shell_service = shell_service,
        .resolve_icon = resolve_icon,
        .resolve_icon_context = resolve_icon_context,
        .next_request_id = 1,
        .next_render_sequence = 1
    };
    return connect_channel(client);
}

void ios_file_explorer_client_disconnect(struct ios_file_explorer_client *client)
{
    if (client == NULL || client->process == NULL) return;
    disconnect_channel(client);
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
    ios_u64 continuation = 0;
    ios_size count = 0;
    if (client == NULL || entries == NULL || entry_count == NULL || capacity == 0
        || directory_handle == IOS_INVALID_HANDLE
        || (operation != IOS_SHELL_DIRECTORY_VIEW && operation != IOS_SHELL_TYPE_VIEW
            && operation != IOS_SHELL_SEARCH)
        || (operation == IOS_SHELL_DIRECTORY_VIEW
            && type_icon_capability != IOS_INVALID_TYPE_ICON_CAPABILITY)
        || (operation != IOS_SHELL_DIRECTORY_VIEW
            && type_icon_capability == IOS_INVALID_TYPE_ICON_CAPABILITY)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *entry_count = 0;
    for (ios_size page = 0; page < IOS_FILE_EXPLORER_MAX_REPLY_PAGES; ++page) {
        const ios_size remaining = capacity - count;
        const ios_u16 maximum_items = (ios_u16)(
            remaining < IOS_SHELL_FILE_VIEW_REPLY_CAPACITY
                ? remaining : IOS_SHELL_FILE_VIEW_REPLY_CAPACITY
        );
        const struct ios_shell_file_view_request request = {
            .size = sizeof(request),
            .version = IOS_SHELL_PROTOCOL_VERSION,
            .directory_handle = directory_handle,
            .type_icon_capability = type_icon_capability,
            .continuation = continuation,
            .maximum_items = maximum_items
        };
        struct ios_shell_dispatch_result result;
        ios_status status = exchange(
            client, operation, &request, sizeof(request), &result
        );
        if (IOS_FAILED(status)) return status;
        const struct ios_shell_file_view_reply *reply = &result.payload.file_view;
        if (result.operation != operation || result.payload_size != sizeof(*reply)
            || reply->size != sizeof(*reply) || reply->version != IOS_SHELL_PROTOCOL_VERSION
            || reply->status != IOS_OK || reply->flags != 0
            || reply->item_count != result.item_count
            || reply->item_count > maximum_items
            || (reply->continuation != 0 && reply->continuation == continuation)) {
            return IOS_ERROR(IOS_E_PROTOCOL);
        }
        memcpy(
            &entries[count], reply->entries,
            reply->item_count * sizeof(struct ios_display_safe_entry)
        );
        count += reply->item_count;
        if (reply->continuation == 0 || count == capacity) {
            *entry_count = count;
            return IOS_OK;
        }
        continuation = reply->continuation;
    }
    return IOS_ERROR(IOS_E_OVERFLOW);
}

static ios_status enumerate_directory(
    void *context,
    ios_u64 directory_handle,
    struct ios_display_safe_entry *entries,
    ios_size capacity,
    ios_size *entry_count
)
{
    return ios_file_explorer_client_file_view(
        context, IOS_SHELL_DIRECTORY_VIEW, directory_handle,
        IOS_INVALID_TYPE_ICON_CAPABILITY, entries, capacity, entry_count
    );
}

static ios_status resolve_icon(
    void *context,
    ios_u64 type_icon_capability,
    ios_u32 object_kind,
    enum ios_presentation_icon *icon
)
{
    struct ios_file_explorer_client *client = context;
    return client->resolve_icon(
        client->resolve_icon_context, type_icon_capability, object_kind, icon
    );
}

struct ios_file_explorer_view_provider ios_file_explorer_client_provider(
    struct ios_file_explorer_client *client
)
{
    return (struct ios_file_explorer_view_provider){
        client, enumerate_directory, resolve_icon
    };
}

ios_status ios_file_explorer_client_render(
    struct ios_file_explorer_client *client,
    ios_handle window_handle,
    ios_handle view_handle
)
{
    struct ios_shell_dispatch_result result;
    ios_status status;
    if (client == NULL || window_handle == IOS_INVALID_HANDLE
        || view_handle == IOS_INVALID_HANDLE || client->next_render_sequence == UINT64_MAX) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    const struct ios_shell_gui_view_request request = {
        .size = sizeof(request),
        .version = IOS_SHELL_PROTOCOL_VERSION,
        .window_handle = window_handle,
        .view_handle = view_handle,
        .render_sequence = client->next_render_sequence
    };
    status = exchange(client, IOS_SHELL_GUI_VIEW, &request, sizeof(request), &result);
    if (IOS_FAILED(status)) return status;
    const struct ios_shell_gui_view_reply *reply = &result.payload.gui_view;
    if (result.operation != IOS_SHELL_GUI_VIEW || result.payload_size != sizeof(*reply)
        || reply->size != sizeof(*reply) || reply->version != IOS_SHELL_PROTOCOL_VERSION
        || reply->flags != 0 || reply->status != IOS_OK
        || reply->render_sequence != request.render_sequence) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    ++client->next_render_sequence;
    return IOS_OK;
}

ios_status ios_file_explorer_client_refresh(
    struct ios_file_explorer_client *client,
    struct ios_file_explorer_model *model
)
{
    if (client == NULL || model == NULL || model->provider.context != client) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    return ios_file_explorer_model_refresh(model);
}
