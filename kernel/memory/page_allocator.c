#include <inferenceos/memory.h>
#include <inferenceos/page_allocator.h>

static bool page_is_allocated(
    const inferenceos_page_allocator *allocator,
    inferenceos_u64 page_index
)
{
    const inferenceos_size byte_index = (inferenceos_size)(page_index / 8U);
    const inferenceos_u8 mask = (inferenceos_u8)(1U << (page_index % 8U));
    return (allocator->allocation_bitmap[byte_index] & mask) != 0U;
}

static void set_page_allocation(
    inferenceos_page_allocator *allocator,
    inferenceos_u64 page_index,
    bool allocated
)
{
    const inferenceos_size byte_index = (inferenceos_size)(page_index / 8U);
    const inferenceos_u8 mask = (inferenceos_u8)(1U << (page_index % 8U));

    if (allocated) {
        allocator->allocation_bitmap[byte_index] |= mask;
    } else {
        allocator->allocation_bitmap[byte_index] &= (inferenceos_u8)~mask;
    }
}

static inferenceos_result page_range(
    const inferenceos_page_allocator *allocator,
    inferenceos_u64 address,
    inferenceos_u64 page_count,
    inferenceos_u64 *first_page
)
{
    inferenceos_u64 byte_count;
    inferenceos_u64 end_address;
    inferenceos_u64 allocator_size;
    inferenceos_u64 allocator_end;

    if (allocator == NULL || !allocator->initialized || first_page == NULL) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    if (page_count == 0U || address % INFERENCEOS_PAGE_SIZE != 0U) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    if (!inferenceos_checked_mul_u64(page_count, INFERENCEOS_PAGE_SIZE, &byte_count)
        || !inferenceos_checked_add_u64(address, byte_count, &end_address)
        || !inferenceos_checked_mul_u64(
            allocator->page_count,
            INFERENCEOS_PAGE_SIZE,
            &allocator_size
        )
        || !inferenceos_checked_add_u64(
            allocator->base_address,
            allocator_size,
            &allocator_end
        )) {
        return INFERENCEOS_RESULT_OVERFLOW;
    }
    if (address < allocator->base_address || end_address > allocator_end) {
        return INFERENCEOS_RESULT_OUT_OF_RANGE;
    }
    *first_page = (address - allocator->base_address) / INFERENCEOS_PAGE_SIZE;
    return INFERENCEOS_RESULT_OK;
}

inferenceos_result inferenceos_page_allocator_bitmap_size(
    inferenceos_u64 page_count,
    inferenceos_size *bitmap_size
)
{
    inferenceos_u64 bytes;

    if (bitmap_size == NULL || page_count == 0U) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    if (!inferenceos_checked_add_u64(page_count, 7U, &bytes)) {
        return INFERENCEOS_RESULT_OVERFLOW;
    }
    bytes /= 8U;
    if (bytes > SIZE_MAX) {
        return INFERENCEOS_RESULT_OVERFLOW;
    }
    *bitmap_size = (inferenceos_size)bytes;
    return INFERENCEOS_RESULT_OK;
}

inferenceos_result inferenceos_page_allocator_initialize(
    inferenceos_page_allocator *allocator,
    inferenceos_u64 base_address,
    inferenceos_u64 byte_length,
    void *bitmap_storage,
    inferenceos_size bitmap_storage_size
)
{
    inferenceos_u64 page_count;
    inferenceos_size required_bitmap_size;
    inferenceos_result result;

    if (allocator == NULL || bitmap_storage == NULL
        || base_address % INFERENCEOS_PAGE_SIZE != 0U
        || byte_length == 0U
        || byte_length % INFERENCEOS_PAGE_SIZE != 0U) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    if (byte_length > UINT64_MAX - base_address) {
        return INFERENCEOS_RESULT_OVERFLOW;
    }
    page_count = byte_length / INFERENCEOS_PAGE_SIZE;
    result = inferenceos_page_allocator_bitmap_size(
        page_count,
        &required_bitmap_size
    );
    if (!inferenceos_result_is_success(result)) {
        return result;
    }
    if (bitmap_storage_size < required_bitmap_size) {
        return INFERENCEOS_RESULT_NO_SPACE;
    }

    (void)memset(bitmap_storage, 0, required_bitmap_size);
    allocator->base_address = base_address;
    allocator->page_count = page_count;
    allocator->free_page_count = page_count;
    allocator->allocation_bitmap = bitmap_storage;
    allocator->allocation_bitmap_size = required_bitmap_size;
    allocator->initialized = true;
    return INFERENCEOS_RESULT_OK;
}

