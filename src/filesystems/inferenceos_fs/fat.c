#include <inferenceos/fs/fat.h>

#include <inferenceos/runtime.h>

static bool valid_cluster(ios_u32 cluster, ios_size entry_count)
{
    return cluster >= 2 && cluster < entry_count && cluster <= IOS_FS_MAX_DATA_CLUSTER;
}

static bool valid_entry_count(ios_size entry_count)
{
    return entry_count > 2 && entry_count <= (ios_size)IOS_FS_MAX_DATA_CLUSTER + 1;
}

static ios_status classify_next(ios_u32 raw, ios_size entry_count, bool *eoc)
{
    ios_u32 value;
    *eoc = false;
    if ((raw & IOS_FS_FAT_UPPER_MASK) != 0) return IOS_ERROR(IOS_E_PROTOCOL);
    value = raw & IOS_FS_FAT_VALUE_MASK;
    if (value >= IOS_FS_FAT_EOC_FIRST) {
        *eoc = true;
        return IOS_OK;
    }
    if (value == IOS_FS_FAT_FREE || value == IOS_FS_FAT_BAD
        || (value >= IOS_FS_FAT_RESERVED_FIRST && value <= IOS_FS_FAT_RESERVED_LAST)
        || !valid_cluster(value, entry_count)) return IOS_ERROR(IOS_E_CORRUPT);
    return IOS_OK;
}

