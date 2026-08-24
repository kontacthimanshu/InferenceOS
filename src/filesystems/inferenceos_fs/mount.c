#include <inferenceos/fs/mount.h>

#include <inferenceos/block.h>
#include <inferenceos/fs/validator.h>
#include <inferenceos/runtime.h>

static bool geometry_matches(
    const struct ios_fs_superblock *superblock, struct ios_block_device *device,
    struct ios_fs_geometry *geometry)
{
    return superblock->total_sectors == device->sector_count
        && IOS_SUCCEEDED(ios_fs_calculate_geometry(device->logical_sector_size,
                                                   device->sector_count, geometry))
        && geometry->fat_sectors == superblock->sectors_per_fat
        && geometry->registry_start_sector == superblock->registry_start_sector;
}

ios_status ios_fs_mount_probe(struct ios_fs_mount *mount, struct ios_block_device *device)
{
    struct ios_fs_superblock_disk primary_disk;
    struct ios_fs_superblock_disk backup_disk;
    struct ios_fs_superblock primary;
    struct ios_fs_superblock backup;
    ios_status primary_status;
    ios_status backup_status;
    ios_status validation_status;
    ios_size root_cluster_count;
    bool primary_bounds;
    bool backup_bounds;
    if (mount == NULL || device == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    memset(mount, 0, sizeof(*mount));
    mount->vfs.driver_name = "InferenceOS-FS";
    mount->vfs.driver_context = mount;
    mount->vfs.device = device;
    mount->vfs.root = &mount->root;
    mount->root.identity = IOS_VFS_ROOT_OBJECT_ID;
    mount->root.kind = IOS_VFS_OBJECT_DIRECTORY;
    ios_fs_mount_report_set(
        mount, IOS_FS_MOUNT_REASON_NOT_PROBED, IOS_FS_TRUSTED_SUPERBLOCK_NONE, false);
    if (device->logical_sector_size != IOS_FS_SECTOR_SIZE
        || device->sector_count < IOS_FS_MINIMUM_VOLUME_BYTES / IOS_FS_SECTOR_SIZE) {
        ios_fs_mount_report_set(
            mount, IOS_FS_MOUNT_REASON_DEVICE_GEOMETRY,
            IOS_FS_TRUSTED_SUPERBLOCK_NONE, false);
        return IOS_OK;
    }
    if (IOS_FAILED(block_device_read(device, 0, 1, &primary_disk))
        || IOS_FAILED(block_device_read(device, 1, 1, &backup_disk))) {
        ios_fs_mount_report_set(
            mount, IOS_FS_MOUNT_REASON_DEVICE_IO, IOS_FS_TRUSTED_SUPERBLOCK_NONE, false);
        return IOS_ERROR(IOS_E_IO);
    }
    primary_status = ios_fs_superblock_decode(&primary_disk, &primary);
    backup_status = ios_fs_superblock_decode(&backup_disk, &backup);
    primary_bounds = IOS_SUCCEEDED(primary_status)
        && geometry_matches(&primary, device, &mount->geometry);
    backup_bounds = IOS_SUCCEEDED(backup_status)
        && geometry_matches(&backup, device, &mount->geometry);
    if (!primary_bounds && !backup_bounds) {
        ios_fs_mount_report_set(
            mount,
            IOS_SUCCEEDED(primary_status) || IOS_SUCCEEDED(backup_status)
                ? IOS_FS_MOUNT_REASON_DEVICE_GEOMETRY
                : IOS_FS_MOUNT_REASON_SUPERBLOCKS_INVALID,
            IOS_FS_TRUSTED_SUPERBLOCK_NONE, false);
        return IOS_OK;
    }
    validation_status = ios_fs_validate_root_chain(
        device, &mount->geometry, &root_cluster_count);
    if (IOS_FAILED(validation_status)) {
        ios_fs_mount_report_set(
            mount,
            validation_status == IOS_ERROR(IOS_E_CORRUPT)
                ? IOS_FS_MOUNT_REASON_ROOT_CHAIN_UNSAFE : IOS_FS_MOUNT_REASON_DEVICE_IO,
            primary_bounds ? IOS_FS_TRUSTED_SUPERBLOCK_PRIMARY
                           : IOS_FS_TRUSTED_SUPERBLOCK_BACKUP,
            true);
        return validation_status == IOS_ERROR(IOS_E_CORRUPT) ? IOS_OK : validation_status;
    }
    if (primary_bounds && backup_bounds
        && ios_fs_superblock_classify_pair(&primary_disk, &backup_disk)
            == IOS_FS_SUPERBLOCK_PAIR_READ_WRITE) {
        mount->superblock = primary;
        ios_fs_mount_report_set(
            mount, IOS_FS_MOUNT_REASON_NONE, IOS_FS_TRUSTED_SUPERBLOCK_BOTH, true);
    } else {
        mount->superblock = primary_bounds ? primary : backup;
        ios_fs_mount_report_set(
            mount,
            primary_bounds && backup_bounds
                ? IOS_FS_MOUNT_REASON_SUPERBLOCK_DISAGREEMENT
                : (primary_bounds ? IOS_FS_MOUNT_REASON_BACKUP_INVALID
                                  : IOS_FS_MOUNT_REASON_PRIMARY_INVALID),
            primary_bounds && backup_bounds
                ? IOS_FS_TRUSTED_SUPERBLOCK_BOTH
                : (primary_bounds ? IOS_FS_TRUSTED_SUPERBLOCK_PRIMARY
                                  : IOS_FS_TRUSTED_SUPERBLOCK_BACKUP),
            true);
    }
    return IOS_OK;
}

ios_status ios_fs_mount_root(
    struct ios_fs_mount *mount, struct ios_block_device *device,
    struct ios_vfs_mount_registry *registry)
{
    ios_status status = ios_fs_mount_probe(mount, device);
    if (IOS_FAILED(status)) return status;
    if (mount->vfs.state == IOS_MOUNT_REJECTED) return IOS_ERROR(IOS_E_CORRUPT);
    return vfs_mount_root(registry, &mount->vfs, "/");
}
