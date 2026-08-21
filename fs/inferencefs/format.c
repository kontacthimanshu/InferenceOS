#include <inferencefs/format.h>
#include <inferencefs/formatter.h>
#include <inferenceos/crc32.h>
#include <inferenceos/endian.h>
#include <inferenceos/memory.h>

#define INFERENCEFS_MINIMUM_SECTORS UINT32_C(32768)
#define INFERENCEFS_MAXIMUM_SECTORS UINT32_C(2097152)
#define INFERENCEFS_FAT_ENTRY_SIZE UINT32_C(4)
#define INFERENCEFS_FAT_RESERVED_ZERO UINT32_C(0x0FFFFFF8)
#define INFERENCEFS_FAT_RESERVED_ONE UINT32_C(0x0FFFFFFF)
#define INFERENCEFS_FAT_ROOT_EOC UINT32_C(0x0FFFFFFF)

inferenceos_result inferencefs_geometry_solve(
    inferenceos_u32 logical_sector_size,
    inferenceos_u64 sector_count,
    inferencefs_geometry *geometry
)
{
    inferencefs_geometry solved;
    inferenceos_u64 fat_sectors = 1U;
    inferenceos_u64 data_clusters = 0U;

    if (geometry == NULL) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    if (logical_sector_size != INFERENCEFS_LOGICAL_SECTOR_SIZE) {
        return INFERENCEOS_RESULT_UNSUPPORTED;
    }
    if (sector_count > UINT32_MAX) {
        return INFERENCEOS_RESULT_OVERFLOW;
    }
    if (sector_count < INFERENCEFS_MINIMUM_SECTORS
        || sector_count > INFERENCEFS_MAXIMUM_SECTORS) {
        return INFERENCEOS_RESULT_OUT_OF_RANGE;
    }

    for (;;) {
        inferenceos_u64 non_data_sectors;
        inferenceos_u64 fat_entries;
        inferenceos_u64 fat_bytes;
        inferenceos_u64 required_fat_sectors;

        if (!inferenceos_checked_add_u64(
                INFERENCEFS_RESERVED_SECTORS,
                fat_sectors,
                &non_data_sectors)
            || non_data_sectors >= sector_count) {
            return INFERENCEOS_RESULT_OVERFLOW;
        }
        data_clusters = (sector_count - non_data_sectors)
            / INFERENCEFS_SECTORS_PER_CLUSTER;
        if (!inferenceos_checked_add_u64(data_clusters, 2U, &fat_entries)
            || !inferenceos_checked_mul_u64(
                fat_entries, INFERENCEFS_FAT_ENTRY_SIZE, &fat_bytes)
            || !inferenceos_checked_add_u64(
                fat_bytes,
                INFERENCEFS_LOGICAL_SECTOR_SIZE - 1U,
                &fat_bytes)) {
            return INFERENCEOS_RESULT_OVERFLOW;
        }
        required_fat_sectors = fat_bytes / INFERENCEFS_LOGICAL_SECTOR_SIZE;
        if (required_fat_sectors == fat_sectors) {
            break;
        }
        fat_sectors = required_fat_sectors;
    }

    if (data_clusters == 0U || fat_sectors == 0U
        || data_clusters > UINT32_MAX || fat_sectors > UINT32_MAX) {
        return INFERENCEOS_RESULT_OVERFLOW;
    }
    solved.total_sectors = (inferenceos_u32)sector_count;
    solved.sectors_per_fat = (inferenceos_u32)fat_sectors;
    solved.data_start_lba = (inferenceos_u32)(
        (inferenceos_u64)INFERENCEFS_RESERVED_SECTORS + fat_sectors);
    solved.data_cluster_count = (inferenceos_u32)data_clusters;
    *geometry = solved;
    return INFERENCEOS_RESULT_OK;
}

