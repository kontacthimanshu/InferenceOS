#include <inferenceos/block.h>

#include <inferenceos/runtime.h>

enum {
    DISK_PROBE_SECTORS = 2048,
    DISK_PROBE_BATCH_SECTORS = 8,
    GPT_HEADER_LBA = 1,
    GPT_MINIMUM_HEADER_SIZE = 92,
    GPT_PARTITION_ENTRY_MINIMUM_SIZE = 128
};

static const ios_u8 inference_fs_magic[8] = {'I','N','F','O','S','F','S','1'};

static ios_u16 read_le16(const ios_u8 *bytes)
{
    return (ios_u16)(bytes[0] | ((ios_u16)bytes[1] << 8));
}

static ios_u32 read_le32(const ios_u8 *bytes)
{
    return (ios_u32)bytes[0] | ((ios_u32)bytes[1] << 8)
        | ((ios_u32)bytes[2] << 16) | ((ios_u32)bytes[3] << 24);
}

static ios_u64 read_le64(const ios_u8 *bytes)
{
    return (ios_u64)read_le32(bytes) | ((ios_u64)read_le32(bytes + 4) << 32);
}

static ios_u32 crc32_zeroed_field(
    const ios_u8 *bytes, ios_size size, ios_size zero_offset, ios_size zero_size
)
{
    ios_u32 crc = UINT32_C(0xffffffff);

    for (ios_size index = 0; index < size; ++index) {
        ios_u8 value = index >= zero_offset && index < zero_offset + zero_size
            ? 0 : bytes[index];
        crc ^= value;
        for (ios_u32 bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1U) != 0 ? UINT32_C(0xedb88320) : 0);
        }
    }
    return crc ^ UINT32_C(0xffffffff);
}

static bool guid_is_zero(const ios_u8 guid[16])
{
    ios_u8 combined = 0;
    for (ios_size index = 0; index < 16; ++index) combined |= guid[index];
    return combined == 0;
}

static bool bytes_are_zero(const ios_u8 *bytes, ios_size size)
{
    ios_u8 combined = 0;
    for (ios_size index = 0; index < size; ++index) combined |= bytes[index];
    return combined == 0;
}

static bool inference_fs_superblocks_valid(const ios_u8 *primary, const ios_u8 *backup)
{
    if (memcmp(primary, inference_fs_magic, sizeof(inference_fs_magic)) != 0
        || memcmp(primary, backup, IOS_BLOCK_SECTOR_SIZE) != 0
        || read_le16(primary + 8) != 1 || read_le16(primary + 12) != IOS_BLOCK_SECTOR_SIZE
        || read_le16(primary + 510) != 0xaa55) {
        return false;
    }
    return crc32_zeroed_field(primary, IOS_BLOCK_SECTOR_SIZE, 0x48, 4)
        == read_le32(primary + 0x48);
}

static bool mbr_has_partition(const ios_u8 sector[IOS_BLOCK_SECTOR_SIZE])
{
    if (sector[510] != 0x55 || sector[511] != 0xaa) return false;
    for (ios_size index = 0; index < 4; ++index) {
        const ios_u8 *entry = sector + 446 + index * 16;
        if (entry[4] != 0 || read_le32(entry + 8) != 0 || read_le32(entry + 12) != 0) {
            return true;
        }
    }
    return false;
}