inferenceos_result inferenceos_page_allocator_reserve(
    inferenceos_page_allocator *allocator,
    inferenceos_u64 address,
    inferenceos_u64 page_count
)
{
    inferenceos_u64 first_page;
    inferenceos_result result = page_range(
        allocator,
        address,
        page_count,
        &first_page
    );

    if (!inferenceos_result_is_success(result)) {
        return result;
    }
    for (inferenceos_u64 index = 0U; index < page_count; ++index) {
        if (page_is_allocated(allocator, first_page + index)) {
            return INFERENCEOS_RESULT_ALREADY_EXISTS;
        }
    }
    for (inferenceos_u64 index = 0U; index < page_count; ++index) {
        set_page_allocation(allocator, first_page + index, true);
    }
    allocator->free_page_count -= page_count;
    return INFERENCEOS_RESULT_OK;
}

inferenceos_result inferenceos_page_allocator_allocate(
    inferenceos_page_allocator *allocator,
    inferenceos_u64 page_count,
    inferenceos_u64 alignment_pages,
    inferenceos_u64 *address
)
{
    if (allocator == NULL || !allocator->initialized || address == NULL
        || page_count == 0U
        || !inferenceos_is_power_of_two_size((inferenceos_size)alignment_pages)) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    if (page_count > allocator->free_page_count
        || page_count > allocator->page_count) {
        return INFERENCEOS_RESULT_NO_SPACE;
    }

    for (inferenceos_u64 first = 0U;
         first <= allocator->page_count - page_count;
         ++first) {
        inferenceos_u64 physical_page;
        bool available = true;

        physical_page = allocator->base_address / INFERENCEOS_PAGE_SIZE + first;
        if (physical_page % alignment_pages != 0U) {
            continue;
        }
        for (inferenceos_u64 index = 0U; index < page_count; ++index) {
            if (page_is_allocated(allocator, first + index)) {
                available = false;
                first += index;
                break;
            }
        }
        if (!available) {
            continue;
        }
        for (inferenceos_u64 index = 0U; index < page_count; ++index) {
            set_page_allocation(allocator, first + index, true);
        }
        allocator->free_page_count -= page_count;
        *address = allocator->base_address + first * INFERENCEOS_PAGE_SIZE;
        return INFERENCEOS_RESULT_OK;
    }
    return INFERENCEOS_RESULT_NO_SPACE;
}

inferenceos_result inferenceos_page_allocator_free(
    inferenceos_page_allocator *allocator,
    inferenceos_u64 address,
    inferenceos_u64 page_count
)
{
    inferenceos_u64 first_page;
    inferenceos_result result = page_range(
        allocator,
        address,
        page_count,
        &first_page
    );

    if (!inferenceos_result_is_success(result)) {
        return result;
    }
    for (inferenceos_u64 index = 0U; index < page_count; ++index) {
        if (!page_is_allocated(allocator, first_page + index)) {
            return INFERENCEOS_RESULT_INCONSISTENT;
        }
    }
    for (inferenceos_u64 index = 0U; index < page_count; ++index) {
        set_page_allocation(allocator, first_page + index, false);
    }
    allocator->free_page_count += page_count;
    return INFERENCEOS_RESULT_OK;
}
