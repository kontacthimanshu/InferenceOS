#include <inferenceos/heap.h>
#include <inferenceos/memory.h>

#define INFERENCEOS_HEAP_BLOCK_MAGIC UINT32_C(0x48454150)

typedef struct inferenceos_heap_block {
    inferenceos_size payload_size;
    struct inferenceos_heap_block *next;
    inferenceos_u32 magic;
    bool allocated;
} inferenceos_heap_block;

static inferenceos_size heap_alignment(void)
{
    return _Alignof(max_align_t);
}

static bool align_size(inferenceos_size value, inferenceos_size *aligned)
{
    return inferenceos_checked_align_up_size(value, heap_alignment(), aligned);
}

static inferenceos_size heap_header_size(void)
{
    inferenceos_size size = 0U;
    const bool aligned = align_size(sizeof(inferenceos_heap_block), &size);
    INFERENCEOS_ASSERT(aligned);
    return size;
}

static inferenceos_heap_block *heap_first(const inferenceos_heap *heap)
{
    return (inferenceos_heap_block *)heap->first_block;
}

static inferenceos_uptr block_payload_address(
    const inferenceos_heap_block *block
)
{
    return (inferenceos_uptr)block + heap_header_size();
}

static bool blocks_are_adjacent(
    const inferenceos_heap_block *left,
    const inferenceos_heap_block *right
)
{
    inferenceos_size span;
    inferenceos_uptr expected;

    if (!inferenceos_checked_add_size(
        heap_header_size(),
        left->payload_size,
        &span
    )) {
        return false;
    }
    if (span > UINTPTR_MAX - (inferenceos_uptr)left) {
        return false;
    }
    expected = (inferenceos_uptr)left + span;
    return expected == (inferenceos_uptr)right;
}

static void heap_coalesce(inferenceos_heap *heap)
{
    inferenceos_heap_block *block = heap_first(heap);

    while (block != NULL && block->next != NULL) {
        inferenceos_heap_block *next = block->next;

        if (!block->allocated && !next->allocated
            && blocks_are_adjacent(block, next)) {
            block->payload_size += heap_header_size() + next->payload_size;
            block->next = next->next;
            continue;
        }
        block = next;
    }
}

inferenceos_result inferenceos_heap_initialize(
    inferenceos_heap *heap,
    void *storage,
    inferenceos_size storage_size
)
{
    const inferenceos_size alignment = heap_alignment();
    const inferenceos_uptr raw_start = (inferenceos_uptr)storage;
    inferenceos_uptr aligned_start;
    inferenceos_size adjustment;
    inferenceos_size usable_size;
    inferenceos_heap_block *first;

    if (heap == NULL || storage == NULL || storage_size == 0U) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    if (raw_start > UINTPTR_MAX - (alignment - 1U)) {
        return INFERENCEOS_RESULT_OVERFLOW;
    }
    aligned_start = (raw_start + alignment - 1U) & ~(alignment - 1U);
    adjustment = (inferenceos_size)(aligned_start - raw_start);
    if (adjustment > storage_size) {
        return INFERENCEOS_RESULT_NO_SPACE;
    }
    usable_size = (storage_size - adjustment) & ~(alignment - 1U);
    if (usable_size <= heap_header_size()) {
        return INFERENCEOS_RESULT_NO_SPACE;
    }
    if (aligned_start > UINTPTR_MAX - usable_size) {
        return INFERENCEOS_RESULT_OVERFLOW;
    }

    first = (inferenceos_heap_block *)aligned_start;
    first->payload_size = usable_size - heap_header_size();
    first->next = NULL;
    first->magic = INFERENCEOS_HEAP_BLOCK_MAGIC;
    first->allocated = false;

    heap->first_block = first;
    heap->arena_start = aligned_start;
    heap->arena_end = aligned_start + usable_size;
    heap->total_bytes = usable_size;
    heap->initialized = true;
    return INFERENCEOS_RESULT_OK;
}

void *inferenceos_heap_allocate(
    inferenceos_heap *heap,
    inferenceos_size size
)
{
    inferenceos_size requested;
    inferenceos_heap_block *block;

    if (heap == NULL || !heap->initialized || size == 0U
        || !align_size(size, &requested)) {
        return NULL;
    }

    for (block = heap_first(heap); block != NULL; block = block->next) {
        inferenceos_size split_requirement;

        if (block->magic != INFERENCEOS_HEAP_BLOCK_MAGIC
            || block->allocated
            || block->payload_size < requested) {
            continue;
        }
        if (inferenceos_checked_add_size(
                requested,
                heap_header_size() + heap_alignment(),
                &split_requirement
            )
            && block->payload_size >= split_requirement) {
            inferenceos_heap_block *remainder = (inferenceos_heap_block *)(
                block_payload_address(block) + requested
            );
            remainder->payload_size = block->payload_size
                - requested
                - heap_header_size();
            remainder->next = block->next;
            remainder->magic = INFERENCEOS_HEAP_BLOCK_MAGIC;
            remainder->allocated = false;
            block->payload_size = requested;
            block->next = remainder;
        }
        block->allocated = true;
        return (void *)block_payload_address(block);
    }
    return NULL;
}

void *inferenceos_heap_allocate_zeroed(
    inferenceos_heap *heap,
    inferenceos_size count,
    inferenceos_size size
)
{
    inferenceos_size total;
    void *allocation;

    if (!inferenceos_checked_mul_size(count, size, &total) || total == 0U) {
        return NULL;
    }
    allocation = inferenceos_heap_allocate(heap, total);
    if (allocation != NULL) {
        (void)memset(allocation, 0, total);
    }
    return allocation;
}

