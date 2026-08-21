#ifndef INFERENCEFS_FAT_H
#define INFERENCEFS_FAT_H

#include <inferencefs/superblock.h>
#include <inferenceos/block_device.h>

typedef enum inferencefs_fat_value_kind {
    INFERENCEFS_FAT_VALUE_FREE = 0,
    INFERENCEFS_FAT_VALUE_NEXT = 1,
    INFERENCEFS_FAT_VALUE_BAD = 2,
    INFERENCEFS_FAT_VALUE_END = 3,
    INFERENCEFS_FAT_VALUE_INVALID = 4
} inferencefs_fat_value_kind;

typedef struct inferencefs_fat {
    const inferenceos_block_device *device;
    inferencefs_geometry geometry;
} inferencefs_fat;

typedef inferenceos_result (*inferencefs_fat_visit_fn)(
    void *context,
    inferenceos_u32 cluster,
    inferenceos_u32 chain_index
);

inferenceos_result inferencefs_fat_initialize(
    inferencefs_fat *fat,
    const inferenceos_block_device *device,
    const inferencefs_geometry *geometry
);
bool inferencefs_fat_cluster_is_valid(
    const inferencefs_fat *fat,
    inferenceos_u32 cluster
);
inferenceos_result inferencefs_fat_read(
    const inferencefs_fat *fat,
    inferenceos_u32 entry,
    inferenceos_u32 *value
);
inferencefs_fat_value_kind inferencefs_fat_classify(
    const inferencefs_fat *fat,
    inferenceos_u32 value
);
inferenceos_result inferencefs_fat_walk(
    const inferencefs_fat *fat,
    inferenceos_u32 first_cluster,
    inferencefs_fat_visit_fn visit,
    void *context,
    inferenceos_u32 *cluster_count
);
inferenceos_result inferencefs_cluster_first_lba(
    const inferencefs_fat *fat,
    inferenceos_u32 cluster,
    inferenceos_u64 *lba
);

#endif
