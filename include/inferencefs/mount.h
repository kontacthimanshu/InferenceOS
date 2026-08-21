#ifndef INFERENCEFS_MOUNT_H
#define INFERENCEFS_MOUNT_H

#include <inferencefs/validator.h>

typedef enum inferencefs_mount_error {
    INFERENCEFS_MOUNT_ERROR_NONE = 0,
    INFERENCEFS_MOUNT_ERROR_ARGUMENT = 1,
    INFERENCEFS_MOUNT_ERROR_DEVICE = 2,
    INFERENCEFS_MOUNT_ERROR_PRIMARY_SUPERBLOCK = 3,
    INFERENCEFS_MOUNT_ERROR_BACKUP_SUPERBLOCK = 4,
    INFERENCEFS_MOUNT_ERROR_SUPERBLOCK_MISMATCH = 5,
    INFERENCEFS_MOUNT_ERROR_NAMESPACE = 6
} inferencefs_mount_error;

typedef struct inferencefs_mount_validation {
    inferenceos_result result;
    inferenceos_vfs_mount_state state;
    inferencefs_mount_error error;
    inferencefs_superblock superblock;
    inferencefs_superblock_outcome primary;
    inferencefs_superblock_outcome backup;
    inferencefs_validation_outcome namespace_validation;
} inferencefs_mount_validation;

inferencefs_mount_validation inferencefs_mount_validate(
    const inferenceos_block_device *device,
    inferencefs_validator_workspace *workspace
);

#endif
