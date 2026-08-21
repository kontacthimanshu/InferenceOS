#include <inferenceos/block_cache.h>
#include <inferenceos/memory.h>

static inferenceos_block_outcome block_success(inferenceos_u32 completed)
{
    const inferenceos_block_outcome outcome = {
        .result = INFERENCEOS_RESULT_OK,
        .error = INFERENCEOS_BLOCK_ERROR_NONE,
        .driver_detail = 0U,
        .sectors_completed = completed
    };
    return outcome;
}

static inferenceos_block_outcome block_failure(
    inferenceos_result result,
    inferenceos_block_error error
)
{
    return inferenceos_block_failure(result, error);
}

static inferenceos_block_cache_outcome cache_outcome(
    inferenceos_result result,
    inferenceos_block_cache_error error,
    inferenceos_block_outcome block_result
)
{
    const inferenceos_block_cache_outcome outcome = {
        .result = result,
        .error = error,
        .block_outcome = block_result
    };
    return outcome;
}

static inferenceos_block_cache_outcome cache_success(void)
{
    return cache_outcome(
        INFERENCEOS_RESULT_OK,
        INFERENCEOS_BLOCK_CACHE_ERROR_NONE,
        block_success(0U)
    );
}

static inferenceos_block_cache_outcome cache_failure(
    inferenceos_block_cache *cache,
    inferenceos_result result,
    inferenceos_block_cache_error error,
    inferenceos_block_outcome block_result
)
{
    if (cache != NULL) {
        cache->last_error = error;
        cache->last_block_outcome = block_result;
    }
    return cache_outcome(result, error, block_result);
}

static bool entry_belongs_to_cache(
    const inferenceos_block_cache *cache,
    const inferenceos_block_cache_entry *entry
)
{
    const inferenceos_uptr start = (inferenceos_uptr)&cache->entries[0];
    const inferenceos_uptr address = (inferenceos_uptr)entry;
    const inferenceos_size entries_size = sizeof(cache->entries);

    return address >= start
        && address - start < entries_size
        && (address - start) % sizeof(cache->entries[0]) == 0U;
}

static void rebuild_replacement_ages(inferenceos_block_cache *cache)
{
    inferenceos_u64 age = 1U;

    for (inferenceos_size index = 0U;
         index < INFERENCEOS_BLOCK_CACHE_CAPACITY;
         ++index) {
        if (cache->entries[index].state != INFERENCEOS_BLOCK_CACHE_ENTRY_INVALID) {
            cache->entries[index].replacement_age = age;
            ++age;
        }
    }
    cache->next_replacement_age = age;
}

static void touch_entry(
    inferenceos_block_cache *cache,
    inferenceos_block_cache_entry *entry
)
{
    if (cache->next_replacement_age == UINT64_MAX) {
        rebuild_replacement_ages(cache);
    }
    entry->replacement_age = cache->next_replacement_age;
    ++cache->next_replacement_age;
}

static inferenceos_block_cache_entry *find_entry(
    inferenceos_block_cache *cache,
    const inferenceos_block_device *device,
    inferenceos_u64 lba
)
{
    for (inferenceos_size index = 0U;
         index < INFERENCEOS_BLOCK_CACHE_CAPACITY;
         ++index) {
        inferenceos_block_cache_entry *candidate = &cache->entries[index];
        if (candidate->state != INFERENCEOS_BLOCK_CACHE_ENTRY_INVALID
            && candidate->device == device
            && candidate->lba == lba) {
            return candidate;
        }
    }
    return NULL;
}

static inferenceos_block_cache_entry *select_victim(
    inferenceos_block_cache *cache
)
{
    inferenceos_block_cache_entry *oldest = NULL;

    for (inferenceos_size index = 0U;
         index < INFERENCEOS_BLOCK_CACHE_CAPACITY;
         ++index) {
        inferenceos_block_cache_entry *candidate = &cache->entries[index];

        if (candidate->state == INFERENCEOS_BLOCK_CACHE_ENTRY_INVALID) {
            return candidate;
        }
        if (candidate->pin_count == 0U
            && (oldest == NULL
                || candidate->replacement_age < oldest->replacement_age)) {
            oldest = candidate;
        }
    }
    return oldest;
}

