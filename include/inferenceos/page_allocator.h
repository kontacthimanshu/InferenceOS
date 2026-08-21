#ifndef INFERENCEOS_PAGE_ALLOCATOR_H
#define INFERENCEOS_PAGE_ALLOCATOR_H

#include <inferenceos/base.h>
#include <inferenceos/result.h>

#define INFERENCEOS_PAGE_SIZE UINT64_C(4096)

typedef struct inferenceos_page_allocator {
    inferenceos_u64 base_address;
    inferenceos_u64 page_count;
    inferenceos_u64 free_page_count;
    inferenceos_u8 *allocation_bitmap;
    inferenceos_size allocation_bitmap_size;
    bool initialized;
} inferenceos_page_allocator;

inferenceos_result inferenceos_page_allocator_bitmap_size(
    inferenceos_u64 page_count,
    inferenceos_size *bitmap_size
);

inferenceos_result inferenceos_page_allocator_initialize(
    inferenceos_page_allocator *allocator,
    inferenceos_u64 base_address,
    inferenceos_u64 byte_length,
    void *bitmap_storage,
    inferenceos_size bitmap_storage_size
);

inferenceos_result inferenceos_page_allocator_reserve(
    inferenceos_page_allocator *allocator,
    inferenceos_u64 address,
    inferenceos_u64 page_count
);

inferenceos_result inferenceos_page_allocator_allocate(
    inferenceos_page_allocator *allocator,
    inferenceos_u64 page_count,
    inferenceos_u64 alignment_pages,
    inferenceos_u64 *address
);

inferenceos_result inferenceos_page_allocator_free(
    inferenceos_page_allocator *allocator,
    inferenceos_u64 address,
    inferenceos_u64 page_count
);

#endif
