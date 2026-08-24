#include <inferenceos/fs/subdirectory.h>

#include <inferenceos/runtime.h>

static bool valid_store(const struct ios_fs_directory_store *store)
{
    return store != NULL && store->fat != NULL && store->fat_entry_count > 2
        && store->cluster_bytes == IOS_FS_SECTOR_SIZE * IOS_FS_SECTORS_PER_CLUSTER
        && store->operations.read != NULL && store->operations.write != NULL
        && store->operations.zero != NULL;
}

static void write_cluster(ios_u8 low[2], ios_u8 high[2], ios_u32 cluster)
{
    *low = (ios_u8)cluster;
    low[1] = (ios_u8)(cluster >> 8);
    *high = (ios_u8)(cluster >> 16);
    high[1] = (ios_u8)(cluster >> 24);
}

static void make_internal_record(
    struct ios_fs_primary_disk *disk, bool parent, ios_u32 cluster
)
{
    memset(disk, 0, sizeof(*disk));
    memset(disk->name, ' ', sizeof(disk->name));
    *disk->name = '.';
    if (parent) disk->name[1] = '.';
    disk->attributes = IOS_FS_ATTRIBUTE_DIRECTORY;
    write_cluster(disk->first_cluster_low, disk->first_cluster_high, cluster);
}

ios_status ios_fs_subdirectory_initialize(
    struct ios_fs_directory_store *store,
    ios_u32 cluster,
    ios_u32 parent_cluster,
    ios_u8 *scratch,
    ios_size scratch_size
)
{
    struct ios_fs_primary_disk current;
    struct ios_fs_primary_disk parent;
    if (!valid_store(store) || scratch == NULL || scratch_size < store->cluster_bytes
        || !store->writable || cluster < 2 || cluster >= store->fat_entry_count
        || parent_cluster < 2 || parent_cluster >= store->fat_entry_count
        || store->fat[cluster] < IOS_FS_FAT_EOC_FIRST) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    memset(scratch, 0, store->cluster_bytes);
    make_internal_record(&current, false, cluster);
    make_internal_record(&parent, true, parent_cluster);
    memcpy(scratch, &current, sizeof(current));
    memcpy(scratch + IOS_FS_PRIMARY_RECORD_SIZE, &parent, sizeof(parent));
    return store->operations.write(store->io_context, cluster, scratch);
}

ios_status ios_fs_subdirectory_grow(
    struct ios_fs_directory_store *store,
    ios_u32 first_cluster,
    ios_u32 *chain_workspace,
    ios_size chain_capacity,
    ios_u32 *new_cluster
)
{
    ios_u32 allocated;
    ios_size chain_count;
    ios_size allocated_count;
    ios_status status;
    if (!valid_store(store) || chain_workspace == NULL || chain_capacity == 0
        || new_cluster == NULL || !store->writable) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *new_cluster = 0;
    status = ios_fs_fat_traverse(
        store->fat, store->fat_entry_count, first_cluster,
        chain_workspace, chain_capacity, &chain_count
    );
    if (IOS_FAILED(status)) return status;
    status = ios_fs_fat_allocate(
        store->fat, store->fat_entry_count, 1, &allocated, 1, &allocated_count
    );
    if (IOS_FAILED(status)) return status;
    status = store->operations.zero(store->io_context, allocated);
    if (IOS_FAILED(status)) {
        store->fat[allocated] = IOS_FS_FAT_FREE;
        return status;
    }
    store->fat[chain_workspace[chain_count - 1]] = allocated;
    *new_cluster = allocated;
    return IOS_OK;
}

static bool same_name(
    const struct ios_fs_directory_entry *left,
    const struct ios_fs_directory_entry *right
)
{
    return memcmp(left->primary.name, right->primary.name, IOS_FS_NAME_SIZE) == 0;
}

static bool internal_entry_is(
    const struct ios_fs_directory_entry *entry, bool parent, ios_u32 cluster
)
{
    if (entry->kind != IOS_FS_DIRECTORY_ENTRY_INTERNAL
        || (cluster != 0 && entry->primary.first_cluster != cluster)
        || *entry->primary.name != '.') return false;
    if (parent) return entry->primary.name[1] == '.';
    return entry->primary.name[1] == ' ';
}

