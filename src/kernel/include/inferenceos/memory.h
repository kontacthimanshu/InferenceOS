#ifndef INFERENCEOS_MEMORY_H
#define INFERENCEOS_MEMORY_H

#include <inferenceos/base.h>
#include <inferenceos/errors.h>

enum {
    IOS_PAGE_SIZE = 4096,
    IOS_MEMORY_MAP_MAX_REGIONS = 256
};

enum ios_physical_memory_kind {
    IOS_PHYSICAL_RESERVED = 0,
    IOS_PHYSICAL_USABLE = 1,
    IOS_PHYSICAL_RECLAIMABLE = 2
};

struct ios_physical_memory_region {
    ios_uptr base;
    ios_u64 page_count;
    enum ios_physical_memory_kind kind;
};

struct ios_physical_reservation {
    ios_uptr base;
    ios_u64 byte_count;
};

struct ios_physical_memory_statistics {
    ios_u64 total_pages;
    ios_u64 free_pages;
    ios_u64 allocated_pages;
    ios_u64 unavailable_pages;
    ios_uptr metadata_base;
    ios_u64 metadata_pages;
};

ios_status physical_memory_initialize(
    const struct ios_physical_memory_region *regions,
    ios_size region_count,
    const struct ios_physical_reservation *reservations,
    ios_size reservation_count
);
bool physical_memory_is_initialized(void);
ios_status physical_allocate_pages(
    ios_u64 page_count,
    ios_u64 alignment_pages,
    ios_uptr *physical_address
);
ios_status physical_free_pages(ios_uptr physical_address, ios_u64 page_count);
ios_status physical_reclaim_pages(ios_uptr physical_address, ios_u64 page_count);
bool physical_page_is_allocated(ios_uptr physical_address);
void physical_memory_statistics(struct ios_physical_memory_statistics *statistics);

enum ios_virtual_memory_flags {
    IOS_VM_WRITE = UINT32_C(1) << 0,
    IOS_VM_EXECUTE = UINT32_C(1) << 1,
    IOS_VM_USER = UINT32_C(1) << 2,
    IOS_VM_GLOBAL = UINT32_C(1) << 3,
    IOS_VM_OWNED = UINT32_C(1) << 4
};

struct ios_address_space {
    ios_uptr root_address;
};

ios_status virtual_memory_initialize(void);
bool virtual_address_is_canonical(ios_uptr address);
bool virtual_user_range_is_valid(ios_uptr address, ios_u64 byte_count);
ios_status virtual_address_space_create(struct ios_address_space *address_space);
void virtual_address_space_destroy(struct ios_address_space *address_space);
void virtual_address_space_activate(const struct ios_address_space *address_space);
ios_status virtual_map_pages(
    struct ios_address_space *address_space,
    ios_uptr virtual_address,
    ios_uptr physical_address,
    ios_u64 page_count,
    ios_u32 flags
);
ios_status virtual_unmap_pages(
    struct ios_address_space *address_space,
    ios_uptr virtual_address,
    ios_u64 page_count
);
ios_status virtual_translate(
    const struct ios_address_space *address_space,
    ios_uptr virtual_address,
    ios_uptr *physical_address
);
ios_status virtual_query(
    const struct ios_address_space *address_space,
    ios_uptr virtual_address,
    ios_uptr *physical_address,
    ios_u32 *flags
);

#endif
