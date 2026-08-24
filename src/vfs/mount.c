#include <inferenceos/vfs.h>

#include <inferenceos/runtime.h>

void vfs_mount_registry_initialize(struct ios_vfs_mount_registry *registry)
{
    if (registry == NULL) return;
    memset(registry, 0, sizeof(*registry));
    registry->next_generation = 1;
}

ios_status vfs_mount_root(
    struct ios_vfs_mount_registry *registry, struct ios_vfs_mount *mount, const char *path)
{
    if (registry == NULL || mount == NULL || path == NULL || strcmp(path, "/") != 0
        || mount->driver_name == NULL || mount->driver_context == NULL || mount->device == NULL
        || mount->root == NULL || mount->state == IOS_MOUNT_REJECTED) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (registry->root != NULL || mount->mounted) return IOS_ERROR(IOS_E_ALREADY_EXISTS);
    if (registry->count >= IOS_VFS_MAX_MOUNTS) return IOS_ERROR(IOS_E_NO_SPACE);
    if (registry->next_generation == UINT32_MAX) return IOS_ERROR(IOS_E_OVERFLOW);
    mount->generation = registry->next_generation++;
    mount->active_operations = 0;
    mount->lifecycle = IOS_VFS_MOUNT_ACTIVE;
    mount->mounted = true;
    registry->mounts[registry->count++] = mount;
    registry->root = mount;
    return IOS_OK;
}

struct ios_vfs_mount *vfs_root_mount(const struct ios_vfs_mount_registry *registry)
{
    return registry == NULL ? NULL : registry->root;
}

ios_status vfs_mount_begin_operation(struct ios_vfs_mount *mount, bool mutation)
{
    if (mount == NULL || !mount->mounted) return IOS_ERROR(IOS_E_INVALID_STATE);
    if (mount->lifecycle == IOS_VFS_MOUNT_DRAINING) return IOS_ERROR(IOS_E_BUSY);
    if (mount->lifecycle != IOS_VFS_MOUNT_ACTIVE) return IOS_ERROR(IOS_E_INVALID_STATE);
    if (mutation && mount->state != IOS_MOUNT_RW) return IOS_ERROR(IOS_E_READ_ONLY);
    if (mount->active_operations == UINT32_MAX) return IOS_ERROR(IOS_E_OVERFLOW);
    ++mount->active_operations;
    ++mount->root->reference_count;
    return IOS_OK;
}

ios_status vfs_mount_end_operation(struct ios_vfs_mount *mount)
{
    if (mount == NULL || !mount->mounted
        || (mount->lifecycle != IOS_VFS_MOUNT_ACTIVE
            && mount->lifecycle != IOS_VFS_MOUNT_DRAINING)
        || mount->active_operations == 0
        || mount->root->reference_count == 0) return IOS_ERROR(IOS_E_INVALID_STATE);
    --mount->active_operations;
    --mount->root->reference_count;
    return IOS_OK;
}

ios_status vfs_enumerate(
    struct ios_vfs_mount *mount,
    ios_u64 directory_identity,
    ios_u64 continuation,
    struct ios_vfs_directory_entry *entries,
    ios_size capacity,
    ios_size *entry_count,
    ios_u64 *next_continuation
)
{
    ios_status status;
    if (mount == NULL || directory_identity == 0 || entries == NULL || capacity == 0
        || entry_count == NULL || next_continuation == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *entry_count = 0;
    *next_continuation = 0;
    if (mount->enumerate == NULL) return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    status = vfs_mount_begin_operation(mount, false);
    if (IOS_FAILED(status)) return status;
    status = mount->enumerate(
        mount->driver_context, directory_identity, continuation, entries, capacity,
        entry_count, next_continuation
    );
    ios_status end_status = vfs_mount_end_operation(mount);
    if (IOS_FAILED(status)) return status;
    if (IOS_FAILED(end_status)) return end_status;
    if (*entry_count > capacity || (*next_continuation != 0 && *entry_count == 0)) {
        *entry_count = 0;
        *next_continuation = 0;
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    return IOS_OK;
}
