#ifndef INFERENCEFS_COMPANION_H
#define INFERENCEFS_COMPANION_H

#include <inferencefs/directory.h>

typedef enum inferencefs_companion_error {
    INFERENCEFS_COMPANION_ERROR_NONE = 0,
    INFERENCEFS_COMPANION_ERROR_ARGUMENT = 1,
    INFERENCEFS_COMPANION_ERROR_TYPE = 2,
    INFERENCEFS_COMPANION_ERROR_VERSION = 3,
    INFERENCEFS_COMPANION_ERROR_ALGORITHM = 4,
    INFERENCEFS_COMPANION_ERROR_FLAGS = 5,
    INFERENCEFS_COMPANION_ERROR_LENGTH = 6,
    INFERENCEFS_COMPANION_ERROR_RESERVED = 7,
    INFERENCEFS_COMPANION_ERROR_CRC = 8,
    INFERENCEFS_COMPANION_ERROR_NAME_CHECKSUM = 9,
    INFERENCEFS_COMPANION_ERROR_EXTENSION_HASH = 10
} inferencefs_companion_error;

typedef struct inferencefs_companion {
    inferenceos_u8 extension_length;
    inferenceos_u8 primary_name_checksum;
    inferenceos_u32 extension_hash;
    inferenceos_u32 stored_crc32;
    bool committed;
} inferencefs_companion;

typedef struct inferencefs_companion_outcome {
    inferenceos_result result;
    inferencefs_companion_error error;
    inferenceos_u32 stored_crc32;
    inferenceos_u32 computed_crc32;
} inferencefs_companion_outcome;

inferencefs_companion_outcome inferencefs_companion_decode(
    const inferencefs_companion_record_disk *disk,
    const inferenceos_u8 primary_name[INFERENCEFS_SHORT_NAME_SIZE],
    bool require_committed,
    inferencefs_companion *decoded
);
inferenceos_result inferencefs_companion_encode(
    inferencefs_companion_record_disk *disk,
    const inferenceos_u8 primary_name[INFERENCEFS_SHORT_NAME_SIZE],
    bool committed
);

#endif
