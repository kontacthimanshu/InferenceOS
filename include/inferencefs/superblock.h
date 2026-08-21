#ifndef INFERENCEFS_SUPERBLOCK_H
#define INFERENCEFS_SUPERBLOCK_H

#include <inferencefs/formatter.h>

typedef enum inferencefs_superblock_error {
    INFERENCEFS_SUPERBLOCK_ERROR_NONE = 0,
    INFERENCEFS_SUPERBLOCK_ERROR_ARGUMENT = 1,
    INFERENCEFS_SUPERBLOCK_ERROR_MAGIC = 2,
    INFERENCEFS_SUPERBLOCK_ERROR_CRC = 3,
    INFERENCEFS_SUPERBLOCK_ERROR_VERSION = 4,
    INFERENCEFS_SUPERBLOCK_ERROR_FIXED_FIELD = 5,
    INFERENCEFS_SUPERBLOCK_ERROR_CAPACITY = 6,
    INFERENCEFS_SUPERBLOCK_ERROR_GEOMETRY = 7,
    INFERENCEFS_SUPERBLOCK_ERROR_LABEL = 8,
    INFERENCEFS_SUPERBLOCK_ERROR_RESERVED = 9,
    INFERENCEFS_SUPERBLOCK_ERROR_TRAILER = 10
} inferencefs_superblock_error;

typedef struct inferencefs_superblock_outcome {
    inferenceos_result result;
    inferencefs_superblock_error error;
    inferenceos_u32 stored_crc32;
    inferenceos_u32 computed_crc32;
} inferencefs_superblock_outcome;

typedef struct inferencefs_superblock {
    inferencefs_geometry geometry;
    inferenceos_u32 root_cluster;
    inferenceos_u32 volume_serial;
    inferenceos_u8 volume_label[11];
    inferenceos_u8 hash_algorithm_id;
    inferenceos_u16 companion_record_version;
    inferenceos_u16 primary_record_version;
} inferencefs_superblock;

/* Decode one complete logical sector against the physical device capacity.
 * Geometry is derived only after all fixed fields and the CRC are validated.
 * decoded is unchanged when the returned result is not OK. */
inferencefs_superblock_outcome inferencefs_superblock_decode(
    const void *sector,
    inferenceos_size sector_size,
    inferenceos_u64 device_sector_count,
    inferencefs_superblock *decoded
);

/* Compare every required version-1 field represented by a decoded block. */
bool inferencefs_superblock_equal(
    const inferencefs_superblock *left,
    const inferencefs_superblock *right
);

#endif