static inferenceos_block_outcome require_complete_sector(
    inferenceos_block_outcome outcome
)
{
    if (inferenceos_block_outcome_is_success(outcome)
        && outcome.sectors_completed != 1U) {
        return block_failure(
            INFERENCEOS_RESULT_IO_ERROR,
            INFERENCEOS_BLOCK_ERROR_PARTIAL_TRANSFER
        );
    }
    return outcome;
}

static inferenceos_block_outcome writeback_entry(
    inferenceos_block_cache_entry *entry
)
{
    inferenceos_block_outcome outcome = require_complete_sector(
        inferenceos_block_write(entry->device, entry->lba, 1U, entry->data)
    );

    entry->last_block_outcome = outcome;
    if (inferenceos_block_outcome_is_success(outcome)) {
        entry->state = INFERENCEOS_BLOCK_CACHE_ENTRY_CLEAN;
    } else {
        entry->state = INFERENCEOS_BLOCK_CACHE_ENTRY_WRITEBACK_FAILED;
    }
    return outcome;
}

static inferenceos_block_cache_outcome validate_device_lba(
    inferenceos_block_cache *cache,
    const inferenceos_block_device *device,
    inferenceos_u64 lba
)
{
    inferenceos_block_info info;
    inferenceos_block_outcome outcome = inferenceos_block_query(device, &info);

    if (!inferenceos_block_outcome_is_success(outcome)) {
        return cache_failure(cache, outcome.result,
            INFERENCEOS_BLOCK_CACHE_ERROR_READ_FAILED, outcome);
    }
    if (info.geometry.logical_sector_size != INFERENCEOS_BLOCK_CACHE_SECTOR_SIZE) {
        return cache_failure(cache, INFERENCEOS_RESULT_UNSUPPORTED,
            INFERENCEOS_BLOCK_CACHE_ERROR_UNSUPPORTED_SECTOR_SIZE,
            block_failure(INFERENCEOS_RESULT_UNSUPPORTED,
                INFERENCEOS_BLOCK_ERROR_INVALID_ARGUMENT));
    }
    if (lba >= info.geometry.sector_count) {
        return cache_failure(cache, INFERENCEOS_RESULT_OUT_OF_RANGE,
            INFERENCEOS_BLOCK_CACHE_ERROR_OUT_OF_RANGE,
            block_failure(INFERENCEOS_RESULT_OUT_OF_RANGE,
                INFERENCEOS_BLOCK_ERROR_OUT_OF_RANGE));
    }
    if (info.status != INFERENCEOS_BLOCK_STATUS_READY
        && info.status != INFERENCEOS_BLOCK_STATUS_READ_ONLY) {
        const inferenceos_result result = info.status == INFERENCEOS_BLOCK_STATUS_BUSY
            ? INFERENCEOS_RESULT_BUSY
            : INFERENCEOS_RESULT_NOT_READY;
        return cache_failure(cache, result,
            INFERENCEOS_BLOCK_CACHE_ERROR_READ_FAILED,
            block_failure(result, INFERENCEOS_BLOCK_ERROR_NOT_READY));
    }
    return cache_success();
}

inferenceos_result inferenceos_block_cache_initialize(
    inferenceos_block_cache *cache
)
{
    if (cache == NULL) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    (void)memset(cache, 0, sizeof(*cache));
    cache->next_replacement_age = 1U;
    cache->flush_state = INFERENCEOS_BLOCK_CACHE_FLUSH_IDLE;
    cache->last_error = INFERENCEOS_BLOCK_CACHE_ERROR_NONE;
    cache->last_block_outcome = block_success(0U);
    cache->initialized = true;
    return INFERENCEOS_RESULT_OK;
}

