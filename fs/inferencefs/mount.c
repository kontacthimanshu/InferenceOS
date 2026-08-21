#include <inferencefs/mount.h>
#include <inferenceos/memory.h>

static inferencefs_mount_validation rejected(
    inferenceos_result result,
    inferencefs_mount_error error
)
{
    inferencefs_mount_validation validation;
    (void)memset(&validation, 0, sizeof(validation));
    validation.result = result;
    validation.state = INFERENCEOS_VFS_MOUNT_REJECTED;
    validation.error = error;
    return validation;
}

static inferenceos_result read_superblock(
    const inferenceos_block_device *device,
    inferenceos_u64 lba,
    inferenceos_u8 sector[INFERENCEFS_SUPERBLOCK_SIZE]
)
{
    const inferenceos_block_outcome read = inferenceos_block_read(
        device, lba, 1U, sector);
    if (!inferenceos_block_outcome_is_success(read)
        || read.sectors_completed != 1U) {
        return inferenceos_result_is_success(read.result)
            ? INFERENCEOS_RESULT_IO_ERROR : read.result;
    }
    return INFERENCEOS_RESULT_OK;
}

inferencefs_mount_validation inferencefs_mount_validate(
    const inferenceos_block_device *device,
    inferencefs_validator_workspace *workspace
)
{
    inferencefs_mount_validation validation;
    inferenceos_block_info info;
    inferenceos_block_outcome query;
    inferenceos_u8 primary_sector[INFERENCEFS_SUPERBLOCK_SIZE];
    inferenceos_u8 backup_sector[INFERENCEFS_SUPERBLOCK_SIZE];
    inferencefs_superblock primary;
    inferencefs_superblock backup;
    inferenceos_result result;
    bool use_backup = false;

    if (device == NULL || workspace == NULL) {
        return rejected(INFERENCEOS_RESULT_INVALID_ARGUMENT,
            INFERENCEFS_MOUNT_ERROR_ARGUMENT);
    }
    query = inferenceos_block_query(device, &info);
    if (!inferenceos_block_outcome_is_success(query)
        || (info.status != INFERENCEOS_BLOCK_STATUS_READY
            && info.status != INFERENCEOS_BLOCK_STATUS_READ_ONLY)
        || info.geometry.logical_sector_size != INFERENCEFS_LOGICAL_SECTOR_SIZE) {
        return rejected(inferenceos_block_outcome_is_success(query)
                ? INFERENCEOS_RESULT_NOT_READY : query.result,
            INFERENCEFS_MOUNT_ERROR_DEVICE);
    }
    result = read_superblock(device, 0U, primary_sector);
    if (!inferenceos_result_is_success(result)) {
        return rejected(result, INFERENCEFS_MOUNT_ERROR_DEVICE);
    }
    result = read_superblock(device, 1U, backup_sector);
    if (!inferenceos_result_is_success(result)) {
        return rejected(result, INFERENCEFS_MOUNT_ERROR_DEVICE);
    }
    (void)memset(&validation, 0, sizeof(validation));
    validation.primary = inferencefs_superblock_decode(
        primary_sector, sizeof(primary_sector),
        info.geometry.sector_count, &primary);
    validation.backup = inferencefs_superblock_decode(
        backup_sector, sizeof(backup_sector),
        info.geometry.sector_count, &backup);
    if (!inferenceos_result_is_success(validation.primary.result)) {
        if (!inferenceos_result_is_success(validation.backup.result)) {
            validation.result = validation.primary.result;
            validation.state = INFERENCEOS_VFS_MOUNT_REJECTED;
            validation.error = INFERENCEFS_MOUNT_ERROR_PRIMARY_SUPERBLOCK;
            return validation;
        }
        validation.superblock = backup;
        use_backup = true;
    } else {
        if (!inferenceos_result_is_success(validation.backup.result)) {
            validation.result = validation.backup.result;
            validation.state = INFERENCEOS_VFS_MOUNT_REJECTED;
            validation.error = INFERENCEFS_MOUNT_ERROR_BACKUP_SUPERBLOCK;
            return validation;
        }
        if (!inferencefs_superblock_equal(&primary, &backup)) {
            validation.result = INFERENCEOS_RESULT_INCONSISTENT;
            validation.state = INFERENCEOS_VFS_MOUNT_REJECTED;
            validation.error = INFERENCEFS_MOUNT_ERROR_SUPERBLOCK_MISMATCH;
            return validation;
        }
        validation.superblock = primary;
    }
    validation.namespace_validation = inferencefs_validate_namespace(
        device, &validation.superblock, workspace);
    if (!inferenceos_result_is_success(validation.namespace_validation.result)) {
        validation.result = validation.namespace_validation.result;
        validation.state = INFERENCEOS_VFS_MOUNT_REJECTED;
        validation.error = INFERENCEFS_MOUNT_ERROR_NAMESPACE;
        return validation;
    }
    validation.result = INFERENCEOS_RESULT_OK;
    validation.error = INFERENCEFS_MOUNT_ERROR_NONE;
    validation.state = use_backup
            || info.status == INFERENCEOS_BLOCK_STATUS_READ_ONLY
        ? INFERENCEOS_VFS_MOUNT_DIAGNOSTIC_READ_ONLY
        : INFERENCEOS_VFS_MOUNT_CLEAN_WRITABLE;
    return validation;
}
