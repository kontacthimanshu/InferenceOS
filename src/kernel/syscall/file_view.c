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
    if (entry->kind == IOS_VFS_OBJECT_REGULAR_FILE
        && (entry->internal_type_identity == 0 || entry->type_prefilter == 0)) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    return IOS_OK;
}

/*
 * VFS type identities pack the one-to-three canonical extension bytes in
 * big-endian order. TYPE_VIEW uses their FNV-1a fingerprint as a binary
 * prefilter, then confirms the authoritative identity to reject collisions.
 * Neither value crosses the application-facing display-safe boundary.
 */
static ios_u64 type_identity_prefilter(ios_u64 identity)
{
    ios_u8 bytes[sizeof(identity)];
    ios_size first = 0;
    ios_u32 hash = UINT32_C(0x811c9dc5);

    for (ios_size index = 0; index < sizeof(bytes); ++index) {
        const ios_size shift = (sizeof(bytes) - 1U - index) * 8U;
        bytes[index] = (ios_u8)(identity >> shift);
    }
    while (first < sizeof(bytes) && bytes[first] == 0) ++first;
    for (ios_size index = first; index < sizeof(bytes); ++index) {
        hash ^= bytes[index];
        hash *= UINT32_C(0x01000193);
    }
    return hash;
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
        .mount_registry = mount_registry,
        .type_catalog = type_catalog,
        .generic_file_capability = generic_file_capability
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
    ios_u64 requested_prefilter = 0;
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
        requested_prefilter = type_identity_prefilter(requested_type);
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
        const bool directory = candidates[index].kind == IOS_VFS_OBJECT_DIRECTORY;
        if ((operation == IOS_SHELL_SEARCH && directory)
            || (!directory && operation != IOS_SHELL_DIRECTORY_VIEW
                && (candidates[index].type_prefilter != requested_prefilter
                    || candidates[index].internal_type_identity != requested_type))) {
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

static ios_status snapshot_directory(
    struct ios_file_view_service *service,
    struct ios_vfs_mount *mount,
    ios_u64 directory_identity,
    ios_size *entry_count
)
{
    ios_u64 continuation;
    ios_status status;
    if (service == NULL || mount == NULL || entry_count == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *entry_count = 0;
    memset(service->candidates, 0, sizeof(service->candidates));
    memset(service->display_entries, 0, sizeof(service->display_entries));
    status = vfs_enumerate(
        mount, directory_identity, 0, service->candidates,
        IOS_FILE_VIEW_DIRECTORY_CAPACITY, entry_count, &continuation
    );
    if (IOS_FAILED(status)) return status;
    if (*entry_count > IOS_FILE_VIEW_DIRECTORY_CAPACITY) {
        *entry_count = 0;
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    if (continuation != 0) {
        *entry_count = 0;
        return IOS_ERROR(IOS_E_NO_SPACE);
    }
    for (ios_size index = 0; index < *entry_count; ++index) {
        status = validate_vfs_entry(&service->candidates[index]);
        if (IOS_FAILED(status)) {
            *entry_count = 0;
            return status;
        }
        status = make_safe_entry(
            service, &service->candidates[index], IOS_INVALID_TYPE_ICON_CAPABILITY,
            &service->display_entries[index]
        );
        if (IOS_FAILED(status)) {
            *entry_count = 0;
            return status;
        }
    }
    status = ios_display_safe_entries_disambiguate(
        service->display_entries, *entry_count,
        service->disambiguation_workspace, IOS_FILE_VIEW_DIRECTORY_CAPACITY
    );
    if (IOS_FAILED(status)) *entry_count = 0;
    return status;
}

static bool display_component_equal(
    const char *left,
    ios_size left_length,
    const char *right,
    ios_size right_length
)
{
    if (left == NULL || right == NULL || left_length != right_length) return false;
    for (ios_size index = 0; index < left_length; ++index) {
        unsigned char left_byte = (unsigned char)left[index];
        unsigned char right_byte = (unsigned char)right[index];
        if (left_byte >= 'a' && left_byte <= 'z') left_byte -= 'a' - 'A';
        if (right_byte >= 'a' && right_byte <= 'z') right_byte -= 'a' - 'A';
        if (left_byte != right_byte) return false;
    }
    return true;
}

static ios_status resolve_normalized_display_path(
    struct ios_file_view_service *service,
    struct ios_vfs_mount *mount,
    const char normalized[IOS_VFS_PATH_CAPACITY],
    struct ios_file_view_resolved_object *resolved
)
{
    ios_u64 current = mount->root->identity;
    ios_size cursor = 1;
    if (normalized[1] == '\0') {
        *resolved = (struct ios_file_view_resolved_object){
            .mount = mount,
            .object_identity = current,
            .parent_identity = current,
            .allowed_operations = IOS_VFS_FILE_OPEN | IOS_VFS_FILE_READ
                | IOS_VFS_FILE_ENUMERATE,
            .kind = IOS_VFS_OBJECT_DIRECTORY
        };
        return IOS_OK;
    }
    while (normalized[cursor] != '\0') {
        ios_size begin = cursor;
        ios_size component_length;
        ios_size entry_count;
        ios_size selected = SIZE_MAX;
        ios_status status;
        while (normalized[cursor] != '\0' && normalized[cursor] != '/') ++cursor;
        component_length = cursor - begin;
        status = snapshot_directory(service, mount, current, &entry_count);
        if (IOS_FAILED(status)) return status;
        for (ios_size index = 0; index < entry_count; ++index) {
            const struct ios_display_safe_entry *entry = &service->display_entries[index];
            if (display_component_equal(
                    entry->display_name, entry->display_name_length,
                    normalized + begin, component_length
                )) {
                if (selected != SIZE_MAX) return IOS_ERROR(IOS_E_CORRUPT);
                selected = index;
            }
        }
        if (selected == SIZE_MAX) return IOS_ERROR(IOS_E_NOT_FOUND);
        const struct ios_vfs_directory_entry *candidate = &service->candidates[selected];
        *resolved = (struct ios_file_view_resolved_object){
            .mount = mount,
            .object_identity = candidate->object_identity,
            .parent_identity = current,
            .internal_type_identity = candidate->internal_type_identity,
            .byte_size = candidate->byte_size,
            .allowed_operations = candidate->allowed_operations,
            .generic_attributes = candidate->generic_attributes,
            .kind = candidate->kind
        };
        if (normalized[cursor] == '/') {
            if (candidate->kind != IOS_VFS_OBJECT_DIRECTORY) {
                return IOS_ERROR(IOS_E_NOT_FOUND);
            }
            current = candidate->object_identity;
            ++cursor;
        }
    }
    return IOS_OK;
}

ios_status ios_file_view_resolve_display_path(
    struct ios_file_view_service *service,
    const struct ios_vfs_path_context *path_context,
    const char *path,
    struct ios_file_view_resolved_object *resolved
)
{
    char normalized[IOS_VFS_PATH_CAPACITY];
    struct ios_vfs_mount *mount;
    ios_status status;
    if (resolved == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    memset(resolved, 0, sizeof(*resolved));
    if (service == NULL || path_context == NULL || path == NULL
        || service->mount_registry == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    mount = vfs_root_mount(service->mount_registry);
    if (mount == NULL || path_context->mount != mount
        || path_context->mount_generation != mount->generation) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    status = vfs_path_normalize(
        path_context->current_directory, path, normalized, sizeof(normalized)
    );
    if (IOS_FAILED(status)) return status;
    status = resolve_normalized_display_path(service, mount, normalized, resolved);
    if (IOS_FAILED(status)) memset(resolved, 0, sizeof(*resolved));
    return status;
}

ios_status ios_file_view_list_directory_path(
    struct ios_file_view_service *service,
    const struct ios_vfs_path_context *path_context,
    const char *path,
    struct ios_display_safe_entry *entries,
    ios_size capacity,
    ios_size *entry_count
)
{
    struct ios_vfs_object directory;
    ios_size count;
    ios_status status;
    if (entries == NULL || capacity == 0 || entry_count == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *entry_count = 0;
    if (service == NULL || path_context == NULL || path == NULL
        || service->mount_registry == NULL
        || path_context->mount != vfs_root_mount(service->mount_registry)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    status = vfs_path_resolve(path_context, path, &directory);
    if (IOS_FAILED(status)) return status;
    if (directory.kind != IOS_VFS_OBJECT_DIRECTORY) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    status = snapshot_directory(service, path_context->mount, directory.identity, &count);
    if (IOS_FAILED(status)) return status;
    if (count > capacity) return IOS_ERROR(IOS_E_NO_SPACE);
    memcpy(entries, service->display_entries, count * sizeof(*entries));
    *entry_count = count;
    return IOS_OK;
}
