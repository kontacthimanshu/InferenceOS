#include <inferenceos/vfs.h>

static ios_status finish_mutation(struct ios_vfs_mount *mount, ios_status status)
{
    const ios_status end_status = vfs_mount_end_operation(mount);
    return IOS_FAILED(status) ? status : end_status;
}

ios_status vfs_read_object(
    struct ios_vfs_mount *mount,
    ios_u64 object_identity,
    ios_u64 offset,
    void *buffer,
    ios_size capacity,
    ios_size *transferred,
    bool *complete
)
{
    ios_status status;
    ios_status end_status;
    if (mount == NULL || object_identity == 0 || buffer == NULL || capacity == 0
        || transferred == NULL || complete == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *transferred = 0;
    *complete = false;
    if (mount->read_object == NULL) return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    status = vfs_mount_begin_operation(mount, false);
    if (IOS_FAILED(status)) return status;
    status = mount->read_object(
        mount->driver_context, object_identity, offset, buffer, capacity,
        transferred, complete
    );
    end_status = vfs_mount_end_operation(mount);
    if (IOS_FAILED(status)) {
        *transferred = 0;
        *complete = false;
        return status;
    }
    if (IOS_FAILED(end_status)) {
        *transferred = 0;
        *complete = false;
        return end_status;
    }
    if (*transferred > capacity || (!*complete && *transferred == 0)) {
        *transferred = 0;
        *complete = false;
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    return IOS_OK;
}

ios_status vfs_replace_object(
    struct ios_vfs_mount *mount,
    ios_u64 object_identity,
    const void *bytes,
    ios_size length
)
{
    ios_status status;
    if (mount == NULL || object_identity == 0 || (bytes == NULL && length != 0)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (mount->replace_object == NULL) return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    status = vfs_mount_begin_operation(mount, true);
    if (IOS_FAILED(status)) return status;
    status = mount->replace_object(
        mount->driver_context, object_identity, bytes, length
    );
    return finish_mutation(mount, status);
}

ios_status vfs_append_object(
    struct ios_vfs_mount *mount,
    ios_u64 object_identity,
    const void *bytes,
    ios_size length
)
{
    ios_status status;
    if (mount == NULL || object_identity == 0 || (bytes == NULL && length != 0)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (mount->append_object == NULL) return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    status = vfs_mount_begin_operation(mount, true);
    if (IOS_FAILED(status)) return status;
    status = mount->append_object(
        mount->driver_context, object_identity, bytes, length
    );
    return finish_mutation(mount, status);
}

ios_status vfs_remove_object(
    struct ios_vfs_mount *mount,
    ios_u64 object_identity
)
{
    ios_status status;
    if (mount == NULL || object_identity == 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (mount->remove_object == NULL) return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    status = vfs_mount_begin_operation(mount, true);
    if (IOS_FAILED(status)) return status;
    status = mount->remove_object(mount->driver_context, object_identity);
    return finish_mutation(mount, status);
}

ios_status vfs_rename_object(
    struct ios_vfs_mount *mount,
    ios_u64 object_identity,
    ios_u64 destination_parent_identity,
    const char *destination_base,
    ios_size destination_base_length
)
{
    ios_status status;
    if (mount == NULL || object_identity == 0 || destination_parent_identity == 0
        || destination_base == NULL || destination_base_length == 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (mount->rename_object == NULL) return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    status = vfs_mount_begin_operation(mount, true);
    if (IOS_FAILED(status)) return status;
    status = mount->rename_object(
        mount->driver_context, object_identity, destination_parent_identity,
        destination_base, destination_base_length
    );
    return finish_mutation(mount, status);
}
