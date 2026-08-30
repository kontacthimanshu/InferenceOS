#include <inferenceos/file_command.h>

#include <inferenceos/runtime.h>

enum { IOS_FILE_COMMAND_TEXT_CHECK_CHUNK = 256 };

static bool supported_text_bytes(const void *bytes, ios_size length)
{
    const ios_u8 *text = bytes;
    if (bytes == NULL && length != 0) return false;
    for (ios_size index = 0; index < length; ++index) {
        const ios_u8 byte = text[index];
        if ((byte < 0x20 || byte > 0x7e)
            && byte != '\t' && byte != '\n' && byte != '\r') {
            return false;
        }
    }
    return true;
}

ios_status ios_file_command_service_initialize(
    struct ios_file_command_service *service,
    struct ios_file_view_service *file_view,
    struct ios_vfs_path_context *path_context
)
{
    if (service == NULL || file_view == NULL || path_context == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *service = (struct ios_file_command_service){ file_view, path_context };
    return IOS_OK;
}

static ios_status validate_service(const struct ios_file_command_service *service)
{
    return service != NULL && service->file_view != NULL
        && service->path_context != NULL
        ? IOS_OK : IOS_ERROR(IOS_E_INVALID_STATE);
}

static ios_status resolve_regular(
    struct ios_file_command_service *service,
    const char *display_path,
    ios_u64 required_operation,
    struct ios_file_view_resolved_object *resolved
)
{
    ios_status status = validate_service(service);
    if (IOS_FAILED(status)) return status;
    status = ios_file_view_resolve_display_path(
        service->file_view, service->path_context, display_path, resolved
    );
    if (IOS_FAILED(status)) return status;
    if (resolved->kind != IOS_VFS_OBJECT_REGULAR_FILE) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (required_operation != 0
        && (resolved->allowed_operations & required_operation) != required_operation) {
        if ((resolved->generic_attributes & IOS_VFS_ATTRIBUTE_READ_ONLY) != 0
            || resolved->mount->state != IOS_MOUNT_RW) {
            return IOS_ERROR(IOS_E_READ_ONLY);
        }
        return IOS_ERROR(IOS_E_ACCESS_DENIED);
    }
    return IOS_OK;
}

static ios_status validate_existing_text(
    const struct ios_file_view_resolved_object *resolved
)
{
    ios_u8 bytes[IOS_FILE_COMMAND_TEXT_CHECK_CHUNK];
    ios_u64 offset = 0;
    bool complete = resolved->byte_size == 0;
    while (!complete) {
        ios_size transferred;
        ios_status status = vfs_read_object(
            resolved->mount, resolved->object_identity, offset,
            bytes, sizeof(bytes), &transferred, &complete
        );
        if (IOS_FAILED(status)) return status;
        if (!supported_text_bytes(bytes, transferred)) {
            return IOS_ERROR(IOS_E_UNEXPECTED_FORMAT);
        }
        if (transferred == 0 && !complete) return IOS_ERROR(IOS_E_PROTOCOL);
        if (offset > UINT64_MAX - transferred) return IOS_ERROR(IOS_E_OVERFLOW);
        offset += transferred;
    }
    return offset == resolved->byte_size
        ? IOS_OK : IOS_ERROR(IOS_E_PROTOCOL);
}

ios_status ios_file_command_write(
    struct ios_file_command_service *service,
    const char *display_path,
    const void *bytes,
    ios_size length
)
{
    struct ios_file_view_resolved_object resolved;
    ios_status status;
    if (display_path == NULL || (bytes == NULL && length != 0)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    status = resolve_regular(
        service, display_path, IOS_VFS_FILE_READ | IOS_VFS_FILE_WRITE, &resolved
    );
    if (IOS_FAILED(status)) return status;
    if (resolved.byte_size != 0 || !supported_text_bytes(bytes, length)) {
        return IOS_ERROR(IOS_E_UNEXPECTED_FORMAT);
    }
    return vfs_replace_object(resolved.mount, resolved.object_identity, bytes, length);
}

ios_status ios_file_command_append(
    struct ios_file_command_service *service,
    const char *display_path,
    const void *bytes,
    ios_size length
)
{
    struct ios_file_view_resolved_object resolved;
    ios_status status;
    if (display_path == NULL || (bytes == NULL && length != 0)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    status = resolve_regular(
        service, display_path, IOS_VFS_FILE_READ | IOS_VFS_FILE_WRITE, &resolved
    );
    if (IOS_FAILED(status)) return status;
    if (!supported_text_bytes(bytes, length)) {
        return IOS_ERROR(IOS_E_UNEXPECTED_FORMAT);
    }
    status = validate_existing_text(&resolved);
    return IOS_FAILED(status) ? status
        : vfs_append_object(resolved.mount, resolved.object_identity, bytes, length);
}

ios_status ios_file_command_cat(
    struct ios_file_command_service *service,
    const char *display_path,
    ios_file_command_output output,
    void *output_context
)
{
    struct ios_file_view_resolved_object resolved;
    ios_u64 offset = 0;
    bool complete;
    ios_status status;
    if (display_path == NULL || output == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    status = resolve_regular(service, display_path, IOS_VFS_FILE_READ, &resolved);
    if (IOS_FAILED(status)) return status;
    status = validate_existing_text(&resolved);
    if (IOS_FAILED(status)) return status;
    complete = resolved.byte_size == 0;
    while (!complete) {
        char bytes[IOS_FILE_COMMAND_TEXT_CHECK_CHUNK + 1U];
        ios_size transferred;
        status = vfs_read_object(
            resolved.mount, resolved.object_identity, offset,
            bytes, IOS_FILE_COMMAND_TEXT_CHECK_CHUNK, &transferred, &complete
        );
        if (IOS_FAILED(status)) return status;
        if (!supported_text_bytes(bytes, transferred)) {
            return IOS_ERROR(IOS_E_UNEXPECTED_FORMAT);
        }
        if (transferred == 0 && !complete) return IOS_ERROR(IOS_E_PROTOCOL);
        bytes[transferred] = '\0';
        if (transferred != 0) output(bytes, output_context);
        if (offset > UINT64_MAX - transferred) return IOS_ERROR(IOS_E_OVERFLOW);
        offset += transferred;
    }
    return offset == resolved.byte_size
        ? IOS_OK : IOS_ERROR(IOS_E_PROTOCOL);
}

static ios_status split_destination(
    const struct ios_vfs_path_context *path_context,
    const char *path,
    char parent[IOS_VFS_PATH_CAPACITY],
    char base[IOS_VFS_DISPLAY_NAME_CAPACITY],
    ios_size *base_length
)
{
    char normalized[IOS_VFS_PATH_CAPACITY];
    ios_size length;
    ios_size slash = 0;
    ios_status status;
    if (path_context == NULL || path == NULL || parent == NULL || base == NULL
        || base_length == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    status = vfs_path_normalize(
        path_context->current_directory, path, normalized, sizeof(normalized)
    );
    if (IOS_FAILED(status)) return status;
    length = strlen(normalized);
    if (length <= 1) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    for (ios_size index = 1; index < length; ++index) {
        if (normalized[index] == '/') slash = index;
    }
    *base_length = length - slash - 1U;
    if (*base_length == 0 || *base_length >= IOS_VFS_DISPLAY_NAME_CAPACITY) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (slash == 0) {
        parent[0] = '/';
        parent[1] = '\0';
    } else {
        memcpy(parent, normalized, slash);
        parent[slash] = '\0';
    }
    memcpy(base, normalized + slash + 1U, *base_length);
    base[*base_length] = '\0';
    return IOS_OK;
}

ios_status ios_file_command_rename(
    struct ios_file_command_service *service,
    const char *source_display_path,
    const char *destination_display_path
)
{
    struct ios_file_view_resolved_object source;
    struct ios_file_view_resolved_object parent;
    char parent_path[IOS_VFS_PATH_CAPACITY];
    char destination_base[IOS_VFS_DISPLAY_NAME_CAPACITY];
    ios_size destination_base_length;
    ios_status status;
    if (source_display_path == NULL || destination_display_path == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    status = resolve_regular(
        service, source_display_path, IOS_VFS_FILE_RENAME, &source
    );
    if (IOS_FAILED(status)) return status;
    status = split_destination(
        service->path_context, destination_display_path,
        parent_path, destination_base, &destination_base_length
    );
    if (IOS_FAILED(status)) return status;
    status = ios_file_view_resolve_display_path(
        service->file_view, service->path_context, parent_path, &parent
    );
    if (IOS_FAILED(status)) return status;
    if (parent.kind != IOS_VFS_OBJECT_DIRECTORY || parent.mount != source.mount) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    return vfs_rename_object(
        source.mount, source.object_identity, parent.object_identity,
        destination_base, destination_base_length
    );
}

ios_status ios_file_command_remove(
    struct ios_file_command_service *service,
    const char *display_path
)
{
    struct ios_file_view_resolved_object resolved;
    ios_status status;
    if (display_path == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    status = resolve_regular(
        service, display_path, IOS_VFS_FILE_DELETE, &resolved
    );
    return IOS_FAILED(status) ? status
        : vfs_remove_object(resolved.mount, resolved.object_identity);
}

ios_status ios_file_command_resolve_diagnostic(
    struct ios_file_command_service *service,
    const char *display_path,
    ios_u64 *object_identity
)
{
    struct ios_file_view_resolved_object resolved;
    ios_status status;
    if (object_identity == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    *object_identity = 0;
    status = resolve_regular(service, display_path, 0, &resolved);
    if (IOS_SUCCEEDED(status)) *object_identity = resolved.object_identity;
    return status;
}
