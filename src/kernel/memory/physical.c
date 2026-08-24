#include <inferenceos/memory.h>

#include <inferenceos/arch/interrupts.h>
#include <inferenceos/runtime.h>

enum page_state {
    PAGE_UNAVAILABLE = 0,
    PAGE_FREE = 1,
    PAGE_ALLOCATED = 2,
    PAGE_RECLAIMABLE = 3
};

#define X86_64_PHYSICAL_ADDRESS_LIMIT UINT64_C(0x0010000000000000)

static ios_u8 *page_states;
static ios_u64 managed_page_count;
static ios_u64 free_page_count;
static ios_u64 allocated_page_count;
static ios_uptr metadata_physical_base;
static ios_u64 metadata_page_count;
static ios_u64 search_hint;
static bool allocator_initialized;

static bool add_overflows(ios_u64 left, ios_u64 right, ios_u64 *result)
{
    if (right > UINT64_MAX - left) {
        return true;
    }
    *result = left + right;
    return false;
}

static bool multiply_overflows(ios_u64 left, ios_u64 right, ios_u64 *result)
{
    if (left != 0 && right > UINT64_MAX / left) {
        return true;
    }
    *result = left * right;
    return false;
}

static bool ranges_overlap(ios_u64 left_base, ios_u64 left_size, ios_u64 right_base, ios_u64 right_size)
{
    ios_u64 left_end;
    ios_u64 right_end;

    if (left_size == 0 || right_size == 0) {
        return false;
    }
    if (add_overflows(left_base, left_size, &left_end)
        || add_overflows(right_base, right_size, &right_end)) {
        return true;
    }
    return left_base < right_end && right_base < left_end;
}

static enum page_state get_page_state(ios_u64 page)
{
    const ios_u64 byte_index = page / 4U;
    const ios_u32 shift = (ios_u32)((page % 4U) * 2U);

    return (enum page_state)((page_states[byte_index] >> shift) & 0x03U);
}

static void set_page_state(ios_u64 page, enum page_state state)
{
    const ios_u64 byte_index = page / 4U;
    const ios_u32 shift = (ios_u32)((page % 4U) * 2U);
    const ios_u8 mask = (ios_u8)(0x03U << shift);

    page_states[byte_index] = (ios_u8)((page_states[byte_index] & (ios_u8)~mask)
        | ((ios_u8)state << shift));
}

static void mark_range(ios_u64 first_page, ios_u64 page_count, enum page_state state)
{
    ios_u64 end_page = first_page + page_count;

    if (end_page > managed_page_count) {
        end_page = managed_page_count;
    }
    for (ios_u64 page = first_page; page < end_page; ++page) {
        set_page_state(page, state);
    }
}

static bool reservation_is_valid(const struct ios_physical_reservation *reservation)
{
    ios_u64 end;

    return reservation->byte_count == 0
        || (!add_overflows(reservation->base, reservation->byte_count, &end)
            && end <= X86_64_PHYSICAL_ADDRESS_LIMIT);
}

static bool candidate_overlaps_reservation(
    ios_u64 candidate,
    ios_u64 byte_count,
    const struct ios_physical_reservation *reservations,
    ios_size reservation_count,
    ios_u64 *next_candidate
)
{
    for (ios_size index = 0; index < reservation_count; ++index) {
        const struct ios_physical_reservation *reservation = &reservations[index];

        if (ranges_overlap(candidate, byte_count, reservation->base, reservation->byte_count)) {
            const ios_u64 reservation_end = reservation->base + reservation->byte_count;
            *next_candidate = (reservation_end + (IOS_PAGE_SIZE - 1U))
                & ~(ios_u64)(IOS_PAGE_SIZE - 1U);
            return true;
        }
    }
    return false;
}

static bool find_metadata_space(
    const struct ios_physical_memory_region *regions,
    ios_size region_count,
    const struct ios_physical_reservation *reservations,
    ios_size reservation_count,
    ios_u64 byte_count,
    ios_uptr *base
)
{
    for (ios_size index = 0; index < region_count; ++index) {
        const struct ios_physical_memory_region *region = &regions[index];
        ios_u64 region_bytes;
        ios_u64 region_end;
        ios_u64 candidate;

        if (region->kind != IOS_PHYSICAL_USABLE
            || multiply_overflows(region->page_count, IOS_PAGE_SIZE, &region_bytes)
            || add_overflows(region->base, region_bytes, &region_end)) {
            continue;
        }

        candidate = region->base;
        while (candidate <= region_end && byte_count <= region_end - candidate) {
            ios_u64 next_candidate;

            if (!candidate_overlaps_reservation(
                    candidate,
                    byte_count,
                    reservations,
                    reservation_count,
                    &next_candidate)) {
                *base = (ios_uptr)candidate;
                return true;
            }
            if (next_candidate <= candidate) {
                break;
            }
            candidate = next_candidate;
        }
    }
    return false;
}

