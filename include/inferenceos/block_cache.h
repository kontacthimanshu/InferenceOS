#ifndef INFERENCEOS_BLOCK_CACHE_H
#define INFERENCEOS_BLOCK_CACHE_H

#include <inferenceos/base.h>
#include <inferenceos/block_device.h>
#include <inferenceos/result.h>

#define INFERENCEOS_BLOCK_CACHE_CAPACITY 64U
#define INFERENCEOS_BLOCK_CACHE_SECTOR_SIZE 512U

typedef enum inferenceos_block_cache_entry_state {
    INFERENCEOS_BLOCK_CACHE_ENTRY_INVALID = 0,
    INFERENCEOS_BLOCK_CACHE_ENTRY_CLEAN = 1,
    INFERENCEOS_BLOCK_CACHE_ENTRY_DIRTY = 2,
    /* A failed writeback remains dirty and may be retried. */
    INFERENCEOS_BLOCK_CACHE_ENTRY_WRITEBACK_FAILED = 3
} inferenceos_block_cache_entry_state;

typedef enum inferenceos_block_cache_flush_state {
    INFERENCEOS_BLOCK_CACHE_FLUSH_IDLE = 0,
    INFERENCEOS_BLOCK_CACHE_FLUSH_WRITING_DIRTY = 1,
    INFERENCEOS_BLOCK_CACHE_FLUSHING_DEVICE = 2,
    INFERENCEOS_BLOCK_CACHE_FLUSH_COMPLETE = 3,
    INFERENCEOS_BLOCK_CACHE_FLUSH_FAILED = 4
} inferenceos_block_cache_flush_state;

typedef enum inferenceos_block_cache_error {
    INFERENCEOS_BLOCK_CACHE_ERROR_NONE = 0,
    INFERENCEOS_BLOCK_CACHE_ERROR_INVALID_ARGUMENT = 1,
    INFERENCEOS_BLOCK_CACHE_ERROR_NOT_INITIALIZED = 2,
    INFERENCEOS_BLOCK_CACHE_ERROR_UNSUPPORTED_SECTOR_SIZE = 3,
    INFERENCEOS_BLOCK_CACHE_ERROR_OUT_OF_RANGE = 4,
    INFERENCEOS_BLOCK_CACHE_ERROR_ALL_ENTRIES_PINNED = 5,
    INFERENCEOS_BLOCK_CACHE_ERROR_READ_FAILED = 6,
    INFERENCEOS_BLOCK_CACHE_ERROR_WRITEBACK_FAILED = 7,
    INFERENCEOS_BLOCK_CACHE_ERROR_DEVICE_FLUSH_FAILED = 8,
    INFERENCEOS_BLOCK_CACHE_ERROR_PIN_OVERFLOW = 9,
    INFERENCEOS_BLOCK_CACHE_ERROR_PIN_UNDERFLOW = 10,
    INFERENCEOS_BLOCK_CACHE_ERROR_ENTRY_MISMATCH = 11,
    INFERENCEOS_BLOCK_CACHE_ERROR_PINNED_DIRTY = 12
} inferenceos_block_cache_error;

typedef struct inferenceos_block_cache_outcome {
    inferenceos_result result;
    inferenceos_block_cache_error error;
    inferenceos_block_outcome block_outcome;
} inferenceos_block_cache_outcome;

typedef struct inferenceos_block_cache_entry {
    const inferenceos_block_device *device;
    inferenceos_u64 lba;
    inferenceos_u64 replacement_age;
    inferenceos_u32 pin_count;
    inferenceos_block_cache_entry_state state;
    inferenceos_block_outcome last_block_outcome;
    inferenceos_u8 data[INFERENCEOS_BLOCK_CACHE_SECTOR_SIZE];
} inferenceos_block_cache_entry;

typedef struct inferenceos_block_cache_status {
    inferenceos_size valid_entries;
    inferenceos_size dirty_entries;
    inferenceos_size pinned_entries;
    inferenceos_block_cache_flush_state flush_state;
    inferenceos_block_cache_error last_error;
    inferenceos_block_outcome last_block_outcome;
} inferenceos_block_cache_status;

typedef struct inferenceos_block_cache {
    inferenceos_block_cache_entry entries[INFERENCEOS_BLOCK_CACHE_CAPACITY];
    inferenceos_u64 next_replacement_age;
    inferenceos_block_cache_flush_state flush_state;
    inferenceos_block_cache_error last_error;
    inferenceos_block_outcome last_block_outcome;
    bool initialized;
} inferenceos_block_cache;

/* Initializes an empty cache without performing device I/O. */
inferenceos_result inferenceos_block_cache_initialize(
    inferenceos_block_cache *cache
);

/* Acquires and pins one sector. A miss reads the complete sector. Replacement
 * is deterministic and never selects a pinned entry. Dirty eviction writes
 * the victim before reuse and retains it on failure. */
inferenceos_block_cache_outcome inferenceos_block_cache_acquire(
    inferenceos_block_cache *cache,
    const inferenceos_block_device *device,
    inferenceos_u64 lba,
    inferenceos_block_cache_entry **entry
);

/* Marks a pinned entry dirty. Callers must mark any modified data before
 * releasing their pin. */
inferenceos_result inferenceos_block_cache_mark_dirty(
    inferenceos_block_cache *cache,
    inferenceos_block_cache_entry *entry
);

inferenceos_result inferenceos_block_cache_release(
    inferenceos_block_cache *cache,
    inferenceos_block_cache_entry *entry
);

/* Writes dirty entries for device in deterministic entry-index order. Only
 * after all writes succeed does it invoke the device flush. Failed writeback
 * entries remain dirty; failure never implies durability. */
inferenceos_block_cache_outcome inferenceos_block_cache_flush(
    inferenceos_block_cache *cache,
    const inferenceos_block_device *device
);

/* Invalidates only clean, unpinned entries belonging to device. */
inferenceos_result inferenceos_block_cache_invalidate(
    inferenceos_block_cache *cache,
    const inferenceos_block_device *device
);

inferenceos_result inferenceos_block_cache_query(
    const inferenceos_block_cache *cache,
    inferenceos_block_cache_status *status
);

static inline inferenceos_u8 *inferenceos_block_cache_entry_data(
    inferenceos_block_cache_entry *entry
)
{
    return entry == NULL ? NULL : entry->data;
}

static inline const inferenceos_u8 *inferenceos_block_cache_entry_const_data(
    const inferenceos_block_cache_entry *entry
)
{
    return entry == NULL ? NULL : entry->data;
}

static inline bool inferenceos_block_cache_outcome_is_success(
    inferenceos_block_cache_outcome outcome
)
{
    return inferenceos_result_is_success(outcome.result)
        && outcome.error == INFERENCEOS_BLOCK_CACHE_ERROR_NONE;
}

#endif
