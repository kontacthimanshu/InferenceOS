#include <inferenceos/vfs.h>

#include <inferenceos/runtime.h>

struct path_leaf {
    char normalized[IOS_VFS_PATH_CAPACITY];
    char parent[IOS_VFS_PATH_CAPACITY];
    const char *component;
    ios_size component_length;
};

static ios_status split_path(
    const struct ios_vfs_path_context *context,
    const char *path,
    struct path_leaf *leaf
)
{
    ios_size slash = 0;
    ios_size length = 0;
    ios_status status;
    if (context == NULL || leaf == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    status = vfs_path_normalize(
        context->current_directory, path, leaf->normalized, sizeof(leaf->normalized)
    );
    if (IOS_FAILED(status)) return status;
    while (leaf->normalized[length] != '\0') {
        if (leaf->normalized[length] == '/') slash = length;
        ++length;
    }
    if (length == 1) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    leaf->component = leaf->normalized + slash + 1;
    leaf->component_length = length - slash - 1;
    if (slash == 0) {
        *leaf->parent = '/';
        leaf->parent[1] = '\0';
    } else {
        for (ios_size index = 0; index < slash; ++index) {
            leaf->parent[index] = leaf->normalized[index];
        }
        leaf->parent[slash] = '\0';
    }
    return IOS_OK;
}

static ios_status resolve_parent(
    const struct ios_vfs_path_context *context,
    const struct path_leaf *leaf,
    struct ios_vfs_object *parent
)
{
    ios_status status = vfs_path_resolve(context, leaf->parent, parent);
    if (IOS_FAILED(status)) return status;
    return parent->kind == IOS_VFS_OBJECT_DIRECTORY
        ? IOS_OK : IOS_ERROR(IOS_E_NOT_FOUND);
}

static ios_status finish_operation(struct ios_vfs_mount *mount, ios_status status)
{
    const ios_status end_status = vfs_mount_end_operation(mount);
    if (IOS_FAILED(status)) return status;
    return end_status;
}

static bool valid_search_path(const struct ios_vfs_search_result *entry)
{
    ios_size length;
    if (entry == NULL || entry->object_identity == 0
        || entry->display_path_length == 0
        || entry->display_path_length > IOS_VFS_PATH_MAX
        || *entry->display_path != '/') {
        return false;
    }
    for (length = 0; length <= IOS_VFS_PATH_MAX; ++length) {
        const ios_u8 byte = (ios_u8)entry->display_path[length];
        if (byte == 0) break;
        if (byte < 0x20 || byte == 0x7f || byte == '\\' || byte == '.') return false;
        if (byte == '/' && length != 0 && entry->display_path[length - 1] == '/') {
            return false;
        }
    }
    return length == entry->display_path_length && length <= IOS_VFS_PATH_MAX;
}

ios_status vfs_create_directory(
    const struct ios_vfs_path_context *context,
    const char *path,
    struct ios_vfs_object *object
)
{
    struct path_leaf leaf;
    struct ios_vfs_object parent;
    ios_status status;
    if (object == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    *object = (struct ios_vfs_object){ 0, IOS_VFS_OBJECT_DIRECTORY, 0 };
    status = split_path(context, path, &leaf);
    if (IOS_FAILED(status)) return status;
    status = resolve_parent(context, &leaf, &parent);
    if (IOS_FAILED(status)) return status;
    if (context->mount->create_directory == NULL) return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    status = vfs_mount_begin_operation(context->mount, true);
    if (IOS_FAILED(status)) return status;
    status = context->mount->create_directory(
        context->mount->driver_context, parent.identity,
        leaf.component, leaf.component_length, object
    );
    status = finish_operation(context->mount, status);
    if (IOS_FAILED(status)) {
        *object = (struct ios_vfs_object){ 0, IOS_VFS_OBJECT_DIRECTORY, 0 };
        return status;
    }
    if (object->identity == 0 || object->kind != IOS_VFS_OBJECT_DIRECTORY) {
        *object = (struct ios_vfs_object){ 0, IOS_VFS_OBJECT_DIRECTORY, 0 };
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    return IOS_OK;
}

ios_status vfs_create_file(
    const struct ios_vfs_path_context *context,
    const char *path,
    struct ios_vfs_object *object
)
{
    struct path_leaf leaf;
    struct ios_vfs_object parent;
    ios_status status;
    if (object == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    *object = (struct ios_vfs_object){ 0, IOS_VFS_OBJECT_REGULAR_FILE, 0 };
    status = split_path(context, path, &leaf);
    if (IOS_FAILED(status)) return status;
    status = resolve_parent(context, &leaf, &parent);
    if (IOS_FAILED(status)) return status;
    if (context->mount->create_file == NULL) return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    status = vfs_mount_begin_operation(context->mount, true);
    if (IOS_FAILED(status)) return status;
    status = context->mount->create_file(
        context->mount->driver_context, parent.identity,
        leaf.component, leaf.component_length, object
    );
    status = finish_operation(context->mount, status);
    if (IOS_FAILED(status)) {
        *object = (struct ios_vfs_object){ 0, IOS_VFS_OBJECT_REGULAR_FILE, 0 };
        return status;
    }
    if (object->identity == 0 || object->kind != IOS_VFS_OBJECT_REGULAR_FILE) {
        *object = (struct ios_vfs_object){ 0, IOS_VFS_OBJECT_REGULAR_FILE, 0 };
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    return IOS_OK;
}

ios_status vfs_remove_directory(
    const struct ios_vfs_path_context *context,
    const char *path
)
{
    struct path_leaf leaf;
    struct ios_vfs_object parent;
    struct ios_vfs_object target;
    ios_status status = split_path(context, path, &leaf);
    if (IOS_FAILED(status)) return status;
    status = vfs_path_resolve(context, leaf.normalized, &target);
    if (IOS_FAILED(status)) return status;
    if (target.kind != IOS_VFS_OBJECT_DIRECTORY) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    status = resolve_parent(context, &leaf, &parent);
    if (IOS_FAILED(status)) return status;
    if (context->mount->remove_directory == NULL) return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    status = vfs_mount_begin_operation(context->mount, true);
    if (IOS_FAILED(status)) return status;
    status = context->mount->remove_directory(
        context->mount->driver_context, parent.identity,
        leaf.component, leaf.component_length
    );
    return finish_operation(context->mount, status);
}

ios_status vfs_list_directory(
    const struct ios_vfs_path_context *context,
    const char *path,
    ios_u64 continuation,
    struct ios_vfs_directory_entry *entries,
    ios_size capacity,
    ios_size *entry_count,
    ios_u64 *next_continuation
)
{
    struct ios_vfs_object directory;
    ios_status status = vfs_path_resolve(context, path, &directory);
    if (IOS_FAILED(status)) return status;
    if (directory.kind != IOS_VFS_OBJECT_DIRECTORY) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    return vfs_enumerate(
        context->mount, directory.identity, continuation, entries,
        capacity, entry_count, next_continuation
    );
}

ios_status vfs_search_extension(
    struct ios_vfs_mount *mount,
    const char *extension,
    ios_size extension_length,
    struct ios_vfs_search_result *entries,
    ios_size capacity,
    ios_size *entry_count,
    bool *truncated
)
{
    ios_status status;
    if (mount == NULL || extension == NULL || extension_length == 0
        || entries == NULL || capacity == 0
        || capacity > IOS_VFS_SEARCH_RESULT_CAPACITY || entry_count == NULL
        || truncated == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *entry_count = 0;
    *truncated = false;
    memset(entries, 0, capacity * sizeof(*entries));
    if (mount->search_extension == NULL) return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    status = vfs_mount_begin_operation(mount, false);
    if (IOS_FAILED(status)) return status;
    status = mount->search_extension(
        mount->driver_context, extension, extension_length,
        entries, capacity, entry_count, truncated
    );
    status = finish_operation(mount, status);
    if (IOS_FAILED(status)) {
        memset(entries, 0, capacity * sizeof(*entries));
        *entry_count = 0;
        *truncated = false;
        return status;
    }
    if (*entry_count > capacity || (*truncated && *entry_count != capacity)) {
        memset(entries, 0, capacity * sizeof(*entries));
        *entry_count = 0;
        *truncated = false;
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    for (ios_size index = 0; index < *entry_count; ++index) {
        if (!valid_search_path(&entries[index])) {
            memset(entries, 0, capacity * sizeof(*entries));
            *entry_count = 0;
            *truncated = false;
            return IOS_ERROR(IOS_E_PROTOCOL);
        }
    }
    return IOS_OK;
}

static bool path_is_descendant(const char *ancestor, const char *candidate)
{
    ios_size index = 0;
    while (ancestor[index] != '\0' && ancestor[index] == candidate[index]) ++index;
    return ancestor[index] == '\0'
        && (candidate[index] == '\0' || candidate[index] == '/');
}

static bool paths_equal(const char *left, const char *right)
{
    ios_size index = 0;
    while (left[index] != '\0' && left[index] == right[index]) ++index;
    return left[index] == '\0' && right[index] == '\0';
}

ios_status vfs_rename(
    const struct ios_vfs_path_context *context,
    const char *source,
    const char *destination
)
{
    struct path_leaf source_leaf;
    struct path_leaf destination_leaf;
    struct ios_vfs_object source_parent;
    struct ios_vfs_object destination_parent;
    struct ios_vfs_object source_object;
    ios_status status = split_path(context, source, &source_leaf);
    if (IOS_FAILED(status)) return status;
    status = split_path(context, destination, &destination_leaf);
    if (IOS_FAILED(status)) return status;
    status = vfs_path_resolve(context, source_leaf.normalized, &source_object);
    if (IOS_FAILED(status)) return status;
    if (paths_equal(source_leaf.normalized, destination_leaf.normalized)) return IOS_OK;
    if (source_object.kind == IOS_VFS_OBJECT_DIRECTORY
        && path_is_descendant(source_leaf.normalized, destination_leaf.parent)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    status = resolve_parent(context, &source_leaf, &source_parent);
    if (IOS_FAILED(status)) return status;
    status = resolve_parent(context, &destination_leaf, &destination_parent);
    if (IOS_FAILED(status)) return status;
    if (context->mount->rename == NULL) return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    status = vfs_mount_begin_operation(context->mount, true);
    if (IOS_FAILED(status)) return status;
    status = context->mount->rename(
        context->mount->driver_context,
        source_parent.identity, source_leaf.component, source_leaf.component_length,
        destination_parent.identity, destination_leaf.component,
        destination_leaf.component_length
    );
    return finish_operation(context->mount, status);
}