static bool label_is_valid(const inferenceos_u8 label[11])
{
    bool padding = false;

    if (label == NULL) {
        return false;
    }
    for (inferenceos_size index = 0U; index < 11U; ++index) {
        const inferenceos_u8 byte = label[index];

        if (byte == (inferenceos_u8)' ') {
            padding = true;
            continue;
        }
        if (padding || !((byte >= (inferenceos_u8)'A'
                    && byte <= (inferenceos_u8)'Z')
                || (byte >= (inferenceos_u8)'0'
                    && byte <= (inferenceos_u8)'9')
                || byte == (inferenceos_u8)'_'
                || byte == (inferenceos_u8)'-')) {
            return false;
        }
    }
    return true;
}

static inferenceos_result block_write_exact(
    const inferenceos_block_device *device,
    inferenceos_u64 lba,
    const void *sector
)
{
    const inferenceos_block_outcome outcome = inferenceos_block_write(
        device, lba, 1U, sector);

    if (!inferenceos_block_outcome_is_success(outcome)) {
        return outcome.result;
    }
    return outcome.sectors_completed == 1U
        ? INFERENCEOS_RESULT_OK
        : INFERENCEOS_RESULT_IO_ERROR;
}

static inferenceos_result block_flush_exact(
    const inferenceos_block_device *device
)
{
    const inferenceos_block_outcome outcome = inferenceos_block_flush(device);
    return inferenceos_block_outcome_is_success(outcome)
        ? INFERENCEOS_RESULT_OK
        : outcome.result;
}

static void build_superblock(
    inferencefs_superblock_disk *superblock,
    const inferencefs_geometry *geometry,
    inferenceos_u32 volume_serial,
    const inferenceos_u8 label[11]
)
{
    static const inferenceos_u8 magic[8] = {
        'I', 'N', 'F', 'F', 'A', 'T', '3', '2'
    };

    (void)memset(superblock, 0, sizeof(*superblock));
    (void)memcpy(superblock->magic, magic, sizeof(magic));
    inferenceos_store_le16(
        superblock->format_version_le, INFERENCEFS_FORMAT_VERSION);
    inferenceos_store_le16(
        superblock->header_size_le, INFERENCEFS_SUPERBLOCK_HEADER_SIZE);
    inferenceos_store_le16(
        superblock->bytes_per_sector_le, INFERENCEFS_LOGICAL_SECTOR_SIZE);
    superblock->sectors_per_cluster = INFERENCEFS_SECTORS_PER_CLUSTER;
    superblock->fat_count = INFERENCEFS_FAT_COUNT;
    inferenceos_store_le16(
        superblock->reserved_sectors_le, INFERENCEFS_RESERVED_SECTORS);
    inferenceos_store_le16(
        superblock->directory_entry_size_le,
        INFERENCEFS_DIRECTORY_RECORD_SIZE);
    inferenceos_store_le16(
        superblock->hash_entry_size_le, INFERENCEFS_COMPANION_RECORD_SIZE);
    inferenceos_store_le16(superblock->flags_le, 0U);
    inferenceos_store_le32(
        superblock->total_sectors_le, geometry->total_sectors);
    inferenceos_store_le32(
        superblock->sectors_per_fat_le, geometry->sectors_per_fat);
    inferenceos_store_le32(
        superblock->root_cluster_le, INFERENCEFS_ROOT_CLUSTER);
    inferenceos_store_le32(superblock->volume_serial_le, volume_serial);
    (void)memcpy(superblock->volume_label, label, 11U);
    superblock->hash_algorithm_id = INFERENCEFS_HASH_ALGORITHM_FNV1A32;
    inferenceos_store_le16(
        superblock->companion_record_version_le,
        INFERENCEFS_COMPANION_RECORD_VERSION);
    inferenceos_store_le16(
        superblock->primary_record_version_le,
        INFERENCEFS_PRIMARY_RECORD_VERSION);
    inferenceos_store_le32(superblock->superblock_crc32_le, 0U);
    inferenceos_store_le16(
        superblock->trailer_signature_le,
        INFERENCEFS_SUPERBLOCK_TRAILER_SIGNATURE);
    inferenceos_store_le32(
        superblock->superblock_crc32_le,
        inferenceos_crc32(superblock, INFERENCEFS_SUPERBLOCK_CRC_LENGTH));
}