ios_status physical_memory_initialize(
    const struct ios_physical_memory_region *regions,
    ios_size region_count,
    const struct ios_physical_reservation *reservations,
    ios_size reservation_count
)
{
    ios_u64 maximum_usable_end = 0;
    ios_u64 state_bytes;
    ios_u64 metadata_bytes;

    if (allocator_initialized) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    if (regions == NULL || region_count == 0 || region_count > IOS_MEMORY_MAP_MAX_REGIONS
        || reservation_count > IOS_MEMORY_MAP_MAX_REGIONS
        || (reservation_count != 0 && reservations == NULL)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }

    for (ios_size index = 0; index < region_count; ++index) {
        ios_u64 region_bytes;
        ios_u64 region_end;

        if ((regions[index].base & (IOS_PAGE_SIZE - 1U)) != 0
            || regions[index].page_count == 0
            || multiply_overflows(regions[index].page_count, IOS_PAGE_SIZE, &region_bytes)
            || add_overflows(regions[index].base, region_bytes, &region_end)
            || region_end > X86_64_PHYSICAL_ADDRESS_LIMIT) {
            return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
        }
        if (regions[index].kind != IOS_PHYSICAL_RESERVED
            && regions[index].kind != IOS_PHYSICAL_USABLE
            && regions[index].kind != IOS_PHYSICAL_RECLAIMABLE) {
            return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
        }
        if (regions[index].kind != IOS_PHYSICAL_RESERVED && region_end > maximum_usable_end) {
            maximum_usable_end = region_end;
        }
        for (ios_size other = index + 1; other < region_count; ++other) {
            ios_u64 other_bytes;

            if (multiply_overflows(regions[other].page_count, IOS_PAGE_SIZE, &other_bytes)
                || ranges_overlap(regions[index].base, region_bytes, regions[other].base, other_bytes)) {
                return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
            }
        }
    }
    for (ios_size index = 0; index < reservation_count; ++index) {
        if (!reservation_is_valid(&reservations[index])) {
            return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
        }
    }
    if (maximum_usable_end == 0) {
        return IOS_ERROR(IOS_E_NO_MEMORY);
    }

    managed_page_count = maximum_usable_end / IOS_PAGE_SIZE;
    state_bytes = (managed_page_count + 3U) / 4U;
    metadata_page_count = (state_bytes + (IOS_PAGE_SIZE - 1U)) / IOS_PAGE_SIZE;
    metadata_bytes = metadata_page_count * IOS_PAGE_SIZE;
    if (!find_metadata_space(
            regions,
            region_count,
            reservations,
            reservation_count,
            metadata_bytes,
            &metadata_physical_base)) {
        return IOS_ERROR(IOS_E_NO_MEMORY);
    }

    page_states = (ios_u8 *)metadata_physical_base;
    memset(page_states, 0, (ios_size)metadata_bytes);
    for (ios_size index = 0; index < region_count; ++index) {
        if (regions[index].kind == IOS_PHYSICAL_USABLE) {
            mark_range(regions[index].base / IOS_PAGE_SIZE, regions[index].page_count, PAGE_FREE);
        } else if (regions[index].kind == IOS_PHYSICAL_RECLAIMABLE) {
            mark_range(
                regions[index].base / IOS_PAGE_SIZE,
                regions[index].page_count,
                PAGE_RECLAIMABLE
            );
        }
    }
    for (ios_size index = 0; index < reservation_count; ++index) {
        const ios_u64 first = reservations[index].base / IOS_PAGE_SIZE;
        ios_u64 end = reservations[index].base + reservations[index].byte_count;

        if ((end & (IOS_PAGE_SIZE - 1U)) != 0) {
            end = (end + (IOS_PAGE_SIZE - 1U)) & ~(ios_u64)(IOS_PAGE_SIZE - 1U);
        }
        mark_range(first, (end / IOS_PAGE_SIZE) - first, PAGE_UNAVAILABLE);
    }
    mark_range(0, 1, PAGE_UNAVAILABLE);
    mark_range(
        metadata_physical_base / IOS_PAGE_SIZE,
        metadata_page_count,
        PAGE_UNAVAILABLE
    );

    free_page_count = 0;
    for (ios_u64 page = 0; page < managed_page_count; ++page) {
        if (get_page_state(page) == PAGE_FREE) {
            ++free_page_count;
        }
    }
    allocated_page_count = 0;
    search_hint = 1;
    allocator_initialized = true;
    return IOS_OK;
}

bool physical_memory_is_initialized(void)
{
    return allocator_initialized;
}

