#include <inferenceos/fs/format.h>

#include <inferenceos/block.h>
#include <inferenceos/runtime.h>

enum { FORMAT_WRITE_SECTORS = 8, FAT_ENTRY_BYTES = 4, FAT_ENTRIES_PER_SECTOR = 128 };

static void write_u32(ios_u8 *bytes, ios_u32 value)
{
    for (ios_size index = 0; index < 4; ++index) bytes[index] = (ios_u8)(value >> (index * 8));
}

ios_status ios_fs_calculate_geometry(
    ios_u32 logical_sector_size, ios_u64 total_sectors, struct ios_fs_geometry *geometry)
{
    ios_u64 low = 1;
    ios_u64 high;
    ios_u64 fixed_sectors;
    ios_u64 cluster_count;
    ios_u64 minimum_sectors = IOS_FS_MINIMUM_VOLUME_BYTES / IOS_FS_SECTOR_SIZE;
    if (geometry == NULL || logical_sector_size != IOS_FS_SECTOR_SIZE) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    memset(geometry, 0, sizeof(*geometry));
    if (total_sectors < minimum_sectors) return IOS_ERROR(IOS_E_NO_SPACE);
    high = ((total_sectors / IOS_FS_SECTORS_PER_CLUSTER) + 2
            + (FAT_ENTRIES_PER_SECTOR - 1)) / FAT_ENTRIES_PER_SECTOR;
    while (low < high) {
        ios_u64 candidate = low + (high - low) / 2;
        ios_u64 required_entries;
        ios_u64 required_fat_sectors;
        if (candidate > UINT64_MAX - IOS_FS_SUPERBLOCK_SECTORS - IOS_FS_REGISTRY_SECTORS) {
            return IOS_ERROR(IOS_E_OVERFLOW);
        }
        fixed_sectors = IOS_FS_SUPERBLOCK_SECTORS + candidate + IOS_FS_REGISTRY_SECTORS;
        if (fixed_sectors >= total_sectors) return IOS_ERROR(IOS_E_NO_SPACE);
        cluster_count = (total_sectors - fixed_sectors) / IOS_FS_SECTORS_PER_CLUSTER;
        if (cluster_count > UINT64_MAX - 2) return IOS_ERROR(IOS_E_OVERFLOW);
        required_entries = cluster_count + 2;
        required_fat_sectors = (required_entries + (FAT_ENTRIES_PER_SECTOR - 1))
            / FAT_ENTRIES_PER_SECTOR;
        if (required_fat_sectors <= candidate) high = candidate;
        else low = candidate + 1;
    }
    if (low > UINT32_MAX) return IOS_ERROR(IOS_E_OUT_OF_RANGE);
    fixed_sectors = IOS_FS_SUPERBLOCK_SECTORS + low + IOS_FS_REGISTRY_SECTORS;
    if (fixed_sectors >= total_sectors) return IOS_ERROR(IOS_E_NO_SPACE);
    cluster_count = (total_sectors - fixed_sectors) / IOS_FS_SECTORS_PER_CLUSTER;
    if (cluster_count == 0 || cluster_count > (ios_u64)IOS_FS_MAX_DATA_CLUSTER - 1
        || cluster_count > UINT32_MAX
        || low * FAT_ENTRIES_PER_SECTOR < cluster_count + 2) {
        return IOS_ERROR(IOS_E_OUT_OF_RANGE);
    }
    geometry->total_sectors = total_sectors;
    geometry->fat_sectors = (ios_u32)low;
    geometry->registry_start_sector = IOS_FS_SUPERBLOCK_SECTORS + low;
    geometry->data_start_sector = geometry->registry_start_sector + IOS_FS_REGISTRY_SECTORS;
    geometry->cluster_count = (ios_u32)cluster_count;
    geometry->usable_bytes = cluster_count * IOS_FS_SECTORS_PER_CLUSTER * IOS_FS_SECTOR_SIZE;
    return IOS_OK;
}

static ios_status zero_sectors(
    struct ios_block_device *device, ios_u64 first_sector, ios_u64 sector_count)
{
    ios_u8 zeros[FORMAT_WRITE_SECTORS * IOS_FS_SECTOR_SIZE] = { 0 };
    while (sector_count != 0) {
        ios_size chunk = sector_count < FORMAT_WRITE_SECTORS ? (ios_size)sector_count
                                                             : FORMAT_WRITE_SECTORS;
        ios_status status = block_device_write(device, first_sector, chunk, zeros);
        if (IOS_FAILED(status)) return status;
        first_sector += chunk;
        sector_count -= chunk;
    }
    return IOS_OK;
}

ios_status ios_fs_format(
    struct ios_block_device *device, ios_u32 volume_serial,
    const ios_u8 volume_label[IOS_FS_VOLUME_LABEL_SIZE], struct ios_fs_geometry *geometry)
{
    struct ios_fs_geometry calculated;
    struct ios_fs_superblock values;
    struct ios_fs_superblock_disk superblock;
    ios_u8 fat_sector[IOS_FS_SECTOR_SIZE] = { 0 };
    ios_status status;
    if (device == NULL || volume_label == NULL || geometry == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    memset(geometry, 0, sizeof(*geometry));
    if (block_device_get_status(device) != IOS_BLOCK_DEVICE_READY) {
        return block_device_get_status(device) == IOS_BLOCK_DEVICE_READ_ONLY
            ? IOS_ERROR(IOS_E_READ_ONLY) : IOS_ERROR(IOS_E_INVALID_STATE);
    }
    status = ios_fs_calculate_geometry(device->logical_sector_size, device->sector_count,
                                       &calculated);
    if (IOS_FAILED(status)) return status;
    values = (struct ios_fs_superblock){
        calculated.total_sectors, calculated.fat_sectors,
        calculated.registry_start_sector, volume_serial, 0, { 0 }
    };
    memcpy(values.volume_label, volume_label, IOS_FS_VOLUME_LABEL_SIZE);
    status = ios_fs_superblock_encode(&values, &superblock);
    if (IOS_FAILED(status)) return status;

    status = zero_sectors(device, IOS_FS_SUPERBLOCK_SECTORS,
                          calculated.data_start_sector + IOS_FS_SECTORS_PER_CLUSTER
                              - IOS_FS_SUPERBLOCK_SECTORS);
    if (IOS_FAILED(status)) return status;
    write_u32(fat_sector, IOS_FS_FAT_END_OF_CHAIN);
    write_u32(fat_sector + FAT_ENTRY_BYTES, IOS_FS_FAT_END_OF_CHAIN);
    write_u32(fat_sector + 2 * FAT_ENTRY_BYTES, IOS_FS_FAT_END_OF_CHAIN);
    status = block_device_write(device, IOS_FS_SUPERBLOCK_SECTORS, 1, fat_sector);
    if (IOS_FAILED(status)) return status;
    status = block_device_write(device, 1, 1, &superblock);
    if (IOS_FAILED(status)) return status;
    status = block_device_write(device, 0, 1, &superblock);
    if (IOS_FAILED(status)) return status;
    status = block_device_flush(device);
    if (IOS_FAILED(status)) return status;
    *geometry = calculated;
    return IOS_OK;
}