inferenceos_result inferenceos_heap_free(
    inferenceos_heap *heap,
    void *allocation
)
{
    inferenceos_heap_block *block;
    const inferenceos_uptr address = (inferenceos_uptr)allocation;

    if (heap == NULL || !heap->initialized || allocation == NULL) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    if (address < heap->arena_start + heap_header_size()
        || address >= heap->arena_end) {
        return INFERENCEOS_RESULT_OUT_OF_RANGE;
    }

    for (block = heap_first(heap); block != NULL; block = block->next) {
        if (block_payload_address(block) != address) {
            continue;
        }
        if (block->magic != INFERENCEOS_HEAP_BLOCK_MAGIC) {
            return INFERENCEOS_RESULT_CORRUPT;
        }
        if (!block->allocated) {
            return INFERENCEOS_RESULT_INCONSISTENT;
        }
        block->allocated = false;
        heap_coalesce(heap);
        return INFERENCEOS_RESULT_OK;
    }
    return INFERENCEOS_RESULT_INVALID_ARGUMENT;
}

inferenceos_result inferenceos_heap_query(
    const inferenceos_heap *heap,
    inferenceos_heap_statistics *statistics
)
{
    inferenceos_heap_block *block;
    inferenceos_heap_statistics result = { .total_bytes = 0U };

    if (heap == NULL || !heap->initialized || statistics == NULL) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    result.total_bytes = heap->total_bytes;
    for (block = heap_first(heap); block != NULL; block = block->next) {
        if (block->magic != INFERENCEOS_HEAP_BLOCK_MAGIC) {
            return INFERENCEOS_RESULT_CORRUPT;
        }
        if (block->allocated) {
            result.allocated_bytes += block->payload_size;
            ++result.allocated_blocks;
        } else {
            result.free_bytes += block->payload_size;
            ++result.free_blocks;
        }
    }
    *statistics = result;
    return INFERENCEOS_RESULT_OK;
}

inferenceos_result inferenceos_fixed_pool_initialize(
    inferenceos_fixed_pool *pool,
    void *storage,
    inferenceos_size storage_size,
    inferenceos_size object_size,
    inferenceos_size capacity
)
{
    const inferenceos_size alignment = heap_alignment();
    const inferenceos_uptr raw_start = (inferenceos_uptr)storage;
    inferenceos_uptr aligned_start;
    inferenceos_size adjustment;
    inferenceos_size minimum_size;
    inferenceos_size stride;
    inferenceos_size required_size;

    if (pool == NULL || storage == NULL || object_size == 0U || capacity == 0U) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    minimum_size = object_size < sizeof(void *) ? sizeof(void *) : object_size;
    if (!align_size(minimum_size, &stride)
        || raw_start > UINTPTR_MAX - (alignment - 1U)) {
        return INFERENCEOS_RESULT_OVERFLOW;
    }
    aligned_start = (raw_start + alignment - 1U) & ~(alignment - 1U);
    adjustment = (inferenceos_size)(aligned_start - raw_start);
    if (!inferenceos_checked_mul_size(stride, capacity, &required_size)
        || required_size > UINTPTR_MAX - aligned_start) {
        return INFERENCEOS_RESULT_OVERFLOW;
    }
    if (adjustment > storage_size
        || required_size > storage_size - adjustment) {
        return INFERENCEOS_RESULT_NO_SPACE;
    }

    pool->storage = (inferenceos_u8 *)aligned_start;
    pool->free_list = NULL;
    pool->object_size = object_size;
    pool->stride = stride;
    pool->capacity = capacity;
    pool->free_count = capacity;
    pool->initialized = true;

    for (inferenceos_size index = capacity; index > 0U; --index) {
        void **slot = (void **)(pool->storage + (index - 1U) * stride);
        *slot = pool->free_list;
        pool->free_list = slot;
    }
    return INFERENCEOS_RESULT_OK;
}

void *inferenceos_fixed_pool_allocate(inferenceos_fixed_pool *pool)
{
    void *object;

    if (pool == NULL || !pool->initialized || pool->free_list == NULL) {
        return NULL;
    }
    object = pool->free_list;
    pool->free_list = *(void **)object;
    --pool->free_count;
    return object;
}

inferenceos_result inferenceos_fixed_pool_free(
    inferenceos_fixed_pool *pool,
    void *object
)
{
    inferenceos_uptr address;
    inferenceos_uptr start;
    inferenceos_size storage_size;
    void *free_object;

    if (pool == NULL || !pool->initialized || object == NULL) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    address = (inferenceos_uptr)object;
    start = (inferenceos_uptr)pool->storage;
    if (!inferenceos_checked_mul_size(pool->stride, pool->capacity, &storage_size)
        || storage_size > UINTPTR_MAX - start
        || address < start
        || address >= start + storage_size
        || (address - start) % pool->stride != 0U) {
        return INFERENCEOS_RESULT_OUT_OF_RANGE;
    }
    free_object = pool->free_list;
    for (inferenceos_size index = 0U;
         index < pool->free_count && free_object != NULL;
         ++index) {
        if (free_object == object) {
            return INFERENCEOS_RESULT_INCONSISTENT;
        }
        free_object = *(void **)free_object;
    }
    *(void **)object = pool->free_list;
    pool->free_list = object;
    ++pool->free_count;
    return INFERENCEOS_RESULT_OK;
}