ios_status ios_fs_subdirectory_enumerate(
    struct ios_fs_directory_store *store,
    ios_u32 first_cluster,
    ios_u32 *chain_workspace,
    ios_size chain_capacity,
    ios_u8 *scratch,
    ios_size scratch_size,
    struct ios_fs_directory_entry *entries,
    ios_size entry_capacity,
    ios_size *entry_count
)
{
    ios_size chain_count;
    ios_size output_count = 0;
    ios_status status;
    if (!valid_store(store) || chain_workspace == NULL || chain_capacity == 0
        || scratch == NULL || scratch_size < store->cluster_bytes
        || entries == NULL || entry_count == NULL || entry_capacity == 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *entry_count = 0;
    status = ios_fs_fat_traverse(
        store->fat, store->fat_entry_count, first_cluster,
        chain_workspace, chain_capacity, &chain_count
    );
    if (IOS_FAILED(status)) return status;
    for (ios_size chain_index = 0; chain_index < chain_count; ++chain_index) {
        struct ios_fs_directory_entry decoded[IOS_FS_DIRECTORY_SLOTS_PER_CLUSTER];
        ios_size decoded_count = 0;
        bool end_seen = false;
        status = store->operations.read(
            store->io_context, chain_workspace[chain_index], scratch
        );
        if (IOS_FAILED(status)) return status;
        for (ios_size slot = 0; slot < IOS_FS_DIRECTORY_SLOTS_PER_CLUSTER; ++slot) {
            if (scratch[slot * IOS_FS_PRIMARY_RECORD_SIZE] == 0) {
                end_seen = true;
                break;
            }
        }
        status = ios_fs_directory_scan(
            scratch, IOS_FS_DIRECTORY_SLOTS_PER_CLUSTER,
            IOS_FS_DIRECTORY_SLOTS_PER_CLUSTER, decoded,
            IOS_ARRAY_COUNT(decoded), &decoded_count
        );
        if (IOS_FAILED(status)) return status;
        if (chain_index == 0
            && (decoded_count < 2
                || !internal_entry_is(decoded, false, first_cluster)
                || !internal_entry_is(&decoded[1], true, 0))) {
            return IOS_ERROR(IOS_E_CORRUPT);
        }
        for (ios_size index = 0; index < decoded_count; ++index) {
            if (decoded[index].kind == IOS_FS_DIRECTORY_ENTRY_INTERNAL) {
                if (chain_index != 0 || index > 1) return IOS_ERROR(IOS_E_CORRUPT);
                continue;
            }
            for (ios_size prior = 0; prior < output_count; ++prior) {
                if (same_name(&entries[prior], &decoded[index])) {
                    return IOS_ERROR(IOS_E_ALREADY_EXISTS);
                }
            }
            if (output_count == entry_capacity) return IOS_ERROR(IOS_E_NO_SPACE);
            decoded[index].primary_slot += chain_index * IOS_FS_DIRECTORY_SLOTS_PER_CLUSTER;
            if (decoded[index].companion_slot != SIZE_MAX) {
                decoded[index].companion_slot +=
                    chain_index * IOS_FS_DIRECTORY_SLOTS_PER_CLUSTER;
            }
            entries[output_count++] = decoded[index];
            *entry_count = output_count;
        }
        if (end_seen) break;
    }
    return IOS_OK;
}

ios_status ios_fs_subdirectory_remove(
    struct ios_fs_directory_store *store,
    ios_u32 first_cluster,
    ios_u32 parent_cluster,
    ios_u32 *chain_workspace,
    ios_size chain_capacity,
    ios_u8 *scratch,
    ios_size scratch_size
)
{
    ios_size chain_count;
    ios_status status;
    if (!valid_store(store) || !store->writable || chain_workspace == NULL
        || chain_capacity == 0 || scratch == NULL || scratch_size < store->cluster_bytes
        || parent_cluster < 2 || parent_cluster >= store->fat_entry_count) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    status = ios_fs_fat_traverse(
        store->fat, store->fat_entry_count, first_cluster,
        chain_workspace, chain_capacity, &chain_count
    );
    if (IOS_FAILED(status)) return status;
    for (ios_size chain_index = 0; chain_index < chain_count; ++chain_index) {
        struct ios_fs_directory_entry decoded[IOS_FS_DIRECTORY_SLOTS_PER_CLUSTER];
        ios_size decoded_count = 0;
        status = store->operations.read(
            store->io_context, chain_workspace[chain_index], scratch
        );
        if (IOS_FAILED(status)) return status;
        status = ios_fs_directory_scan(
            scratch, IOS_FS_DIRECTORY_SLOTS_PER_CLUSTER,
            IOS_FS_DIRECTORY_SLOTS_PER_CLUSTER, decoded,
            IOS_ARRAY_COUNT(decoded), &decoded_count
        );
        if (IOS_FAILED(status)) return status;
        if (chain_index == 0
            && (decoded_count < 2
                || !internal_entry_is(decoded, false, first_cluster)
                || !internal_entry_is(&decoded[1], true, parent_cluster))) {
            return IOS_ERROR(IOS_E_CORRUPT);
        }
        for (ios_size index = 0; index < decoded_count; ++index) {
            if (chain_index == 0 && index == 0
                && internal_entry_is(&decoded[index], false, first_cluster)) continue;
            if (chain_index == 0 && index == 1
                && internal_entry_is(&decoded[index], true, parent_cluster)) continue;
            return IOS_ERROR(IOS_E_NOT_EMPTY);
        }
    }
    return ios_fs_fat_free(
        store->fat, store->fat_entry_count, first_cluster,
        chain_workspace, chain_capacity
    );
}
