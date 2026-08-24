#include <inferenceos/proprietary_test.h>

#include <inferenceos/runtime.h>

enum { IOS_PROPRIETARY_TEST_MAX_PAGES = 64 };

static ios_status connect_shell(struct ios_proprietary_test_application *application)
{
    ios_status status = ipc_service_connect(
        application->process, IOS_SERVICE_SHELL, &application->shell_channel);
    if (IOS_FAILED(status)) return status;
    application->connected = true;
    return IOS_OK;
}

static void close_entries(struct ios_proprietary_test_application *application)
{
    while (application->entry_count != 0) {
        --application->entry_count;
        (void)handle_table_close(
            &application->process->handles,
            application->entries[application->entry_count].object_handle);
    }
    memset(application->entries, 0, sizeof(application->entries));
}

void ios_proprietary_test_disconnect(struct ios_proprietary_test_application *application)
{
    if (application == NULL || application->process == NULL) return;
    close_entries(application);
    if (application->connected) {
        (void)handle_table_close(&application->process->handles, application->shell_channel);
    }
    application->shell_channel = IOS_INVALID_HANDLE;
    application->connected = false;
}

ios_status ios_proprietary_test_initialize(
    struct ios_proprietary_test_application *application,
    struct ios_process *process,
    struct ios_shell_service *shell,
    ios_handle directory_handle,
    ios_handle type_capability)
{
    if (application == NULL || process == NULL || shell == NULL
        || process->process_id == 0 || process->application_identity == 0
        || directory_handle == IOS_INVALID_HANDLE
        || type_capability == IOS_INVALID_HANDLE) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    memset(application, 0, sizeof(*application));
    application->process = process;
    application->shell = shell;
    application->directory_handle = directory_handle;
    application->type_capability = type_capability;
    application->next_request_id = 1;
    return connect_shell(application);
}

static ios_status validate_entry(
    const struct ios_proprietary_test_application *application,
    const struct ios_display_safe_entry *entry)
{
    void *content = NULL;
    if (entry->size != sizeof(*entry) || entry->version != IOS_DISPLAY_SAFE_ENTRY_VERSION
        || entry->object_kind != IOS_DISPLAY_SAFE_REGULAR_FILE
        || entry->type_icon_capability == 0
        || entry->display_name_length == 0
        || entry->display_name_length >= IOS_DISPLAY_SAFE_NAME_CAPACITY
        || entry->allowed_operations
            != (IOS_DISPLAY_SAFE_OPERATION_OPEN | IOS_DISPLAY_SAFE_OPERATION_READ)) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    for (ios_size index = 0; index < entry->display_name_length; ++index) {
        if (entry->display_name[index] == '.' || entry->display_name[index] == '/'
            || entry->display_name[index] == '\\') return IOS_ERROR(IOS_E_PROTOCOL);
    }
    if (entry->display_name[entry->display_name_length] != '\0') {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    return handle_table_resolve(
        &application->process->handles, entry->object_handle,
        IOS_OBJECT_CONTENT, IOS_RIGHT_READ, &content);
}

ios_status ios_proprietary_test_run(struct ios_proprietary_test_application *application)
{
    ios_u64 continuation = 0;
    if (application == NULL || application->process == NULL || application->shell == NULL
        || !application->connected) return IOS_ERROR(IOS_E_INVALID_STATE);
    close_entries(application);
    for (ios_size page = 0; page < IOS_PROPRIETARY_TEST_MAX_PAGES; ++page) {
        const ios_size remaining = IOS_PROPRIETARY_TEST_ENTRY_CAPACITY
                                 - application->entry_count;
        const ios_u16 maximum = (ios_u16)(remaining < IOS_SHELL_FILE_VIEW_REPLY_CAPACITY
            ? remaining : IOS_SHELL_FILE_VIEW_REPLY_CAPACITY);
        const struct ios_shell_file_view_request request = {
            .size = sizeof(request),
            .version = IOS_SHELL_PROTOCOL_VERSION,
            .directory_handle = application->directory_handle,
            .type_icon_capability = application->type_capability,
            .continuation = continuation,
            .maximum_items = maximum
        };
        struct ios_shell_dispatch_result result;
        ios_status status;
        if (application->next_request_id == 0) return IOS_ERROR(IOS_E_OVERFLOW);
        status = ios_shell_service_exchange(
            application->shell, application->process, application->shell_channel,
            application->next_request_id++, IOS_SHELL_TYPE_VIEW,
            &request, sizeof(request), &result);
        if (IOS_FAILED(status)) return status;
        const struct ios_shell_file_view_reply *reply = &result.payload.file_view;
        if (result.operation != IOS_SHELL_TYPE_VIEW || result.payload_size != sizeof(*reply)
            || reply->status != IOS_OK || reply->item_count != result.item_count
            || reply->item_count > maximum
            || (reply->continuation != 0 && reply->continuation == continuation)) {
            return IOS_ERROR(IOS_E_PROTOCOL);
        }
        for (ios_size index = 0; index < reply->item_count; ++index) {
            status = validate_entry(application, &reply->entries[index]);
            if (IOS_FAILED(status)) return status;
            application->entries[application->entry_count++] = reply->entries[index];
        }
        if (reply->continuation == 0) return IOS_OK;
        if (application->entry_count == IOS_PROPRIETARY_TEST_ENTRY_CAPACITY) {
            return IOS_ERROR(IOS_E_NO_SPACE);
        }
        continuation = reply->continuation;
    }
    return IOS_ERROR(IOS_E_OVERFLOW);
}
