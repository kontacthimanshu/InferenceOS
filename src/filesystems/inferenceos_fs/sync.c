#include <inferenceos/fs/sync.h>

static bool valid_component(enum ios_fs_dirty_component component)
{
    const ios_u32 value = (ios_u32)component;
    return value != 0 && (value & ~((ios_u32)IOS_FS_DIRTY_ALL)) == 0;
}

ios_status ios_fs_sync_initialize(struct ios_fs_sync *sync, struct ios_block_cache *cache)
{
    if (sync == NULL || cache == NULL || cache->device == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *sync = (struct ios_fs_sync){ cache, cache->current_generation, 0, 0, IOS_OK };
    return IOS_OK;
}

ios_status ios_fs_sync_write_sector(
    struct ios_fs_sync *sync, ios_u64 sector, const void *bytes,
    enum ios_fs_dirty_component component
)
{
    ios_status status;
    if (sync == NULL || sync->cache == NULL || bytes == NULL || !valid_component(component)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    status = block_cache_write(sync->cache, sector, bytes, sync->generation);
    if (IOS_FAILED(status)) {
        sync->last_result = status;
        return status;
    }
    sync->dirty_components |= (ios_u32)component;
    return IOS_OK;
}

ios_status ios_fs_sync_barrier(struct ios_fs_sync *sync)
{
    ios_u64 next_generation;
    ios_status status;
    if (sync == NULL || sync->cache == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    if (sync->generation == UINT64_MAX) {
        sync->last_result = IOS_ERROR(IOS_E_OVERFLOW);
        return sync->last_result;
    }
    status = block_cache_barrier(sync->cache, sync->generation);
    sync->last_result = status;
    if (IOS_FAILED(status)) return status;
    sync->durable_generation = sync->generation;
    sync->dirty_components = 0;
    status = block_cache_advance_generation(sync->cache, &next_generation);
    if (IOS_FAILED(status)) {
        sync->last_result = status;
        return status;
    }
    sync->generation = next_generation;
    return IOS_OK;
}

ios_status ios_fs_sync_barrier_operation(void *context)
{
    return ios_fs_sync_barrier(context);
}

void ios_fs_sync_invalidate_operation(void *context)
{
    struct ios_fs_sync *sync = context;
    if (sync == NULL || sync->cache == NULL) return;
    block_cache_invalidate(sync->cache);
    sync->cache = NULL;
    sync->dirty_components = 0;
}

ios_status ios_fs_sync_all(struct ios_fs_sync *sync)
{
    return ios_fs_sync_barrier(sync);
}

bool ios_fs_sync_is_dirty(const struct ios_fs_sync *sync)
{
    return sync != NULL && sync->dirty_components != 0;
}

ios_status ios_fs_sync_durable_result(const struct ios_fs_sync *sync)
{
    if (sync == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    if (IOS_FAILED(sync->last_result)) return sync->last_result;
    return sync->dirty_components == 0 ? IOS_OK : IOS_ERROR(IOS_E_WOULD_BLOCK);
}
