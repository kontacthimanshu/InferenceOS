#include <inferenceos/vfs.h>

static ios_status path_length(const char *path, ios_size *length)
{
    ios_size index;
    if (path == NULL || length == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    for (index = 0; index <= IOS_VFS_PATH_MAX; ++index) {
        if (path[index] == '\0') {
            *length = index;
            return IOS_OK;
        }
    }
    return IOS_ERROR(IOS_E_OUT_OF_RANGE);
}

static bool invalid_path_byte(char character)
{
    const ios_u8 byte = (ios_u8)character;
    return byte < 0x20 || byte == 0x7f || character == '\\';
}

static ios_status append_component(
    const char *component,
    ios_size length,
    char *normalized,
    ios_size capacity,
    ios_size *output_length,
    ios_size starts[IOS_VFS_MAX_DIRECTORY_LEVELS - 1],
    ios_size *depth
)
{
    ios_size start;
    if (length == 1 && *component == '.') return IOS_OK;
    if (length == 2 && *component == '.' && *(component + 1) == '.') {
        if (*depth != 0) {
            *output_length = starts[--*depth];
            normalized[*output_length] = '\0';
        }
        return IOS_OK;
    }
    if (*depth >= IOS_VFS_MAX_DIRECTORY_LEVELS - 1) return IOS_ERROR(IOS_E_OUT_OF_RANGE);
    for (ios_size index = 0; index < length; ++index) {
        if (invalid_path_byte(component[index])) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    start = *output_length;
    if (start != 1) ++*output_length;
    if (*output_length > IOS_VFS_PATH_MAX || length > IOS_VFS_PATH_MAX - *output_length
        || *output_length + length >= capacity) {
        *output_length = start;
        return IOS_ERROR(IOS_E_OUT_OF_RANGE);
    }
    starts[(*depth)++] = start;
    if (start != 1) normalized[start] = '/';
    for (ios_size index = 0; index < length; ++index) {
        normalized[*output_length + index] = component[index];
    }
    *output_length += length;
    normalized[*output_length] = '\0';
    return IOS_OK;
}

static ios_status consume_path(
    const char *path,
    ios_size length,
    char *normalized,
    ios_size capacity,
    ios_size *output_length,
    ios_size starts[IOS_VFS_MAX_DIRECTORY_LEVELS - 1],
    ios_size *depth
)
{
    ios_size cursor = 0;
    while (cursor < length) {
        ios_size begin;
        while (cursor < length && path[cursor] == '/') ++cursor;
        begin = cursor;
        while (cursor < length && path[cursor] != '/') ++cursor;
        if (cursor != begin) {
            ios_status status = append_component(
                path + begin, cursor - begin, normalized, capacity,
                output_length, starts, depth
            );
            if (IOS_FAILED(status)) return status;
        }
    }
    return IOS_OK;
}

ios_status vfs_path_normalize(
    const char *current_directory,
    const char *path,
    char *normalized,
    ios_size normalized_capacity
)
{
    ios_size current_length;
    ios_size path_size;
    ios_size output_length = 1;
    ios_size depth = 0;
    ios_size starts[IOS_VFS_MAX_DIRECTORY_LEVELS - 1];
    ios_status status;

    if (normalized == NULL || normalized_capacity == 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *normalized = '\0';
    if (current_directory == NULL || path == NULL || *current_directory != '/') {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    status = path_length(current_directory, &current_length);
    if (IOS_FAILED(status)) return status;
    status = path_length(path, &path_size);
    if (IOS_FAILED(status)) return status;
    if (path_size == 0 || normalized_capacity < 2) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);

    *normalized = '/';
    normalized[1] = '\0';
    if (*path != '/') {
        status = consume_path(
            current_directory, current_length, normalized, normalized_capacity,
            &output_length, starts, &depth
        );
        if (IOS_FAILED(status)) goto failure;
    }
    status = consume_path(
        path, path_size, normalized, normalized_capacity, &output_length, starts, &depth
    );
    if (IOS_FAILED(status)) goto failure;
    return IOS_OK;

failure:
    *normalized = '\0';
    return status;
}

static ios_status validate_context(const struct ios_vfs_path_context *context)
{
    if (context == NULL || context->mount == NULL || context->mount->root == NULL
        || !context->mount->mounted
        || context->mount->lifecycle != IOS_VFS_MOUNT_ACTIVE
        || context->mount_generation != context->mount->generation
        || context->current_directory_identity == 0
        || *context->current_directory != '/') {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    return IOS_OK;
}

ios_status vfs_path_context_initialize(
    struct ios_vfs_path_context *context,
    struct ios_vfs_mount *mount
)
{
    if (context == NULL || mount == NULL || mount->root == NULL || !mount->mounted
        || mount->lifecycle != IOS_VFS_MOUNT_ACTIVE
        || mount->root->identity == 0
        || mount->root->kind != IOS_VFS_OBJECT_DIRECTORY) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    context->mount = mount;
    context->current_directory_identity = mount->root->identity;
    context->mount_generation = mount->generation;
    *context->current_directory = '/';
    context->current_directory[1] = '\0';
    return IOS_OK;
}

static ios_status resolve_normalized(
    const struct ios_vfs_path_context *context,
    const char normalized[IOS_VFS_PATH_CAPACITY],
    struct ios_vfs_object *object
)
{
    struct ios_vfs_object current = *context->mount->root;
    ios_size cursor = 1;
    if (normalized[1] == '\0') {
        *object = current;
        return IOS_OK;
    }
    if (context->mount->lookup == NULL) return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    while (normalized[cursor] != '\0') {
        ios_size begin = cursor;
        ios_status status;
        while (normalized[cursor] != '\0' && normalized[cursor] != '/') ++cursor;
        if (current.kind != IOS_VFS_OBJECT_DIRECTORY) return IOS_ERROR(IOS_E_NOT_FOUND);
        status = context->mount->lookup(
            context->mount->driver_context, current.identity,
            normalized + begin, cursor - begin, &current
        );
        if (IOS_FAILED(status)) return status;
        if (current.identity == 0
            || (current.kind != IOS_VFS_OBJECT_DIRECTORY
                && current.kind != IOS_VFS_OBJECT_REGULAR_FILE)) {
            return IOS_ERROR(IOS_E_PROTOCOL);
        }
        if (normalized[cursor] == '/') ++cursor;
    }
    *object = current;
    return IOS_OK;
}

ios_status vfs_path_resolve(
    const struct ios_vfs_path_context *context,
    const char *path,
    struct ios_vfs_object *object
)
{
    char normalized[IOS_VFS_PATH_CAPACITY];
    ios_status status;
    ios_status end_status;
    if (object == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    *object = (struct ios_vfs_object){ 0, IOS_VFS_OBJECT_DIRECTORY, 0 };
    status = validate_context(context);
    if (IOS_FAILED(status)) return status;
    status = vfs_path_normalize(
        context->current_directory, path, normalized, sizeof(normalized)
    );
    if (IOS_FAILED(status)) return status;
    status = vfs_mount_begin_operation(context->mount, false);
    if (IOS_FAILED(status)) return status;
    status = resolve_normalized(context, normalized, object);
    end_status = vfs_mount_end_operation(context->mount);
    if (IOS_FAILED(status)) {
        *object = (struct ios_vfs_object){ 0, IOS_VFS_OBJECT_DIRECTORY, 0 };
        return status;
    }
    if (IOS_FAILED(end_status)) {
        *object = (struct ios_vfs_object){ 0, IOS_VFS_OBJECT_DIRECTORY, 0 };
        return end_status;
    }
    return IOS_OK;
}

ios_status vfs_path_set_current(
    struct ios_vfs_path_context *context,
    const char *path
)
{
    char normalized[IOS_VFS_PATH_CAPACITY];
    struct ios_vfs_object object;
    ios_status status = validate_context(context);
    if (IOS_FAILED(status)) return status;
    status = vfs_path_normalize(
        context->current_directory, path, normalized, sizeof(normalized)
    );
    if (IOS_FAILED(status)) return status;
    status = vfs_path_resolve(context, normalized, &object);
    if (IOS_FAILED(status)) return status;
    if (object.kind != IOS_VFS_OBJECT_DIRECTORY) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    for (ios_size index = 0; index < sizeof(context->current_directory); ++index) {
        context->current_directory[index] = normalized[index];
        if (normalized[index] == '\0') break;
    }
    context->current_directory_identity = object.identity;
    return IOS_OK;
}

ios_status vfs_path_get_current(
    const struct ios_vfs_path_context *context,
    char *path,
    ios_size capacity
)
{
    ios_size length;
    ios_status status;
    if (path == NULL || capacity == 0) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    *path = '\0';
    status = validate_context(context);
    if (IOS_FAILED(status)) return status;
    status = path_length(context->current_directory, &length);
    if (IOS_FAILED(status)) return status;
    if (capacity <= length) return IOS_ERROR(IOS_E_OUT_OF_RANGE);
    for (ios_size index = 0; index <= length; ++index) {
        path[index] = context->current_directory[index];
    }
    return IOS_OK;
}