inferenceos_block_cache_outcome inferenceos_block_cache_acquire(
    inferenceos_block_cache *cache,
    const inferenceos_block_device *device,
    inferenceos_u64 lba,
    inferenceos_block_cache_entry **entry
)
{
    inferenceos_block_cache_entry *candidate;
    inferenceos_block_cache_outcome validation;
    inferenceos_block_outcome block_result;
    inferenceos_u8 sector[INFERENCEOS_BLOCK_CACHE_SECTOR_SIZE];

    if (cache == NULL || device == NULL || entry == NULL) {
        return cache_failure(cache, INFERENCEOS_RESULT_INVALID_ARGUMENT,
            INFERENCEOS_BLOCK_CACHE_ERROR_INVALID_ARGUMENT,
            block_failure(INFERENCEOS_RESULT_INVALID_ARGUMENT,
                INFERENCEOS_BLOCK_ERROR_INVALID_ARGUMENT));
    }
    *entry = NULL;
    if (!cache->initialized) {
        return cache_failure(cache, INFERENCEOS_RESULT_NOT_READY,
            INFERENCEOS_BLOCK_CACHE_ERROR_NOT_INITIALIZED,
            block_failure(INFERENCEOS_RESULT_NOT_READY,
                INFERENCEOS_BLOCK_ERROR_NOT_READY));
    }

    candidate = find_entry(cache, device, lba);
    if (candidate != NULL) {
        if (candidate->pin_count == UINT32_MAX) {
            return cache_failure(cache, INFERENCEOS_RESULT_OVERFLOW,
                INFERENCEOS_BLOCK_CACHE_ERROR_PIN_OVERFLOW,
                block_failure(INFERENCEOS_RESULT_OVERFLOW,
                    INFERENCEOS_BLOCK_ERROR_ARITHMETIC_OVERFLOW));
        }
        ++candidate->pin_count;
        touch_entry(cache, candidate);
        *entry = candidate;
        return cache_success();
    }

    candidate = select_victim(cache);
    if (candidate == NULL) {
        return cache_failure(cache, INFERENCEOS_RESULT_BUSY,
            INFERENCEOS_BLOCK_CACHE_ERROR_ALL_ENTRIES_PINNED,
            block_failure(INFERENCEOS_RESULT_BUSY,
                INFERENCEOS_BLOCK_ERROR_NOT_READY));
    }
    validation = validate_device_lba(cache, device, lba);
    if (!inferenceos_block_cache_outcome_is_success(validation)) {
        return validation;
    }
    if (candidate->state == INFERENCEOS_BLOCK_CACHE_ENTRY_DIRTY
        || candidate->state == INFERENCEOS_BLOCK_CACHE_ENTRY_WRITEBACK_FAILED) {
        block_result = writeback_entry(candidate);
        if (!inferenceos_block_outcome_is_success(block_result)) {
            return cache_failure(cache, block_result.result,
                INFERENCEOS_BLOCK_CACHE_ERROR_WRITEBACK_FAILED, block_result);
        }
    }

    block_result = require_complete_sector(
        inferenceos_block_read(device, lba, 1U, sector)
    );
    if (!inferenceos_block_outcome_is_success(block_result)) {
        return cache_failure(cache, block_result.result,
            INFERENCEOS_BLOCK_CACHE_ERROR_READ_FAILED, block_result);
    }

    (void)memcpy(candidate->data, sector, sizeof(sector));
    candidate->device = device;
    candidate->lba = lba;
    candidate->pin_count = 1U;
    candidate->state = INFERENCEOS_BLOCK_CACHE_ENTRY_CLEAN;
    candidate->last_block_outcome = block_result;
    touch_entry(cache, candidate);
    *entry = candidate;
    return cache_outcome(
        INFERENCEOS_RESULT_OK,
        INFERENCEOS_BLOCK_CACHE_ERROR_NONE,
        block_result
    );
}

