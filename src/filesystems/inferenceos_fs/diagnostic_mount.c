#include <inferenceos/fs/mount.h>

static enum ios_mount_state state_for_reason(enum ios_fs_mount_reason reason)
{
    switch (reason) {
    case IOS_FS_MOUNT_REASON_NONE:
        return IOS_MOUNT_RW;
    case IOS_FS_MOUNT_REASON_PRIMARY_INVALID:
    case IOS_FS_MOUNT_REASON_BACKUP_INVALID:
    case IOS_FS_MOUNT_REASON_SUPERBLOCK_DISAGREEMENT:
        return IOS_MOUNT_DIAGNOSTIC;
    default:
        return IOS_MOUNT_REJECTED;
    }
}

void ios_fs_mount_report_set(
    struct ios_fs_mount *mount, enum ios_fs_mount_reason reason,
    enum ios_fs_trusted_superblock trusted_superblock, bool bounds_trusted)
{
    enum ios_mount_state state;
    if (mount == NULL) return;
    state = state_for_reason(reason);
    mount->report = (struct ios_fs_mount_report){
        state, reason, trusted_superblock, bounds_trusted,
        state == IOS_MOUNT_DIAGNOSTIC
    };
    mount->vfs.state = state;
}

const struct ios_fs_mount_report *ios_fs_mount_report_get(const struct ios_fs_mount *mount)
{
    return mount == NULL ? NULL : &mount->report;
}

const char *ios_fs_mount_reason_name(enum ios_fs_mount_reason reason)
{
    switch (reason) {
    case IOS_FS_MOUNT_REASON_NONE: return "none";
    case IOS_FS_MOUNT_REASON_NOT_PROBED: return "not_probed";
    case IOS_FS_MOUNT_REASON_DEVICE_GEOMETRY: return "device_geometry";
    case IOS_FS_MOUNT_REASON_SUPERBLOCKS_INVALID: return "superblocks_invalid";
    case IOS_FS_MOUNT_REASON_PRIMARY_INVALID: return "primary_invalid";
    case IOS_FS_MOUNT_REASON_BACKUP_INVALID: return "backup_invalid";
    case IOS_FS_MOUNT_REASON_SUPERBLOCK_DISAGREEMENT: return "superblock_disagreement";
    case IOS_FS_MOUNT_REASON_ROOT_CHAIN_UNSAFE: return "root_chain_unsafe";
    case IOS_FS_MOUNT_REASON_DEVICE_IO: return "device_io";
    default: return "unknown";
    }
}

const char *ios_fs_trusted_superblock_name(enum ios_fs_trusted_superblock trusted)
{
    switch (trusted) {
    case IOS_FS_TRUSTED_SUPERBLOCK_NONE: return "none";
    case IOS_FS_TRUSTED_SUPERBLOCK_PRIMARY: return "primary";
    case IOS_FS_TRUSTED_SUPERBLOCK_BACKUP: return "backup";
    case IOS_FS_TRUSTED_SUPERBLOCK_BOTH: return "both";
    default: return "unknown";
    }
}

ios_status ios_fs_diagnostic_begin_read(struct ios_fs_mount *mount)
{
    if (mount == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    if (mount->vfs.state != IOS_MOUNT_DIAGNOSTIC || !mount->report.bounds_trusted
        || !mount->report.read_only) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    return vfs_mount_begin_operation(&mount->vfs, false);
}

ios_status ios_fs_diagnostic_end_read(struct ios_fs_mount *mount)
{
    if (mount == NULL || mount->vfs.state != IOS_MOUNT_DIAGNOSTIC) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    return vfs_mount_end_operation(&mount->vfs);
}
