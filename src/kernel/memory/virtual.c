#include <inferenceos/memory.h>

#include <inferenceos/arch/paging.h>
#include <inferenceos/runtime.h>

enum {
    PAGE_TABLE_ENTRY_COUNT = 512
};

#define PTE_PRESENT UINT64_C(0x001)
#define PTE_WRITABLE UINT64_C(0x002)
#define PTE_USER UINT64_C(0x004)
#define PTE_LARGE UINT64_C(0x080)
#define PTE_GLOBAL UINT64_C(0x100)
#define PTE_OWNED UINT64_C(0x200)
#define PTE_ADDRESS_MASK UINT64_C(0x000ffffffffff000)
#define PTE_2M_ADDRESS_MASK UINT64_C(0x000fffffffe00000)
#define PTE_1G_ADDRESS_MASK UINT64_C(0x000fffffc0000000)
#define PTE_NO_EXECUTE (UINT64_C(1) << 63)
#define USER_VIRTUAL_START UINT64_C(0x0000010000000000)
#define USER_VIRTUAL_END UINT64_C(0x0000800000000000)

static bool execute_disable_enabled;
static ios_uptr kernel_root_address;

static ios_u16 pml4_index(ios_uptr address)
{
    return (ios_u16)((address >> 39) & UINT64_C(0x1ff));
}

static ios_u16 pdpt_index(ios_uptr address)
{
    return (ios_u16)((address >> 30) & UINT64_C(0x1ff));
}

static ios_u16 pd_index(ios_uptr address)
{
    return (ios_u16)((address >> 21) & UINT64_C(0x1ff));
}

static ios_u16 pt_index(ios_uptr address)
{
    return (ios_u16)((address >> 12) & UINT64_C(0x1ff));
}

static ios_u64 *table_from_entry(ios_u64 entry)
{
    return (ios_u64 *)(ios_uptr)(entry & PTE_ADDRESS_MASK);
}

static bool table_is_empty(const ios_u64 *table)
{
    for (ios_u16 index = 0; index < PAGE_TABLE_ENTRY_COUNT; ++index) {
        if (table[index] != 0) {
            return false;
        }
    }
    return true;
}

static ios_status ensure_next_table(
    ios_u64 *entry,
    bool user,
    ios_u64 **table,
    bool *created
)
{
    ios_uptr physical_address;
    ios_status status;

    if ((*entry & PTE_PRESENT) != 0) {
        if ((*entry & PTE_LARGE) != 0) {
            return IOS_ERROR(IOS_E_INVALID_STATE);
        }
        if (user) {
            *entry |= PTE_USER | PTE_WRITABLE;
        }
        *table = table_from_entry(*entry);
        *created = false;
        return IOS_OK;
    }

    status = physical_allocate_pages(1, 1, &physical_address);
    if (IOS_FAILED(status)) {
        return status;
    }
    *table = (ios_u64 *)physical_address;
    memset(*table, 0, IOS_PAGE_SIZE);
    *entry = physical_address | PTE_PRESENT | PTE_WRITABLE | (user ? PTE_USER : 0);
    *created = true;
    return IOS_OK;
}

static ios_status locate_leaf(
    const struct ios_address_space *address_space,
    ios_uptr virtual_address,
    ios_u64 **leaf
)
{
    ios_u64 *pml4 = (ios_u64 *)address_space->root_address;
    ios_u64 pml4e = pml4[pml4_index(virtual_address)];
    ios_u64 *pdpt;
    ios_u64 pdpte;
    ios_u64 *pd;
    ios_u64 pde;
    ios_u64 *pt;

    if ((pml4e & PTE_PRESENT) == 0 || (pml4e & PTE_LARGE) != 0) {
        return IOS_ERROR(IOS_E_NOT_FOUND);
    }
    pdpt = table_from_entry(pml4e);
    pdpte = pdpt[pdpt_index(virtual_address)];
    if ((pdpte & PTE_PRESENT) == 0 || (pdpte & PTE_LARGE) != 0) {
        return IOS_ERROR(IOS_E_NOT_FOUND);
    }
    pd = table_from_entry(pdpte);
    pde = pd[pd_index(virtual_address)];
    if ((pde & PTE_PRESENT) == 0 || (pde & PTE_LARGE) != 0) {
        return IOS_ERROR(IOS_E_NOT_FOUND);
    }
    pt = table_from_entry(pde);
    *leaf = &pt[pt_index(virtual_address)];
    return IOS_OK;
}

