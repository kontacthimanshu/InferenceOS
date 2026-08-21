#ifndef INFERENCEFS_VALIDATOR_H
#define INFERENCEFS_VALIDATOR_H

#include <inferencefs/fat.h>
#include <inferenceos/vfs.h>

#define INFERENCEFS_VALIDATOR_OWNERSHIP_BYTES 32768U

typedef enum inferencefs_validation_error {
    INFERENCEFS_VALIDATION_ERROR_NONE = 0,
    INFERENCEFS_VALIDATION_ERROR_ARGUMENT = 1,
    INFERENCEFS_VALIDATION_ERROR_IO = 2,
    INFERENCEFS_VALIDATION_ERROR_RESERVED_FAT = 3,
    INFERENCEFS_VALIDATION_ERROR_FAT_VALUE = 4,
    INFERENCEFS_VALIDATION_ERROR_FAT_LOOP = 5,
    INFERENCEFS_VALIDATION_ERROR_FAT_CROSS_LINK = 6,
    INFERENCEFS_VALIDATION_ERROR_DIRECTORY_SLOT = 7,
    INFERENCEFS_VALIDATION_ERROR_COMPANION = 8,
    INFERENCEFS_VALIDATION_ERROR_CHAIN_SIZE = 9,
    INFERENCEFS_VALIDATION_ERROR_DEPTH = 10
} inferencefs_validation_error;

typedef struct inferencefs_validator_workspace {
    inferenceos_u8 ownership[INFERENCEFS_VALIDATOR_OWNERSHIP_BYTES];
} inferencefs_validator_workspace;

typedef struct inferencefs_validation_outcome {
    inferenceos_result result;
    inferencefs_validation_error error;
    inferenceos_u32 cluster;
} inferencefs_validation_outcome;

inferencefs_validation_outcome inferencefs_validate_namespace(
    const inferenceos_block_device *device,
    const inferencefs_superblock *superblock,
    inferencefs_validator_workspace *workspace
);

#endif
