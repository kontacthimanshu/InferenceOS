#ifndef INFERENCEOS_FS_MOUNT_H
#define INFERENCEOS_FS_MOUNT_H

#include <inferenceos/fs/diagnostic_mount.h>
#include <inferenceos/fs/format.h>
#include <inferenceos/vfs.h>

struct ios_fs_mount {
    struct ios_vfs_mount vfs;
    struct ios_vfs_object root;
    struct ios_fs_superblock superblock;
    struct ios_fs_geometry geometry;
    struct ios_fs_mount_report report;
};

ios_status ios_fs_mount_probe(struct ios_fs_mount *mount, struct ios_block_device *device);
ios_status ios_fs_mount_root(
    struct ios_fs_mount *mount,
    struct ios_block_device *device,
    struct ios_vfs_mount_registry *registry
);

#endif