static ios_status map_one(
    struct ios_address_space *address_space,
    ios_uptr virtual_address,
    ios_uptr physical_address,
    ios_u32 flags
)
{
    const bool user = (flags & IOS_VM_USER) != 0;
    ios_u64 *pml4 = (ios_u64 *)address_space->root_address;
    ios_u64 *pdpt;
    ios_u64 *pd;
    ios_u64 *pt;
    ios_u64 *leaf;
    ios_u64 *pml4e = &pml4[pml4_index(virtual_address)];
    ios_u64 *pdpte;
    ios_u64 *pde;
    bool created_pdpt;
    bool created_pd;
    bool created_pt;
    ios_status status;
    ios_u64 entry_flags = PTE_PRESENT;

    status = ensure_next_table(pml4e, user, &pdpt, &created_pdpt);
    if (IOS_FAILED(status)) {
        return status;
    }
    pdpte = &pdpt[pdpt_index(virtual_address)];
    status = ensure_next_table(pdpte, user, &pd, &created_pd);
    if (IOS_FAILED(status)) {
        if (created_pdpt) {
            *pml4e = 0;
            (void)physical_free_pages((ios_uptr)pdpt, 1);
        }
        return status;
    }
    pde = &pd[pd_index(virtual_address)];
    status = ensure_next_table(pde, user, &pt, &created_pt);
    if (IOS_FAILED(status)) {
        if (created_pd) {
            *pdpte = 0;
            (void)physical_free_pages((ios_uptr)pd, 1);
        }
        if (created_pdpt) {
            *pml4e = 0;
            (void)physical_free_pages((ios_uptr)pdpt, 1);
        }
        return status;
    }
    leaf = &pt[pt_index(virtual_address)];
    if ((*leaf & PTE_PRESENT) != 0) {
        if (created_pt) {
            *pde = 0;
            (void)physical_free_pages((ios_uptr)pt, 1);
        }
        if (created_pd) {
            *pdpte = 0;
            (void)physical_free_pages((ios_uptr)pd, 1);
        }
        if (created_pdpt) {
            *pml4e = 0;
            (void)physical_free_pages((ios_uptr)pdpt, 1);
        }
        return IOS_ERROR(IOS_E_ALREADY_EXISTS);
    }

    if ((flags & IOS_VM_WRITE) != 0) {
        entry_flags |= PTE_WRITABLE;
    }
    if (user) {
        entry_flags |= PTE_USER;
    }
    if ((flags & IOS_VM_GLOBAL) != 0) {
        entry_flags |= PTE_GLOBAL;
    }
    if ((flags & IOS_VM_OWNED) != 0) {
        entry_flags |= PTE_OWNED;
    }
    if ((flags & IOS_VM_EXECUTE) == 0) {
        entry_flags |= PTE_NO_EXECUTE;
    }
    *leaf = physical_address | entry_flags;
    if (address_space->root_address == x86_64_paging_root()) {
        x86_64_paging_invalidate(virtual_address);
    }
    return IOS_OK;
}

