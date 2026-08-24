#include <inferenceos/vfs.h>

ios_status vfs_mount_configure_unmount(
    struct ios_vfs_mount *mount,
    void *context,
    ios_vfs_unmount_sync_function sync,
    ios_vfs_unmount_invalidate_function invalidate)
{
    if (mount == NULL || context == NULL || sync == NULL || invalidate == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (mount->lifecycle == IOS_VFS_MOUNT_DRAINING) return IOS_ERROR(IOS_E_BUSY);
    mount->unmount_context = context;
    mount->unmount_sync = sync;
    mount->unmount_invalidate = invalidate;
    return IOS_OK;
}

ios_status vfs_unmount_root(struct ios_vfs_mount_registry *registry)
{
    struct ios_vfs_mount *mount;
    ios_status status;

    if (registry == NULL || registry->root == NULL) return IOS_ERROR(IOS_E_NOT_FOUND);
    mount = registry->root;
    if (!mount->mounted || mount->root == NULL
        || (mount->lifecycle != IOS_VFS_MOUNT_ACTIVE
            && mount->lifecycle != IOS_VFS_MOUNT_DRAINING)) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }

    mount->lifecycle = IOS_VFS_MOUNT_DRAINING;
    if (mount->active_operations != 0 || mount->root->reference_count != 0) {
        return IOS_ERROR(IOS_E_BUSY);
    }

    if (mount->unmount_sync != NULL) {
        status = mount->unmount_sync(mount->unmount_context);
        if (IOS_FAILED(status)) {
            mount->lifecycle = IOS_VFS_MOUNT_ACTIVE;
            return status;
        }
    }
    if (mount->unmount_invalidate != NULL) {
        mount->unmount_invalidate(mount->unmount_context);
    }

    mount->mounted = false;
    mount->lifecycle = IOS_VFS_MOUNT_DETACHED;
    registry->root = NULL;
    registry->mounts[0] = NULL;
    registry->count = 0;
    return IOS_OK;
}