ios_status ios_fs_fat_entry_encode(ios_u32 value, ios_u8 disk[4])
{
    if (disk == NULL || (value & IOS_FS_FAT_UPPER_MASK) != 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    for (ios_size index = 0; index < 4; ++index) disk[index] = (ios_u8)(value >> (index * 8));
    return IOS_OK;
}

ios_status ios_fs_fat_entry_decode(const ios_u8 disk[4], ios_u32 *value)
{
    ios_u32 decoded = 0;
    if (disk == NULL || value == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    for (ios_size index = 0; index < 4; ++index) decoded |= (ios_u32)disk[index] << (index * 8);
    if ((decoded & IOS_FS_FAT_UPPER_MASK) != 0) return IOS_ERROR(IOS_E_PROTOCOL);
    *value = decoded;
    return IOS_OK;
}

ios_status ios_fs_fat_traverse(
    const ios_u32 *fat,
    ios_size entry_count,
    ios_u32 start,
    ios_u32 *clusters,
    ios_size cluster_capacity,
    ios_size *cluster_count
)
{
    ios_u32 cluster = start;
    ios_size count = 0;
    if (fat == NULL || clusters == NULL || cluster_count == NULL || !valid_entry_count(entry_count)
        || cluster_capacity == 0 || !valid_cluster(start, entry_count)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *cluster_count = 0;
    while (count < entry_count) {
        ios_u32 next;
        bool eoc;
        if (!valid_cluster(cluster, entry_count)) return IOS_ERROR(IOS_E_CORRUPT);
        for (ios_size prior = 0; prior < count; ++prior) {
            if (clusters[prior] == cluster) return IOS_ERROR(IOS_E_CORRUPT);
        }
        if (count == cluster_capacity) return IOS_ERROR(IOS_E_NO_SPACE);
        clusters[count++] = cluster;
        *cluster_count = count;
        next = fat[cluster];
        {
            const ios_status status = classify_next(next, entry_count, &eoc);
            if (IOS_FAILED(status)) return status;
        }
        if (eoc) return IOS_OK;
        cluster = next & IOS_FS_FAT_VALUE_MASK;
    }
    return IOS_ERROR(IOS_E_CORRUPT);
}

ios_status ios_fs_fat_allocate(
    ios_u32 *fat,
    ios_size entry_count,
    ios_size requested,
    ios_u32 *clusters,
    ios_size cluster_capacity,
    ios_size *cluster_count
)
{
    ios_size found = 0;
    if (fat == NULL || clusters == NULL || cluster_count == NULL || !valid_entry_count(entry_count)
        || requested == 0 || requested > cluster_capacity) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *cluster_count = 0;
    for (ios_u32 cluster = 2; cluster < entry_count && found < requested; ++cluster) {
        if (fat[cluster] == IOS_FS_FAT_FREE) clusters[found++] = cluster;
    }
    if (found != requested) return IOS_ERROR(IOS_E_NO_SPACE);
    for (ios_size index = 0; index < requested; ++index) {
        fat[clusters[index]] = index + 1 < requested ? clusters[index + 1] : IOS_FS_FAT_END_OF_CHAIN;
    }
    *cluster_count = requested;
    return IOS_OK;
}

ios_status ios_fs_fat_free(
    ios_u32 *fat,
    ios_size entry_count,
    ios_u32 start,
    ios_u32 *workspace,
    ios_size workspace_capacity
)
{
    ios_size count;
    ios_status status = ios_fs_fat_traverse(
        fat, entry_count, start, workspace, workspace_capacity, &count
    );
    if (IOS_FAILED(status)) return status;
    for (ios_size index = 0; index < count; ++index) fat[workspace[index]] = IOS_FS_FAT_FREE;
    return IOS_OK;
}

ios_status ios_fs_fat_validate_ownership(
    const ios_u32 *fat,
    ios_size entry_count,
    const ios_u32 *starts,
    ios_size owner_count,
    ios_u32 *owner_map,
    ios_size owner_map_count
)
{
    if (fat == NULL || starts == NULL || owner_map == NULL || owner_count == 0
        || !valid_entry_count(entry_count) || owner_map_count < entry_count
        || owner_count > UINT32_MAX) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    memset(owner_map, 0, entry_count * sizeof(*owner_map));
    for (ios_size owner = 0; owner < owner_count; ++owner) {
        ios_u32 cluster = starts[owner];
        ios_size traversed = 0;
        if (!valid_cluster(cluster, entry_count)) return IOS_ERROR(IOS_E_CORRUPT);
        while (traversed++ < entry_count) {
            bool eoc;
            ios_status status;
            if (!valid_cluster(cluster, entry_count) || owner_map[cluster] != 0) {
                return IOS_ERROR(IOS_E_CORRUPT);
            }
            owner_map[cluster] = (ios_u32)(owner + 1);
            status = classify_next(fat[cluster], entry_count, &eoc);
            if (IOS_FAILED(status)) return status;
            if (eoc) break;
            cluster = fat[cluster] & IOS_FS_FAT_VALUE_MASK;
        }
        if (traversed > entry_count) return IOS_ERROR(IOS_E_CORRUPT);
    }
    return IOS_OK;
}

ios_status ios_fs_fat_validate_file_capacity(
    const ios_u32 *fat,
    ios_size entry_count,
    ios_u32 start,
    ios_u32 file_size,
    ios_u32 cluster_bytes
)
{
    ios_u32 slow = start;
    ios_u64 capacity = 0;
    ios_size traversed = 0;
    if (file_size == 0) return start == 0 ? IOS_OK : IOS_ERROR(IOS_E_CORRUPT);
    if (fat == NULL || cluster_bytes == 0 || !valid_entry_count(entry_count)
        || !valid_cluster(start, entry_count)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    while (traversed++ < entry_count) {
        bool eoc;
        ios_status status;
        if (!valid_cluster(slow, entry_count)) return IOS_ERROR(IOS_E_CORRUPT);
        capacity += cluster_bytes;
        status = classify_next(fat[slow], entry_count, &eoc);
        if (IOS_FAILED(status)) return status;
        if (eoc) return capacity >= file_size ? IOS_OK : IOS_ERROR(IOS_E_CORRUPT);
        slow = fat[slow] & IOS_FS_FAT_VALUE_MASK;
    }
    return IOS_ERROR(IOS_E_CORRUPT);
}