static inferenceos_result write_zero_sectors(
    const inferenceos_block_device *device,
    inferenceos_u64 first_lba,
    inferenceos_u32 sector_count,
    const inferenceos_u8 zero_sector[INFERENCEFS_SUPERBLOCK_SIZE]
)
{
    for (inferenceos_u32 sector = 0U; sector < sector_count; ++sector) {
        const inferenceos_result result = block_write_exact(
            device, first_lba + sector, zero_sector);
        if (!inferenceos_result_is_success(result)) {
            return result;
        }
    }
    return INFERENCEOS_RESULT_OK;
}

inferenceos_result inferencefs_format_device(
    const inferenceos_block_device *device,
    inferenceos_u32 volume_serial,
    const inferenceos_u8 label[11],
    inferencefs_geometry *geometry
)
{
    inferenceos_block_info info;
    inferenceos_block_outcome query;
    inferencefs_geometry solved;
    inferencefs_superblock_disk superblock;
    inferenceos_u8 sector[INFERENCEFS_SUPERBLOCK_SIZE];
    inferenceos_result result;

    if (device == NULL || geometry == NULL || !label_is_valid(label)) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    query = inferenceos_block_query(device, &info);
    if (!inferenceos_block_outcome_is_success(query)) {
        return query.result;
    }
    if (info.status == INFERENCEOS_BLOCK_STATUS_READ_ONLY) {
        return INFERENCEOS_RESULT_READ_ONLY;
    }
    if (info.status != INFERENCEOS_BLOCK_STATUS_READY) {
        return INFERENCEOS_RESULT_NOT_READY;
    }
    result = inferencefs_geometry_solve(
        info.geometry.logical_sector_size,
        info.geometry.sector_count,
        &solved);
    if (!inferenceos_result_is_success(result)) {
        return result;
    }
    build_superblock(&superblock, &solved, volume_serial, label);

    /* Remove both publication records first. If formatting is interrupted,
     * neither stale superblock can describe the newly initialized metadata. */
    (void)memset(sector, 0, sizeof(sector));
    result = write_zero_sectors(device, 0U, INFERENCEFS_RESERVED_SECTORS, sector);
    if (!inferenceos_result_is_success(result)) {
        return result;
    }
    result = block_flush_exact(device);
    if (!inferenceos_result_is_success(result)) {
        return result;
    }

    result = write_zero_sectors(
        device,
        INFERENCEFS_RESERVED_SECTORS,
        solved.sectors_per_fat,
        sector);
    if (!inferenceos_result_is_success(result)) {
        return result;
    }
    inferenceos_store_le32(sector + 0U, INFERENCEFS_FAT_RESERVED_ZERO);
    inferenceos_store_le32(sector + 4U, INFERENCEFS_FAT_RESERVED_ONE);
    inferenceos_store_le32(sector + 8U, INFERENCEFS_FAT_ROOT_EOC);
    result = block_write_exact(device, INFERENCEFS_RESERVED_SECTORS, sector);
    if (!inferenceos_result_is_success(result)) {
        return result;
    }
    (void)memset(sector, 0, sizeof(sector));
    result = write_zero_sectors(
        device,
        solved.data_start_lba,
        INFERENCEFS_SECTORS_PER_CLUSTER,
        sector);
    if (!inferenceos_result_is_success(result)) {
        return result;
    }

    result = block_write_exact(device, 1U, &superblock);
    if (!inferenceos_result_is_success(result)) {
        return result;
    }
    result = block_flush_exact(device);
    if (!inferenceos_result_is_success(result)) {
        return result;
    }
    result = block_write_exact(device, 0U, &superblock);
    if (!inferenceos_result_is_success(result)) {
        return result;
    }
    result = block_flush_exact(device);
    if (!inferenceos_result_is_success(result)) {
        return result;
    }
    *geometry = solved;
    return INFERENCEOS_RESULT_OK;
}
