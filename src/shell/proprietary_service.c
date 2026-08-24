#include <inferenceos/proprietary_service.h>

#include <inferenceos/runtime.h>

static void rollback_handles(struct ios_process *process, ios_handle *handles, ios_size count)
{
    while (count != 0) (void)handle_table_close(&process->handles, handles[--count]);
}

ios_status ios_proprietary_service_initialize(
    struct ios_proprietary_service *service,
    const struct ios_proprietary_service_config *config)
{
    if (service == NULL || config == NULL || config->type_capabilities == NULL
        || config->resolve_process == NULL || config->enumerate == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    memset(service, 0, sizeof(*service));
    service->config = *config;
    service->initialized = true;
    return IOS_OK;
}

ios_status ios_proprietary_service_dispatch(
    void *context,
    ios_u64 caller_process_id,
    ios_u64 caller_application_identity,
    enum ios_shell_operation operation,
    const struct ios_shell_file_view_request *request,
    struct ios_shell_file_view_reply *reply)
{
    struct ios_proprietary_service *service = context;
    struct ios_proprietary_match matches[IOS_SHELL_FILE_VIEW_REPLY_CAPACITY] = { 0 };
    ios_handle minted[IOS_SHELL_FILE_VIEW_REPLY_CAPACITY] = { 0 };
    struct ios_process *process = NULL;
    ios_type_icon_capability presentation_capability;
    ios_u64 internal_type_identity;
    ios_u64 next_continuation = 0;
    ios_size count = 0;
    ios_size minted_count = 0;
    ios_size capacity;
    ios_status status;

    if (reply != NULL) {
        memset(reply->entries, 0, sizeof(reply->entries));
        reply->item_count = 0;
        reply->continuation = 0;
    }
    if (service == NULL || !service->initialized || request == NULL || reply == NULL
        || operation != IOS_SHELL_TYPE_VIEW || caller_process_id == 0
        || caller_application_identity == 0 || request->type_icon_capability == 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    status = service->config.resolve_process(
        service->config.context, caller_process_id, caller_application_identity, &process);
    if (IOS_FAILED(status)) return status;
    if (process == NULL || process->process_id != caller_process_id
        || process->application_identity != caller_application_identity) {
        return IOS_ERROR(IOS_E_ACCESS_DENIED);
    }
    status = ios_type_capability_resolve(
        service->config.type_capabilities, process,
        (ios_handle)request->type_icon_capability,
        &internal_type_identity, &presentation_capability);
    if (IOS_FAILED(status)) return status;

    capacity = request->maximum_items;
    if (capacity > IOS_SHELL_FILE_VIEW_REPLY_CAPACITY) {
        capacity = IOS_SHELL_FILE_VIEW_REPLY_CAPACITY;
    }
    status = service->config.enumerate(
        service->config.context, request->directory_handle, internal_type_identity,
        request->continuation, matches, capacity, &count, &next_continuation);
    if (IOS_FAILED(status)) return status;
    if (count > capacity) return IOS_ERROR(IOS_E_CORRUPT);

    for (ios_size index = 0; index < count; ++index) {
        struct ios_display_safe_source_entry source;
        if (matches[index].content == NULL || matches[index].base_name == NULL
            || matches[index].internal_type_identity != internal_type_identity
            || ((matches[index].retain_content == NULL)
                != (matches[index].release_content == NULL))) {
            status = IOS_ERROR(IOS_E_CORRUPT);
            goto fail;
        }
        if (matches[index].retain_content != NULL) {
            matches[index].retain_content(matches[index].content);
        }
        status = handle_table_insert(
            &process->handles, matches[index].content, IOS_OBJECT_CONTENT, IOS_RIGHT_READ,
            matches[index].retain_content, matches[index].release_content, &minted[index]);
        if (IOS_FAILED(status)) {
            if (matches[index].release_content != NULL) {
                matches[index].release_content(matches[index].content);
            }
            goto fail;
        }
        ++minted_count;
        source = (struct ios_display_safe_source_entry){
            .base_name = matches[index].base_name,
            .object_handle = minted[index],
            .type_icon_capability = presentation_capability,
            .byte_size = matches[index].byte_size,
            .allowed_operations = IOS_DISPLAY_SAFE_OPERATION_OPEN
                                | IOS_DISPLAY_SAFE_OPERATION_READ,
            .generic_attributes = matches[index].generic_attributes,
            .object_kind = IOS_DISPLAY_SAFE_REGULAR_FILE
        };
        status = ios_display_safe_entry_convert(&source, &reply->entries[index]);
        if (IOS_FAILED(status)) goto fail;
    }
    reply->item_count = (ios_u16)count;
    reply->continuation = next_continuation;
    return IOS_OK;

fail:
    rollback_handles(process, minted, minted_count);
    memset(reply->entries, 0, sizeof(reply->entries));
    reply->item_count = 0;
    reply->continuation = 0;
    return status;
}