ios_status physical_allocate_pages(
    ios_u64 page_count,
    ios_u64 alignment_pages,
    ios_uptr *physical_address
)
{
    const ios_u64 previous_flags = x86_64_interrupt_save_disable();

    if (!allocator_initialized || physical_address == NULL || page_count == 0
        || alignment_pages == 0 || (alignment_pages & (alignment_pages - 1U)) != 0) {
        x86_64_interrupt_restore(previous_flags);
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (page_count > free_page_count) {
        x86_64_interrupt_restore(previous_flags);
        return IOS_ERROR(IOS_E_NO_MEMORY);
    }

    for (ios_u32 pass = 0; pass < 2U; ++pass) {
        ios_u64 page = pass == 0U ? search_hint : 1U;
        const ios_u64 limit = pass == 0U ? managed_page_count : search_hint;

        while (page < limit) {
            ios_u64 available = 0;

            page = (page + alignment_pages - 1U) & ~(alignment_pages - 1U);
            if (page >= limit || page_count > limit - page) {
                break;
            }
            while (available < page_count && get_page_state(page + available) == PAGE_FREE) {
                ++available;
            }
            if (available == page_count) {
                for (ios_u64 offset = 0; offset < page_count; ++offset) {
                    set_page_state(page + offset, PAGE_ALLOCATED);
                }
                free_page_count -= page_count;
                allocated_page_count += page_count;
                search_hint = page + page_count;
                if (search_hint >= managed_page_count) {
                    search_hint = 1;
                }
                *physical_address = (ios_uptr)(page * IOS_PAGE_SIZE);
                x86_64_interrupt_restore(previous_flags);
                return IOS_OK;
            }
            page += available + 1U;
        }
    }

    search_hint = 1;
    x86_64_interrupt_restore(previous_flags);
    return IOS_ERROR(IOS_E_NO_MEMORY);
}

ios_status physical_free_pages(ios_uptr physical_address, ios_u64 page_count)
{
    const ios_u64 first_page = physical_address / IOS_PAGE_SIZE;
    const ios_u64 previous_flags = x86_64_interrupt_save_disable();

    if (!allocator_initialized || page_count == 0
        || (physical_address & (IOS_PAGE_SIZE - 1U)) != 0
        || first_page >= managed_page_count
        || page_count > managed_page_count - first_page) {
        x86_64_interrupt_restore(previous_flags);
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    for (ios_u64 offset = 0; offset < page_count; ++offset) {
        if (get_page_state(first_page + offset) != PAGE_ALLOCATED) {
            x86_64_interrupt_restore(previous_flags);
            return IOS_ERROR(IOS_E_INVALID_STATE);
        }
    }
    for (ios_u64 offset = 0; offset < page_count; ++offset) {
        set_page_state(first_page + offset, PAGE_FREE);
    }
    free_page_count += page_count;
    allocated_page_count -= page_count;
    if (first_page < search_hint) {
        search_hint = first_page;
    }
    x86_64_interrupt_restore(previous_flags);
    return IOS_OK;
}

ios_status physical_reclaim_pages(ios_uptr physical_address, ios_u64 page_count)
{
    const ios_u64 first_page = physical_address / IOS_PAGE_SIZE;
    const ios_u64 previous_flags = x86_64_interrupt_save_disable();

    if (!allocator_initialized || page_count == 0
        || (physical_address & (IOS_PAGE_SIZE - 1U)) != 0
        || first_page >= managed_page_count
        || page_count > managed_page_count - first_page) {
        x86_64_interrupt_restore(previous_flags);
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    for (ios_u64 offset = 0; offset < page_count; ++offset) {
        if (get_page_state(first_page + offset) != PAGE_RECLAIMABLE) {
            x86_64_interrupt_restore(previous_flags);
            return IOS_ERROR(IOS_E_INVALID_STATE);
        }
    }
    for (ios_u64 offset = 0; offset < page_count; ++offset) {
        set_page_state(first_page + offset, PAGE_FREE);
    }
    free_page_count += page_count;
    if (first_page < search_hint) {
        search_hint = first_page;
    }
    x86_64_interrupt_restore(previous_flags);
    return IOS_OK;
}

bool physical_page_is_allocated(ios_uptr physical_address)
{
    const ios_u64 page = physical_address / IOS_PAGE_SIZE;
    const ios_u64 previous_flags = x86_64_interrupt_save_disable();
    const bool allocated = allocator_initialized
        && (physical_address & (IOS_PAGE_SIZE - 1U)) == 0
        && page < managed_page_count
        && get_page_state(page) == PAGE_ALLOCATED;

    x86_64_interrupt_restore(previous_flags);
    return allocated;
}

void physical_memory_statistics(struct ios_physical_memory_statistics *statistics)
{
    const ios_u64 previous_flags = x86_64_interrupt_save_disable();

    IOS_ASSERT(statistics != NULL);
    statistics->total_pages = managed_page_count;
    statistics->free_pages = free_page_count;
    statistics->allocated_pages = allocated_page_count;
    statistics->unavailable_pages = managed_page_count - free_page_count - allocated_page_count;
    statistics->metadata_base = metadata_physical_base;
    statistics->metadata_pages = metadata_page_count;
    x86_64_interrupt_restore(previous_flags);
}
