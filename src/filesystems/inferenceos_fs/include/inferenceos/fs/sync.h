#ifndef INFERENCEOS_FS_SYNC_H
#define INFERENCEOS_FS_SYNC_H

#include <inferenceos/block.h>

enum ios_fs_dirty_component {
    IOS_FS_DIRTY_CONTENT = 1U << 0,
    IOS_FS_DIRTY_ALLOCATION = 1U << 1,
    IOS_FS_DIRTY_PRIMARY = 1U << 2,
    IOS_FS_DIRTY_COMPANION = 1U << 3,
    IOS_FS_DIRTY_REGISTRY = 1U << 4,
    IOS_FS_DIRTY_ALL = (1U << 5) - 1U
};

struct ios_fs_sync {
    struct ios_block_cache *cache;
    ios_u64 generation;
    ios_u64 durable_generation;
    ios_u32 dirty_components;
    ios_status last_result;
};

ios_status ios_fs_sync_initialize(
    struct ios_fs_sync *sync, struct ios_block_cache *cache
);
ios_status ios_fs_sync_write_sector(
    struct ios_fs_sync *sync,
    ios_u64 sector,
    const void *bytes,
    enum ios_fs_dirty_component component
);
ios_status ios_fs_sync_barrier(struct ios_fs_sync *sync);
ios_status ios_fs_sync_barrier_operation(void *context);
void ios_fs_sync_invalidate_operation(void *context);
ios_status ios_fs_sync_all(struct ios_fs_sync *sync);
bool ios_fs_sync_is_dirty(const struct ios_fs_sync *sync);
ios_status ios_fs_sync_durable_result(const struct ios_fs_sync *sync);

#endif