static ios_status unmap_one(
    struct ios_address_space *address_space,
    ios_uptr virtual_address,
    bool release_owned_page
)
{
    ios_u64 *pml4 = (ios_u64 *)address_space->root_address;
    ios_u64 *pml4e = &pml4[pml4_index(virtual_address)];
    ios_u64 *pdpt;
    ios_u64 *pdpte;
    ios_u64 *pd;
    ios_u64 *pde;
    ios_u64 *pt;
    ios_u64 *leaf;
    ios_u64 old_entry;

    if ((*pml4e & PTE_PRESENT) == 0 || (*pml4e & PTE_LARGE) != 0) {
        return IOS_ERROR(IOS_E_NOT_FOUND);
    }
    pdpt = table_from_entry(*pml4e);
    pdpte = &pdpt[pdpt_index(virtual_address)];
    if ((*pdpte & PTE_PRESENT) == 0 || (*pdpte & PTE_LARGE) != 0) {
        return IOS_ERROR(IOS_E_NOT_FOUND);
    }
    pd = table_from_entry(*pdpte);
    pde = &pd[pd_index(virtual_address)];
    if ((*pde & PTE_PRESENT) == 0 || (*pde & PTE_LARGE) != 0) {
        return IOS_ERROR(IOS_E_NOT_FOUND);
    }
    pt = table_from_entry(*pde);
    leaf = &pt[pt_index(virtual_address)];
    if ((*leaf & PTE_PRESENT) == 0) {
        return IOS_ERROR(IOS_E_NOT_FOUND);
    }

    old_entry = *leaf;
    *leaf = 0;
    if (address_space->root_address == x86_64_paging_root()) {
        x86_64_paging_invalidate(virtual_address);
    }
    if (release_owned_page && (old_entry & PTE_OWNED) != 0) {
        (void)physical_free_pages((ios_uptr)(old_entry & PTE_ADDRESS_MASK), 1);
    }
    if (table_is_empty(pt)) {
        const ios_uptr table_address = (ios_uptr)pt;
        *pde = 0;
        (void)physical_free_pages(table_address, 1);
    }
    if (table_is_empty(pd)) {
        const ios_uptr table_address = (ios_uptr)pd;
        *pdpte = 0;
        (void)physical_free_pages(table_address, 1);
    }
    if (table_is_empty(pdpt)) {
        const ios_uptr table_address = (ios_uptr)pdpt;
        *pml4e = 0;
        (void)physical_free_pages(table_address, 1);
    }
    return IOS_OK;
}

static void destroy_table(ios_u64 *table, ios_u32 level)
{
    for (ios_u16 index = 0; index < PAGE_TABLE_ENTRY_COUNT; ++index) {
        const ios_u64 entry = table[index];

        if ((entry & PTE_PRESENT) == 0) {
            continue;
        }
        if (level == 1U || (entry & PTE_LARGE) != 0) {
            if ((entry & PTE_OWNED) != 0) {
                (void)physical_free_pages((ios_uptr)(entry & PTE_ADDRESS_MASK), 1);
            }
        } else {
            ios_u64 *child = table_from_entry(entry);
            destroy_table(child, level - 1U);
            (void)physical_free_pages((ios_uptr)child, 1);
        }
        table[index] = 0;
    }
}

ios_status virtual_memory_initialize(void)
{
    if (!physical_memory_is_initialized()) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    kernel_root_address = x86_64_paging_root();
    execute_disable_enabled = x86_64_paging_enable_execute_disable();
    return execute_disable_enabled ? IOS_OK : IOS_ERROR(IOS_E_NOT_SUPPORTED);
}

void virtual_kernel_address_space_activate(void)
{
    IOS_ASSERT(kernel_root_address != 0);
    x86_64_paging_activate(kernel_root_address);
}

bool virtual_address_is_canonical(ios_uptr address)
{
    return address <= UINT64_C(0x00007fffffffffff)
        || address >= UINT64_C(0xffff800000000000);
}

bool virtual_user_range_is_valid(ios_uptr address, ios_u64 byte_count)
{
    ios_u64 end;

    if (byte_count == 0 || address < USER_VIRTUAL_START || address >= USER_VIRTUAL_END
        || byte_count > UINT64_MAX - address) {
        return false;
    }
    end = address + byte_count;
    return end <= USER_VIRTUAL_END;
}

static bool virtual_range_is_canonical(ios_uptr address, ios_u64 byte_count)
{
    ios_u64 last;

    if (byte_count == 0 || byte_count - 1U > UINT64_MAX - address) {
        return false;
    }
    last = address + byte_count - 1U;
    if (address <= UINT64_C(0x00007fffffffffff)) {
        return last <= UINT64_C(0x00007fffffffffff);
    }
    return address >= UINT64_C(0xffff800000000000)
        && last >= UINT64_C(0xffff800000000000);
}

