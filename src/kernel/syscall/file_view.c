#include <inferenceos/file_view.h>

#include <inferenceos/runtime.h>

static ios_status validate_vfs_entry(const struct ios_vfs_directory_entry *entry)
{
    const ios_u64 allowed_operation_mask = IOS_VFS_FILE_OPEN | IOS_VFS_FILE_READ
        | IOS_VFS_FILE_WRITE | IOS_VFS_FILE_RENAME | IOS_VFS_FILE_DELETE
        | IOS_VFS_FILE_ENUMERATE;
    ios_size length = strnlen(entry->display_base_name, IOS_VFS_DISPLAY_NAME_CAPACITY);
    if (length == 0 || length >= IOS_VFS_DISPLAY_NAME_CAPACITY
        || entry->object_identity == 0
        || (entry->kind != IOS_VFS_OBJECT_DIRECTORY
            && entry->kind != IOS_VFS_OBJECT_REGULAR_FILE)
        || (entry->allowed_operations & ~allowed_operation_mask) != 0
        || (entry->generic_attributes & ~IOS_VFS_ATTRIBUTE_READ_ONLY) != 0) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    if (entry->kind == IOS_VFS_OBJECT_REGULAR_FILE && entry->internal_type_identity == 0) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    return IOS_OK;
}

static ios_status make_safe_entry(
    const struct ios_file_view_service *service,
    const struct ios_vfs_directory_entry *source,
    ios_type_icon_capability type_capability,
    struct ios_display_safe_entry *destination
)
{
    ios_status status;
    if (source->kind == IOS_VFS_OBJECT_REGULAR_FILE
        && type_capability == IOS_INVALID_TYPE_ICON_CAPABILITY) {
        status = ios_type_catalog_find_capability(
            service->type_catalog, source->internal_type_identity, &type_capability
        );
        if (status == IOS_ERROR(IOS_E_NOT_FOUND)) {
            type_capability = service->generic_file_capability;
        } else if (IOS_FAILED(status)) {
            return status;
        }
    }
    const struct ios_display_safe_source_entry safe_source = {
        .base_name = source->display_base_name,
        .object_handle = source->object_identity,
        .type_icon_capability = source->kind == IOS_VFS_OBJECT_DIRECTORY
            ? IOS_INVALID_TYPE_ICON_CAPABILITY : type_capability,
        .byte_size = source->byte_size,
        .allowed_operations = source->allowed_operations,
        .generic_attributes = source->generic_attributes,
        .object_kind = source->kind == IOS_VFS_OBJECT_DIRECTORY
            ? IOS_DISPLAY_SAFE_DIRECTORY : IOS_DISPLAY_SAFE_REGULAR_FILE
    };
    return ios_display_safe_entry_convert(&safe_source, destination);
}

ios_status ios_file_view_service_initialize(
    struct ios_file_view_service *service,
    struct ios_vfs_mount_registry *mount_registry,
    const struct ios_type_catalog *type_catalog,
    ios_type_icon_capability generic_file_capability
)
{
    enum ios_presentation_icon fallback_icon;
    if (service == NULL || mount_registry == NULL || type_catalog == NULL
        || generic_file_capability == IOS_INVALID_TYPE_ICON_CAPABILITY
        || IOS_FAILED(ios_type_catalog_resolve_icon(
            type_catalog, generic_file_capability,
            IOS_TYPE_CATALOG_REGULAR_FILE, &fallback_icon
        )) || fallback_icon != IOS_ICON_GENERIC_FILE) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *service = (struct ios_file_view_service){
        mount_registry, type_catalog, generic_file_capability
    };
    return IOS_OK;
}

ios_status ios_file_view_dispatch(
    void *context,
    ios_u64 caller_process_id,
    ios_u64 caller_application_identity,
    enum ios_shell_operation operation,
    const struct ios_shell_file_view_request *request,
    struct ios_shell_file_view_reply *reply
)
{
    struct ios_file_view_service *service = context;
    struct ios_vfs_directory_entry candidates[IOS_SHELL_FILE_VIEW_REPLY_CAPACITY];
    struct ios_vfs_mount *mount;
    ios_u64 requested_type = 0;
    ios_u64 next_continuation;
    ios_size candidate_count;
    ios_status status;
    if (service == NULL || request == NULL || reply == NULL
        || caller_process_id == 0 || caller_application_identity == 0
        || (operation != IOS_SHELL_DIRECTORY_VIEW && operation != IOS_SHELL_TYPE_VIEW
            && operation != IOS_SHELL_SEARCH)
        || request->maximum_items == 0
        || request->maximum_items > IOS_SHELL_FILE_VIEW_REPLY_CAPACITY) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (operation != IOS_SHELL_DIRECTORY_VIEW) {
        status = ios_type_catalog_resolve_identity(
            service->type_catalog, request->type_icon_capability, &requested_type
        );
        if (IOS_FAILED(status)) return status;
    }
    mount = vfs_root_mount(service->mount_registry);
    if (mount == NULL) return IOS_ERROR(IOS_E_NOT_FOUND);
    status = vfs_enumerate(
        mount, request->directory_handle, request->continuation, candidates,
        request->maximum_items, &candidate_count, &next_continuation
    );
    if (IOS_FAILED(status)) return status;
    reply->item_count = 0;
    reply->continuation = next_continuation;
    for (ios_size index = 0; index < candidate_count; ++index) {
        status = validate_vfs_entry(&candidates[index]);
        if (IOS_FAILED(status)) return status;
        if (operation != IOS_SHELL_DIRECTORY_VIEW
            && candidates[index].internal_type_identity != requested_type) {
            continue;
        }
        status = make_safe_entry(
            service, &candidates[index], request->type_icon_capability,
            &reply->entries[reply->item_count]
        );
        if (IOS_FAILED(status)) return status;
        ++reply->item_count;
    }
    return IOS_OK;
}
