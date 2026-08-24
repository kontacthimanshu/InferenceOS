#ifndef INFERENCEOS_FS_DIAGNOSTIC_MOUNT_H
#define INFERENCEOS_FS_DIAGNOSTIC_MOUNT_H

#include <inferenceos/vfs.h>

struct ios_fs_mount;

enum ios_fs_mount_reason {
    IOS_FS_MOUNT_REASON_NONE,
    IOS_FS_MOUNT_REASON_NOT_PROBED,
    IOS_FS_MOUNT_REASON_DEVICE_GEOMETRY,
    IOS_FS_MOUNT_REASON_SUPERBLOCKS_INVALID,
    IOS_FS_MOUNT_REASON_PRIMARY_INVALID,
    IOS_FS_MOUNT_REASON_BACKUP_INVALID,
    IOS_FS_MOUNT_REASON_SUPERBLOCK_DISAGREEMENT,
    IOS_FS_MOUNT_REASON_ROOT_CHAIN_UNSAFE,
    IOS_FS_MOUNT_REASON_DEVICE_IO
};

enum ios_fs_trusted_superblock {
    IOS_FS_TRUSTED_SUPERBLOCK_NONE,
    IOS_FS_TRUSTED_SUPERBLOCK_PRIMARY,
    IOS_FS_TRUSTED_SUPERBLOCK_BACKUP,
    IOS_FS_TRUSTED_SUPERBLOCK_BOTH
};

struct ios_fs_mount_report {
    enum ios_mount_state state;
    enum ios_fs_mount_reason reason;
    enum ios_fs_trusted_superblock trusted_superblock;
    bool bounds_trusted;
    bool read_only;
};

void ios_fs_mount_report_set(
    struct ios_fs_mount *mount,
    enum ios_fs_mount_reason reason,
    enum ios_fs_trusted_superblock trusted_superblock,
    bool bounds_trusted
);
const struct ios_fs_mount_report *ios_fs_mount_report_get(const struct ios_fs_mount *mount);
const char *ios_fs_mount_reason_name(enum ios_fs_mount_reason reason);
const char *ios_fs_trusted_superblock_name(enum ios_fs_trusted_superblock trusted);
ios_status ios_fs_diagnostic_begin_read(struct ios_fs_mount *mount);
ios_status ios_fs_diagnostic_end_read(struct ios_fs_mount *mount);

#endif