ios_status virtual_address_space_create(struct ios_address_space *address_space)
{
    ios_uptr root_address;
    ios_u64 *root;
    const ios_u64 *current_root;
    ios_status status;

    if (!execute_disable_enabled || address_space == NULL) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    status = physical_allocate_pages(1, 1, &root_address);
    if (IOS_FAILED(status)) {
        return status;
    }
    root = (ios_u64 *)root_address;
    memset(root, 0, IOS_PAGE_SIZE);
    current_root = (const ios_u64 *)kernel_root_address;

    for (ios_u16 index = 0; index < 2; ++index) {
        root[index] = current_root[index];
    }
    for (ios_u16 index = 256; index < PAGE_TABLE_ENTRY_COUNT; ++index) {
        root[index] = current_root[index];
    }
    address_space->root_address = root_address;
    return IOS_OK;
}

void virtual_address_space_destroy(struct ios_address_space *address_space)
{
    ios_u64 *root;

    IOS_ASSERT(address_space != NULL);
    IOS_ASSERT(address_space->root_address != 0);
    IOS_ASSERT(address_space->root_address != kernel_root_address);

    root = (ios_u64 *)address_space->root_address;
    for (ios_u16 index = 2; index < 256; ++index) {
        if ((root[index] & PTE_PRESENT) != 0) {
            ios_u64 *child = table_from_entry(root[index]);
            destroy_table(child, 3);
            (void)physical_free_pages((ios_uptr)child, 1);
            root[index] = 0;
        }
    }
    (void)physical_free_pages(address_space->root_address, 1);
    address_space->root_address = 0;
}

void virtual_address_space_activate(const struct ios_address_space *address_space)
{
    IOS_ASSERT(address_space != NULL);
    IOS_ASSERT(address_space->root_address != 0);
    x86_64_paging_activate(address_space->root_address);
}

ios_status virtual_map_pages(
    struct ios_address_space *address_space,
    ios_uptr virtual_address,
    ios_uptr physical_address,
    ios_u64 page_count,
    ios_u32 flags
)
{
    ios_u64 byte_count;
    bool user_range;

    if (!execute_disable_enabled || address_space == NULL || address_space->root_address == 0
        || page_count == 0 || page_count > UINT64_MAX / IOS_PAGE_SIZE) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    byte_count = page_count * IOS_PAGE_SIZE;
    user_range = virtual_user_range_is_valid(virtual_address, byte_count);
    if ((virtual_address & (IOS_PAGE_SIZE - 1U)) != 0
        || (physical_address & (IOS_PAGE_SIZE - 1U)) != 0
        || virtual_address > UINT64_MAX - byte_count
        || physical_address > PTE_ADDRESS_MASK
        || byte_count - IOS_PAGE_SIZE > PTE_ADDRESS_MASK - physical_address
        || !virtual_range_is_canonical(virtual_address, byte_count)
        || ((flags & ~(IOS_VM_WRITE | IOS_VM_EXECUTE | IOS_VM_USER
                | IOS_VM_GLOBAL | IOS_VM_OWNED)) != 0)
        || ((flags & (IOS_VM_WRITE | IOS_VM_EXECUTE))
            == (IOS_VM_WRITE | IOS_VM_EXECUTE))
        || (((flags & IOS_VM_USER) != 0) != user_range)
        || (!user_range && address_space->root_address != x86_64_paging_root())) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if ((flags & IOS_VM_OWNED) != 0) {
        for (ios_u64 page = 0; page < page_count; ++page) {
            if (!physical_page_is_allocated(physical_address + page * IOS_PAGE_SIZE)) {
                return IOS_ERROR(IOS_E_INVALID_STATE);
            }
        }
    }

    for (ios_u64 page = 0; page < page_count; ++page) {
        const ios_status status = map_one(
            address_space,
            virtual_address + page * IOS_PAGE_SIZE,
            physical_address + page * IOS_PAGE_SIZE,
            flags
        );
        if (IOS_FAILED(status)) {
            while (page != 0) {
                --page;
                (void)unmap_one(
                    address_space,
                    virtual_address + page * IOS_PAGE_SIZE,
                    false
                );
            }
            return status;
        }
    }
    return IOS_OK;
}