static ios_status classify_gpt(
    struct ios_block_device *device, const ios_u8 header[IOS_BLOCK_SECTOR_SIZE],
    const ios_u8 boot_guid[16], enum ios_block_disk_classification *classification
)
{
    ios_u8 sector[IOS_BLOCK_SECTOR_SIZE];
    const ios_u32 header_size = read_le32(header + 12);
    const ios_u64 entries_lba = read_le64(header + 72);
    const ios_u32 entry_count = read_le32(header + 80);
    const ios_u32 entry_size = read_le32(header + 84);
    ios_u64 entries_bytes;

    *classification = IOS_BLOCK_DISK_PROTECTED_PARTITIONED;
    if (header_size < GPT_MINIMUM_HEADER_SIZE || header_size > IOS_BLOCK_SECTOR_SIZE
        || read_le64(header + 24) != GPT_HEADER_LBA
        || read_le64(header + 32) >= device->sector_count
        || crc32_zeroed_field(header, header_size, 16, 4) != read_le32(header + 16)
        || entry_count == 0 || entry_size < GPT_PARTITION_ENTRY_MINIMUM_SIZE
        || (entry_size & 7U) != 0 || entry_count > UINT64_MAX / entry_size) {
        return IOS_ERROR(IOS_E_CORRUPT);
    }
    entries_bytes = (ios_u64)entry_count * entry_size;
    if (entries_lba >= device->sector_count
        || (entries_bytes + IOS_BLOCK_SECTOR_SIZE - 1U) / IOS_BLOCK_SECTOR_SIZE
            > device->sector_count - entries_lba) {
        return IOS_ERROR(IOS_E_CORRUPT);
    }
    if (guid_is_zero(boot_guid)) {
        return IOS_OK;
    }
    for (ios_u32 index = 0; index < entry_count; ++index) {
        const ios_u64 byte_offset = (ios_u64)index * entry_size;
        const ios_u64 lba = entries_lba + byte_offset / IOS_BLOCK_SECTOR_SIZE;
        const ios_size offset = (ios_size)(byte_offset % IOS_BLOCK_SECTOR_SIZE);
        ios_u8 unique_guid[16];
        ios_status status;

        if (offset + 32 <= IOS_BLOCK_SECTOR_SIZE) {
            status = block_device_read(device, lba, 1, sector);
            if (IOS_FAILED(status)) return status;
            memcpy(unique_guid, sector + offset + 16, sizeof(unique_guid));
        } else {
            ios_u8 pair[IOS_BLOCK_SECTOR_SIZE * 2];
            status = block_device_read(device, lba, 2, pair);
            if (IOS_FAILED(status)) return status;
            memcpy(unique_guid, pair + offset + 16, sizeof(unique_guid));
        }
        if (memcmp(unique_guid, boot_guid, sizeof(unique_guid)) == 0) {
            *classification = IOS_BLOCK_DISK_PROTECTED_BOOT;
            return IOS_OK;
        }
    }
    return IOS_OK;
}

static ios_status probe_zero_region(
    struct ios_block_device *device, ios_u64 first_sector, ios_u64 sector_count,
    bool *all_zero
)
{
    ios_u8 sectors[DISK_PROBE_BATCH_SECTORS * IOS_BLOCK_SECTOR_SIZE];

    *all_zero = true;
    while (sector_count != 0) {
        ios_size batch = sector_count > DISK_PROBE_BATCH_SECTORS
            ? DISK_PROBE_BATCH_SECTORS : (ios_size)sector_count;
        ios_status status = block_device_read(device, first_sector, batch, sectors);
        if (IOS_FAILED(status)) return status;
        if (!bytes_are_zero(sectors, batch * IOS_BLOCK_SECTOR_SIZE)) {
            *all_zero = false;
            return IOS_OK;
        }
        first_sector += batch;
        sector_count -= batch;
    }
    return IOS_OK;
}

ios_status block_classify_data_disk(
    struct ios_block_device *device,
    const ios_u8 boot_partition_guid[16],
    enum ios_block_disk_classification *classification
)
{
    ios_u8 first_two[IOS_BLOCK_SECTOR_SIZE * 2];
    bool start_zero;
    bool end_zero;
    ios_u64 probe_sectors;
    ios_status status;

    if (device == NULL || boot_partition_guid == NULL || classification == NULL
        || device->logical_sector_size != IOS_BLOCK_SECTOR_SIZE || device->sector_count < 2) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *classification = IOS_BLOCK_DISK_REJECTED_IO;
    status = block_device_read(device, 0, 2, first_two);
    if (IOS_FAILED(status)) return status;
    if (inference_fs_superblocks_valid(first_two, first_two + IOS_BLOCK_SECTOR_SIZE)) {
        *classification = IOS_BLOCK_DISK_ELIGIBLE_INFERENCE_FS;
        return IOS_OK;
    }
    if (memcmp(first_two + IOS_BLOCK_SECTOR_SIZE, "EFI PART", 8) == 0) {
        status = classify_gpt(device, first_two + IOS_BLOCK_SECTOR_SIZE,
            boot_partition_guid, classification);
        /* A malformed GPT remains protected; return success after fail-closed classification. */
        return status == IOS_ERROR(IOS_E_CORRUPT) ? IOS_OK : status;
    }
    if (mbr_has_partition(first_two)) {
        *classification = IOS_BLOCK_DISK_PROTECTED_PARTITIONED;
        return IOS_OK;
    }
    probe_sectors = device->sector_count < DISK_PROBE_SECTORS
        ? device->sector_count : DISK_PROBE_SECTORS;
    status = probe_zero_region(device, 0, probe_sectors, &start_zero);
    if (IOS_FAILED(status)) return status;
    status = probe_zero_region(device, device->sector_count - probe_sectors,
        probe_sectors, &end_zero);
    if (IOS_FAILED(status)) return status;
    *classification = start_zero && end_zero
        ? IOS_BLOCK_DISK_ELIGIBLE_BLANK : IOS_BLOCK_DISK_PROTECTED_FOREIGN;
    return IOS_OK;
}
