#ifndef INFERENCEOS_HEAP_H
#define INFERENCEOS_HEAP_H

#include <inferenceos/base.h>
#include <inferenceos/result.h>

typedef struct inferenceos_heap {
    void *first_block;
    inferenceos_uptr arena_start;
    inferenceos_uptr arena_end;
    inferenceos_size total_bytes;
    bool initialized;
} inferenceos_heap;

typedef struct inferenceos_heap_statistics {
    inferenceos_size total_bytes;
    inferenceos_size allocated_bytes;
    inferenceos_size free_bytes;
    inferenceos_size allocated_blocks;
    inferenceos_size free_blocks;
} inferenceos_heap_statistics;

inferenceos_result inferenceos_heap_initialize(
    inferenceos_heap *heap,
    void *storage,
    inferenceos_size storage_size
);

void *inferenceos_heap_allocate(
    inferenceos_heap *heap,
    inferenceos_size size
);

void *inferenceos_heap_allocate_zeroed(
    inferenceos_heap *heap,
    inferenceos_size count,
    inferenceos_size size
);

inferenceos_result inferenceos_heap_free(
    inferenceos_heap *heap,
    void *allocation
);

inferenceos_result inferenceos_heap_query(
    const inferenceos_heap *heap,
    inferenceos_heap_statistics *statistics
);

typedef struct inferenceos_fixed_pool {
    inferenceos_u8 *storage;
    void *free_list;
    inferenceos_size object_size;
    inferenceos_size stride;
    inferenceos_size capacity;
    inferenceos_size free_count;
    bool initialized;
} inferenceos_fixed_pool;

inferenceos_result inferenceos_fixed_pool_initialize(
    inferenceos_fixed_pool *pool,
    void *storage,
    inferenceos_size storage_size,
    inferenceos_size object_size,
    inferenceos_size capacity
);

void *inferenceos_fixed_pool_allocate(inferenceos_fixed_pool *pool);

inferenceos_result inferenceos_fixed_pool_free(
    inferenceos_fixed_pool *pool,
    void *object
);

#endif