ios_status virtual_unmap_pages(
    struct ios_address_space *address_space,
    ios_uptr virtual_address,
    ios_u64 page_count
)
{
    if (address_space == NULL || address_space->root_address == 0 || page_count == 0
        || (virtual_address & (IOS_PAGE_SIZE - 1U)) != 0
        || page_count > UINT64_MAX / IOS_PAGE_SIZE
        || virtual_address > UINT64_MAX - page_count * IOS_PAGE_SIZE) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (!virtual_user_range_is_valid(virtual_address, page_count * IOS_PAGE_SIZE)
        && address_space->root_address != x86_64_paging_root()) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    for (ios_u64 page = 0; page < page_count; ++page) {
        ios_u64 *leaf;
        if (IOS_FAILED(locate_leaf(address_space, virtual_address + page * IOS_PAGE_SIZE, &leaf))
            || (*leaf & PTE_PRESENT) == 0) {
            return IOS_ERROR(IOS_E_NOT_FOUND);
        }
    }
    for (ios_u64 page = 0; page < page_count; ++page) {
        (void)unmap_one(address_space, virtual_address + page * IOS_PAGE_SIZE, true);
    }
    return IOS_OK;
}

ios_status virtual_translate(
    const struct ios_address_space *address_space,
    ios_uptr virtual_address,
    ios_uptr *physical_address
)
{
    const ios_u64 *pml4;
    ios_u64 pml4e;
    const ios_u64 *pdpt;
    ios_u64 pdpte;
    const ios_u64 *pd;
    ios_u64 pde;
    const ios_u64 *pt;
    ios_u64 pte;

    if (address_space == NULL || physical_address == NULL || address_space->root_address == 0
        || !virtual_address_is_canonical(virtual_address)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }

    pml4 = (const ios_u64 *)address_space->root_address;
    pml4e = pml4[pml4_index(virtual_address)];
    if ((pml4e & PTE_PRESENT) == 0 || (pml4e & PTE_LARGE) != 0) {
        return IOS_ERROR(IOS_E_NOT_FOUND);
    }

    pdpt = table_from_entry(pml4e);
    pdpte = pdpt[pdpt_index(virtual_address)];
    if ((pdpte & PTE_PRESENT) == 0) return IOS_ERROR(IOS_E_NOT_FOUND);
    if ((pdpte & PTE_LARGE) != 0) {
        *physical_address = (ios_uptr)(pdpte & PTE_1G_ADDRESS_MASK)
            + (virtual_address & ((UINT64_C(1) << 30U) - 1U));
        return IOS_OK;
    }

    pd = table_from_entry(pdpte);
    pde = pd[pd_index(virtual_address)];
    if ((pde & PTE_PRESENT) == 0) return IOS_ERROR(IOS_E_NOT_FOUND);
    if ((pde & PTE_LARGE) != 0) {
        *physical_address = (ios_uptr)(pde & PTE_2M_ADDRESS_MASK)
            + (virtual_address & ((UINT64_C(1) << 21U) - 1U));
        return IOS_OK;
    }

    pt = table_from_entry(pde);
    pte = pt[pt_index(virtual_address)];
    if ((pte & PTE_PRESENT) == 0) return IOS_ERROR(IOS_E_NOT_FOUND);
    *physical_address = (ios_uptr)(pte & PTE_ADDRESS_MASK)
        + (virtual_address & (IOS_PAGE_SIZE - 1U));
    return IOS_OK;
}

ios_status virtual_query(
    const struct ios_address_space *address_space,
    ios_uptr virtual_address,
    ios_uptr *physical_address,
    ios_u32 *flags
)
{
    ios_u64 *leaf;
    ios_status status;
    ios_u32 result = 0;

    if (flags == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    status = virtual_translate(address_space, virtual_address, physical_address);
    if (IOS_FAILED(status)) {
        return status;
    }
    status = locate_leaf(address_space, virtual_address, &leaf);
    if (IOS_FAILED(status)) {
        return status;
    }
    if ((*leaf & PTE_WRITABLE) != 0) {
        result |= IOS_VM_WRITE;
    }
    if ((*leaf & PTE_NO_EXECUTE) == 0) {
        result |= IOS_VM_EXECUTE;
    }
    if ((*leaf & PTE_USER) != 0) {
        result |= IOS_VM_USER;
    }
    if ((*leaf & PTE_GLOBAL) != 0) {
        result |= IOS_VM_GLOBAL;
    }
    if ((*leaf & PTE_OWNED) != 0) {
        result |= IOS_VM_OWNED;
    }
    *flags = result;
    return IOS_OK;
}