inferenceos_result inferenceos_block_cache_mark_dirty(
    inferenceos_block_cache *cache,
    inferenceos_block_cache_entry *entry
)
{
    if (cache == NULL || entry == NULL) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    if (!cache->initialized) {
        cache->last_error = INFERENCEOS_BLOCK_CACHE_ERROR_NOT_INITIALIZED;
        return INFERENCEOS_RESULT_NOT_READY;
    }
    if (!entry_belongs_to_cache(cache, entry)
        || entry->state == INFERENCEOS_BLOCK_CACHE_ENTRY_INVALID) {
        cache->last_error = INFERENCEOS_BLOCK_CACHE_ERROR_ENTRY_MISMATCH;
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    if (entry->pin_count == 0U) {
        cache->last_error = INFERENCEOS_BLOCK_CACHE_ERROR_PIN_UNDERFLOW;
        return INFERENCEOS_RESULT_INCONSISTENT;
    }
    entry->state = INFERENCEOS_BLOCK_CACHE_ENTRY_DIRTY;
    return INFERENCEOS_RESULT_OK;
}

inferenceos_result inferenceos_block_cache_release(
    inferenceos_block_cache *cache,
    inferenceos_block_cache_entry *entry
)
{
    if (cache == NULL || entry == NULL) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    if (!cache->initialized) {
        cache->last_error = INFERENCEOS_BLOCK_CACHE_ERROR_NOT_INITIALIZED;
        return INFERENCEOS_RESULT_NOT_READY;
    }
    if (!entry_belongs_to_cache(cache, entry)
        || entry->state == INFERENCEOS_BLOCK_CACHE_ENTRY_INVALID) {
        cache->last_error = INFERENCEOS_BLOCK_CACHE_ERROR_ENTRY_MISMATCH;
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    if (entry->pin_count == 0U) {
        cache->last_error = INFERENCEOS_BLOCK_CACHE_ERROR_PIN_UNDERFLOW;
        return INFERENCEOS_RESULT_INCONSISTENT;
    }
    --entry->pin_count;
    return INFERENCEOS_RESULT_OK;
}

inferenceos_block_cache_outcome inferenceos_block_cache_flush(
    inferenceos_block_cache *cache,
    const inferenceos_block_device *device
)
{
    inferenceos_block_outcome block_result;

    if (cache == NULL || device == NULL) {
        return cache_failure(cache, INFERENCEOS_RESULT_INVALID_ARGUMENT,
            INFERENCEOS_BLOCK_CACHE_ERROR_INVALID_ARGUMENT,
            block_failure(INFERENCEOS_RESULT_INVALID_ARGUMENT,
                INFERENCEOS_BLOCK_ERROR_INVALID_ARGUMENT));
    }
    if (!cache->initialized) {
        return cache_failure(cache, INFERENCEOS_RESULT_NOT_READY,
            INFERENCEOS_BLOCK_CACHE_ERROR_NOT_INITIALIZED,
            block_failure(INFERENCEOS_RESULT_NOT_READY,
                INFERENCEOS_BLOCK_ERROR_NOT_READY));
    }

    for (inferenceos_size index = 0U;
         index < INFERENCEOS_BLOCK_CACHE_CAPACITY;
         ++index) {
        const inferenceos_block_cache_entry *candidate = &cache->entries[index];
        if (candidate->device == device && candidate->pin_count != 0U
            && (candidate->state == INFERENCEOS_BLOCK_CACHE_ENTRY_DIRTY
                || candidate->state
                    == INFERENCEOS_BLOCK_CACHE_ENTRY_WRITEBACK_FAILED)) {
            cache->flush_state = INFERENCEOS_BLOCK_CACHE_FLUSH_FAILED;
            return cache_failure(cache, INFERENCEOS_RESULT_BUSY,
                INFERENCEOS_BLOCK_CACHE_ERROR_PINNED_DIRTY,
                block_failure(INFERENCEOS_RESULT_BUSY,
                    INFERENCEOS_BLOCK_ERROR_NOT_READY));
        }
    }

    cache->flush_state = INFERENCEOS_BLOCK_CACHE_FLUSH_WRITING_DIRTY;
    for (inferenceos_size index = 0U;
         index < INFERENCEOS_BLOCK_CACHE_CAPACITY;
         ++index) {
        inferenceos_block_cache_entry *candidate = &cache->entries[index];
        if (candidate->device != device
            || (candidate->state != INFERENCEOS_BLOCK_CACHE_ENTRY_DIRTY
                && candidate->state
                    != INFERENCEOS_BLOCK_CACHE_ENTRY_WRITEBACK_FAILED)) {
            continue;
        }
        block_result = writeback_entry(candidate);
        if (!inferenceos_block_outcome_is_success(block_result)) {
            cache->flush_state = INFERENCEOS_BLOCK_CACHE_FLUSH_FAILED;
            return cache_failure(cache, block_result.result,
                INFERENCEOS_BLOCK_CACHE_ERROR_WRITEBACK_FAILED, block_result);
        }
    }

    cache->flush_state = INFERENCEOS_BLOCK_CACHE_FLUSHING_DEVICE;
    block_result = inferenceos_block_flush(device);
    if (!inferenceos_block_outcome_is_success(block_result)) {
        cache->flush_state = INFERENCEOS_BLOCK_CACHE_FLUSH_FAILED;
        return cache_failure(cache, block_result.result,
            INFERENCEOS_BLOCK_CACHE_ERROR_DEVICE_FLUSH_FAILED, block_result);
    }
    cache->flush_state = INFERENCEOS_BLOCK_CACHE_FLUSH_COMPLETE;
    cache->last_error = INFERENCEOS_BLOCK_CACHE_ERROR_NONE;
    cache->last_block_outcome = block_result;
    return cache_success();
}

inferenceos_result inferenceos_block_cache_invalidate(
    inferenceos_block_cache *cache,
    const inferenceos_block_device *device
)
{
    if (cache == NULL || device == NULL) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    if (!cache->initialized) {
        cache->last_error = INFERENCEOS_BLOCK_CACHE_ERROR_NOT_INITIALIZED;
        return INFERENCEOS_RESULT_NOT_READY;
    }
    for (inferenceos_size index = 0U;
         index < INFERENCEOS_BLOCK_CACHE_CAPACITY;
         ++index) {
        const inferenceos_block_cache_entry *candidate = &cache->entries[index];
        if (candidate->device == device
            && (candidate->pin_count != 0U
                || candidate->state == INFERENCEOS_BLOCK_CACHE_ENTRY_DIRTY
                || candidate->state
                    == INFERENCEOS_BLOCK_CACHE_ENTRY_WRITEBACK_FAILED)) {
            cache->last_error = candidate->pin_count != 0U
                ? INFERENCEOS_BLOCK_CACHE_ERROR_ALL_ENTRIES_PINNED
                : INFERENCEOS_BLOCK_CACHE_ERROR_WRITEBACK_FAILED;
            return INFERENCEOS_RESULT_BUSY;
        }
    }
    for (inferenceos_size index = 0U;
         index < INFERENCEOS_BLOCK_CACHE_CAPACITY;
         ++index) {
        inferenceos_block_cache_entry *candidate = &cache->entries[index];
        if (candidate->device == device) {
            (void)memset(candidate, 0, sizeof(*candidate));
        }
    }
    return INFERENCEOS_RESULT_OK;
}

inferenceos_result inferenceos_block_cache_query(
    const inferenceos_block_cache *cache,
    inferenceos_block_cache_status *status
)
{
    inferenceos_block_cache_status result = { .valid_entries = 0U };

    if (cache == NULL || status == NULL) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    if (!cache->initialized) {
        return INFERENCEOS_RESULT_NOT_READY;
    }
    for (inferenceos_size index = 0U;
         index < INFERENCEOS_BLOCK_CACHE_CAPACITY;
         ++index) {
        const inferenceos_block_cache_entry *entry = &cache->entries[index];
        if (entry->state != INFERENCEOS_BLOCK_CACHE_ENTRY_INVALID) {
            ++result.valid_entries;
        }
        if (entry->state == INFERENCEOS_BLOCK_CACHE_ENTRY_DIRTY
            || entry->state == INFERENCEOS_BLOCK_CACHE_ENTRY_WRITEBACK_FAILED) {
            ++result.dirty_entries;
        }
        if (entry->pin_count != 0U) {
            ++result.pinned_entries;
        }
    }
    result.flush_state = cache->flush_state;
    result.last_error = cache->last_error;
    result.last_block_outcome = cache->last_block_outcome;
    *status = result;
    return INFERENCEOS_RESULT_OK;
}
