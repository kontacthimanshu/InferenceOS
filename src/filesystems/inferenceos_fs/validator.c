#include <inferenceos/fs/validator.h>

#include <inferenceos/runtime.h>

enum { FAT_ENTRY_BYTES = 4, FAT_ENTRIES_PER_SECTOR = IOS_FS_SECTOR_SIZE / FAT_ENTRY_BYTES };

static bool valid_entry_count(ios_size entry_count)
{
    return entry_count > IOS_FS_ROOT_CLUSTER
        && entry_count <= (ios_size)IOS_FS_MAX_DATA_CLUSTER + 1;
}

static bool valid_cluster(ios_u32 cluster, ios_size entry_count)
{
    return cluster >= IOS_FS_ROOT_CLUSTER && cluster < entry_count
        && cluster <= IOS_FS_MAX_DATA_CLUSTER;
}

static ios_status classify_next(
    ios_u32 raw, ios_size entry_count, ios_u32 *next, bool *end_of_chain)
{
    if (next == NULL || end_of_chain == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    *next = 0;
    *end_of_chain = false;
    if ((raw & IOS_FS_FAT_UPPER_MASK) != 0) return IOS_ERROR(IOS_E_CORRUPT);
    raw &= IOS_FS_FAT_VALUE_MASK;
    if (raw >= IOS_FS_FAT_EOC_FIRST) {
        *end_of_chain = true;
        return IOS_OK;
    }
    if (raw == IOS_FS_FAT_FREE || raw == IOS_FS_FAT_BAD
        || (raw >= IOS_FS_FAT_RESERVED_FIRST && raw <= IOS_FS_FAT_RESERVED_LAST)
        || !valid_cluster(raw, entry_count)) {
        return IOS_ERROR(IOS_E_CORRUPT);
    }
    *next = raw;
    return IOS_OK;
}

ios_status ios_fs_validate_owners(
    const ios_u32 *fat, ios_size entry_count,
    const struct ios_fs_allocation_owner *owners, ios_size owner_count,
    ios_u32 *owner_map, ios_size owner_map_count)
{
    if (fat == NULL || owners == NULL || owner_map == NULL || owner_count == 0
        || owner_count > UINT32_MAX || !valid_entry_count(entry_count)
        || owner_map_count < entry_count) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    memset(owner_map, 0, entry_count * sizeof(*owner_map));
    for (ios_size owner_index = 0; owner_index < owner_count; ++owner_index) {
        const struct ios_fs_allocation_owner *owner = &owners[owner_index];
        ios_u32 cluster = owner->first_cluster;
        ios_u64 capacity = 0;
        bool complete = false;
        if (owner->kind != IOS_FS_OWNER_DIRECTORY
            && owner->kind != IOS_FS_OWNER_REGULAR_FILE) {
            return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
        }
        if (owner->kind == IOS_FS_OWNER_REGULAR_FILE && owner->file_size == 0) {
            if (cluster != 0) return IOS_ERROR(IOS_E_CORRUPT);
            continue;
        }
        if (!valid_cluster(cluster, entry_count)) return IOS_ERROR(IOS_E_CORRUPT);
        for (ios_size traversed = 0; traversed < entry_count; ++traversed) {
            ios_u32 next;
            bool end_of_chain;
            ios_status status;
            if (!valid_cluster(cluster, entry_count) || owner_map[cluster] != 0) {
                return IOS_ERROR(IOS_E_CORRUPT);
            }
            owner_map[cluster] = (ios_u32)(owner_index + 1);
            if (capacity > UINT64_MAX - IOS_FS_SECTOR_SIZE * IOS_FS_SECTORS_PER_CLUSTER) {
                return IOS_ERROR(IOS_E_OVERFLOW);
            }
            capacity += IOS_FS_SECTOR_SIZE * IOS_FS_SECTORS_PER_CLUSTER;
            status = classify_next(fat[cluster], entry_count, &next, &end_of_chain);
            if (IOS_FAILED(status)) return status;
            if (end_of_chain) {
                complete = true;
                break;
            }
            cluster = next;
        }
        if (!complete) return IOS_ERROR(IOS_E_CORRUPT);
        if (owner->kind == IOS_FS_OWNER_REGULAR_FILE && capacity < owner->file_size) {
            return IOS_ERROR(IOS_E_CORRUPT);
        }
    }
    return IOS_OK;
}

static ios_status read_fat_entry(
    struct ios_block_device *device, const struct ios_fs_geometry *geometry,
    ios_u32 cluster, ios_u32 *value)
{
    ios_u8 sector[IOS_FS_SECTOR_SIZE];
    const ios_size entry_count = (ios_size)geometry->cluster_count + IOS_FS_ROOT_CLUSTER;
    const ios_u64 fat_sector = IOS_FS_SUPERBLOCK_SECTORS
        + (ios_u64)cluster / FAT_ENTRIES_PER_SECTOR;
    const ios_size offset = (cluster % FAT_ENTRIES_PER_SECTOR) * FAT_ENTRY_BYTES;
    ios_status status;
    if (value == NULL || !valid_cluster(cluster, entry_count)
        || fat_sector >= geometry->registry_start_sector) {
        return IOS_ERROR(IOS_E_CORRUPT);
    }
    status = block_device_read(device, fat_sector, 1, sector);
    if (IOS_FAILED(status)) return status;
    status = ios_fs_fat_entry_decode(sector + offset, value);
    return IOS_FAILED(status) ? IOS_ERROR(IOS_E_CORRUPT) : IOS_OK;
}

static ios_status advance(
    struct ios_block_device *device, const struct ios_fs_geometry *geometry,
    ios_u32 cluster, ios_u32 *next, bool *end_of_chain)
{
    ios_u32 raw;
    ios_status status = read_fat_entry(device, geometry, cluster, &raw);
    if (IOS_FAILED(status)) return status;
    return classify_next(
        raw, (ios_size)geometry->cluster_count + IOS_FS_ROOT_CLUSTER,
        next, end_of_chain);
}

static ios_status validate_reserved_entries(struct ios_block_device *device)
{
    ios_u8 sector[IOS_FS_SECTOR_SIZE];
    ios_u32 value;
    ios_status status = block_device_read(device, IOS_FS_SUPERBLOCK_SECTORS, 1, sector);
    if (IOS_FAILED(status)) return status;
    for (ios_size index = 0; index < IOS_FS_ROOT_CLUSTER; ++index) {
        status = ios_fs_fat_entry_decode(sector + index * FAT_ENTRY_BYTES, &value);
        if (IOS_FAILED(status) || value < IOS_FS_FAT_EOC_FIRST) {
            return IOS_ERROR(IOS_E_CORRUPT);
        }
    }
    return IOS_OK;
}

ios_status ios_fs_validate_root_chain(
    struct ios_block_device *device, const struct ios_fs_geometry *geometry,
    ios_size *cluster_count)
{
    ios_u32 slow = IOS_FS_ROOT_CLUSTER;
    ios_u32 fast = IOS_FS_ROOT_CLUSTER;
    ios_size count = 0;
    bool fast_ended = false;
    if (device == NULL || geometry == NULL || cluster_count == NULL
        || geometry->cluster_count == 0
        || geometry->cluster_count > IOS_FS_MAX_DATA_CLUSTER - 1) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *cluster_count = 0;
    {
        const ios_status status = validate_reserved_entries(device);
        if (IOS_FAILED(status)) return status;
    }
    while (count < geometry->cluster_count) {
        ios_u32 next;
        bool end_of_chain;
        ios_status status = advance(device, geometry, slow, &next, &end_of_chain);
        if (IOS_FAILED(status)) return status;
        ++count;
        *cluster_count = count;
        if (end_of_chain) return IOS_OK;
        slow = next;

        if (!fast_ended) {
            status = advance(device, geometry, fast, &next, &end_of_chain);
            if (IOS_FAILED(status)) return status;
            if (end_of_chain) {
                fast_ended = true;
            } else {
                fast = next;
                status = advance(device, geometry, fast, &next, &end_of_chain);
                if (IOS_FAILED(status)) return status;
                if (end_of_chain) fast_ended = true;
                else fast = next;
            }
            if (!fast_ended && slow == fast) return IOS_ERROR(IOS_E_CORRUPT);
        }
    }
    return IOS_ERROR(IOS_E_CORRUPT);
}
