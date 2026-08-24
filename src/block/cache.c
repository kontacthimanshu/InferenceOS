#include <inferenceos/block.h>

#include <inferenceos/runtime.h>

static struct ios_block_cache_entry *find_entry(struct ios_block_cache *cache, ios_u64 sector)
{
    for (ios_size index = 0; index < cache->entry_count; ++index) {
        if (cache->entries[index].state != IOS_BLOCK_CACHE_EMPTY
            && cache->entries[index].sector == sector) return &cache->entries[index];
    }
    return NULL;
}

static struct ios_block_cache_entry *select_victim(struct ios_block_cache *cache)
{
    struct ios_block_cache_entry *victim = NULL;
    for (ios_size index = 0; index < cache->entry_count; ++index) {
        struct ios_block_cache_entry *entry = &cache->entries[index];
        if (entry->pin_count != 0 || entry->io_active) continue;
        if (entry->state == IOS_BLOCK_CACHE_EMPTY) return entry;
        if (entry->state == IOS_BLOCK_CACHE_CLEAN
            && (victim == NULL || entry->last_use < victim->last_use)) victim = entry;
    }
    return victim;
}

static ios_status acquire_entry(
    struct ios_block_cache *cache, ios_u64 sector, bool load, struct ios_block_cache_entry **result)
{
    struct ios_block_cache_entry *entry;
    ios_status status;
    if (sector >= cache->device->sector_count) return IOS_ERROR(IOS_E_OUT_OF_RANGE);
    entry = find_entry(cache, sector);
    if (entry != NULL) {
        entry->last_use = ++cache->use_clock;
        *result = entry;
        return IOS_OK;
    }
    entry = select_victim(cache);
    if (entry == NULL) return IOS_ERROR(IOS_E_WOULD_BLOCK);
    memset(entry, 0, sizeof(*entry));
    entry->sector = sector;
    entry->last_use = ++cache->use_clock;
    if (load) {
        entry->io_active = true;
        status = block_device_read(cache->device, sector, 1, entry->bytes);
        entry->io_active = false;
        if (IOS_FAILED(status)) {
            memset(entry, 0, sizeof(*entry));
            return status;
        }
    }
    entry->state = IOS_BLOCK_CACHE_CLEAN;
    *result = entry;
    return IOS_OK;
}

ios_status block_cache_initialize(
    struct ios_block_cache *cache, struct ios_block_device *device,
    struct ios_block_cache_entry *entry_storage, ios_size entry_count)
{
    if (cache == NULL || device == NULL || entry_storage == NULL || entry_count == 0
        || entry_count > IOS_BLOCK_CACHE_MAX_ENTRIES
        || device->logical_sector_size != IOS_BLOCK_SECTOR_SIZE
        || device->status == IOS_BLOCK_DEVICE_OFFLINE || device->status == IOS_BLOCK_DEVICE_FAILED) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    memset(entry_storage, 0, entry_count * sizeof(*entry_storage));
    *cache = (struct ios_block_cache){ device, entry_storage, entry_count, 1, 0 };
    return IOS_OK;
}

ios_status block_cache_read(struct ios_block_cache *cache, ios_u64 sector, void *buffer)
{
    struct ios_block_cache_entry *entry;
    ios_status status;
    if (cache == NULL || buffer == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    status = acquire_entry(cache, sector, true, &entry);
    if (IOS_FAILED(status)) return status;
    memcpy(buffer, entry->bytes, IOS_BLOCK_SECTOR_SIZE);
    return IOS_OK;
}

ios_status block_cache_write(
    struct ios_block_cache *cache, ios_u64 sector, const void *buffer, ios_u64 generation)
{
    struct ios_block_cache_entry *entry;
    ios_status status;
    if (cache == NULL || buffer == NULL || generation == 0
        || generation > cache->current_generation) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    if (cache->device->status == IOS_BLOCK_DEVICE_READ_ONLY) return IOS_ERROR(IOS_E_READ_ONLY);
    status = acquire_entry(cache, sector, false, &entry);
    if (IOS_FAILED(status)) return status;
    memcpy(entry->bytes, buffer, IOS_BLOCK_SECTOR_SIZE);
    entry->dirty_generation = generation;
    entry->state = IOS_BLOCK_CACHE_DIRTY;
    return IOS_OK;
}

ios_status block_cache_pin(struct ios_block_cache *cache, ios_u64 sector)
{
    struct ios_block_cache_entry *entry;
    ios_status status;
    if (cache == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    status = acquire_entry(cache, sector, true, &entry);
    if (IOS_FAILED(status)) return status;
    if (entry->pin_count == UINT32_MAX) return IOS_ERROR(IOS_E_OVERFLOW);
    ++entry->pin_count;
    return IOS_OK;
}

ios_status block_cache_unpin(struct ios_block_cache *cache, ios_u64 sector)
{
    struct ios_block_cache_entry *entry;
    if (cache == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    entry = find_entry(cache, sector);
    if (entry == NULL || entry->pin_count == 0) return IOS_ERROR(IOS_E_INVALID_STATE);
    --entry->pin_count;
    return IOS_OK;
}

ios_status block_cache_advance_generation(struct ios_block_cache *cache, ios_u64 *generation)
{
    if (cache == NULL || generation == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    if (cache->current_generation == UINT64_MAX) return IOS_ERROR(IOS_E_OVERFLOW);
    *generation = ++cache->current_generation;
    return IOS_OK;
}

ios_status block_cache_barrier(struct ios_block_cache *cache, ios_u64 target_generation)
{
    ios_status first_error = IOS_OK;
    if (cache == NULL || target_generation == 0
        || target_generation > cache->current_generation) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    for (ios_size index = 0; index < cache->entry_count; ++index) {
        struct ios_block_cache_entry *entry = &cache->entries[index];
        ios_status status;
        if ((entry->state != IOS_BLOCK_CACHE_DIRTY && entry->state != IOS_BLOCK_CACHE_ERROR)
            || entry->dirty_generation > target_generation) continue;
        entry->io_active = true;
        status = block_device_write(cache->device, entry->sector, 1, entry->bytes);
        entry->io_active = false;
        if (IOS_FAILED(status)) {
            entry->state = IOS_BLOCK_CACHE_ERROR;
            if (IOS_SUCCEEDED(first_error)) first_error = status;
        } else {
            entry->state = IOS_BLOCK_CACHE_DIRTY;
        }
    }
    if (IOS_FAILED(first_error)) return first_error;
    first_error = block_device_flush(cache->device);
    if (IOS_FAILED(first_error)) {
        for (ios_size index = 0; index < cache->entry_count; ++index) {
            struct ios_block_cache_entry *entry = &cache->entries[index];
            if (entry->state == IOS_BLOCK_CACHE_DIRTY
                && entry->dirty_generation <= target_generation) entry->state = IOS_BLOCK_CACHE_ERROR;
        }
        return first_error;
    }
    for (ios_size index = 0; index < cache->entry_count; ++index) {
        struct ios_block_cache_entry *entry = &cache->entries[index];
        if ((entry->state == IOS_BLOCK_CACHE_DIRTY || entry->state == IOS_BLOCK_CACHE_ERROR)
            && entry->dirty_generation <= target_generation) {
            entry->state = IOS_BLOCK_CACHE_CLEAN;
            entry->dirty_generation = 0;
        }
    }
    return IOS_OK;
}

void block_cache_invalidate(struct ios_block_cache *cache)
{
    if (cache == NULL || cache->entries == NULL) return;
    memset(cache->entries, 0, cache->entry_count * sizeof(*cache->entries));
    cache->use_clock = 0;
}
