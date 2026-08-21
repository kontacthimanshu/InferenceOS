#include <inferencefs/fat.h>
#include <inferencefs/format.h>
#include <inferenceos/endian.h>

inferenceos_result inferencefs_fat_initialize(
    inferencefs_fat *fat,
    const inferenceos_block_device *device,
    const inferencefs_geometry *geometry
)
{
    if (fat == NULL || device == NULL || geometry == NULL) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    fat->device = device;
    fat->geometry = *geometry;
    return INFERENCEOS_RESULT_OK;
}

bool inferencefs_fat_cluster_is_valid(
    const inferencefs_fat *fat,
    inferenceos_u32 cluster
)
{
    inferenceos_u64 limit;

    return fat != NULL
        && inferenceos_checked_add_u64(
            fat->geometry.data_cluster_count, 2U, &limit)
        && cluster >= 2U && cluster < limit;
}

inferenceos_result inferencefs_fat_read(
    const inferencefs_fat *fat,
    inferenceos_u32 entry,
    inferenceos_u32 *value
)
{
    inferenceos_u8 sector[INFERENCEFS_SUPERBLOCK_SIZE];
    inferenceos_u64 byte_offset;
    inferenceos_u64 lba;
    inferenceos_block_outcome read;

    if (fat == NULL || fat->device == NULL || value == NULL) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    if (!inferenceos_checked_mul_u64(entry, 4U, &byte_offset)) {
        return INFERENCEOS_RESULT_OVERFLOW;
    }
    lba = INFERENCEFS_RESERVED_SECTORS
        + (byte_offset / INFERENCEFS_LOGICAL_SECTOR_SIZE);
    if (lba >= (inferenceos_u64)INFERENCEFS_RESERVED_SECTORS
            + fat->geometry.sectors_per_fat) {
        return INFERENCEOS_RESULT_OUT_OF_RANGE;
    }
    read = inferenceos_block_read(fat->device, lba, 1U, sector);
    if (!inferenceos_block_outcome_is_success(read)
        || read.sectors_completed != 1U) {
        return inferenceos_result_is_success(read.result)
            ? INFERENCEOS_RESULT_IO_ERROR : read.result;
    }
    *value = inferenceos_load_le32(
        sector + (inferenceos_size)(byte_offset % INFERENCEFS_LOGICAL_SECTOR_SIZE));
    return INFERENCEOS_RESULT_OK;
}

inferencefs_fat_value_kind inferencefs_fat_classify(
    const inferencefs_fat *fat,
    inferenceos_u32 value
)
{
    if ((value & UINT32_C(0xF0000000)) != 0U) {
        return INFERENCEFS_FAT_VALUE_INVALID;
    }
    if (value == 0U) {
        return INFERENCEFS_FAT_VALUE_FREE;
    }
    if (value == UINT32_C(0x0FFFFFF7)) {
        return INFERENCEFS_FAT_VALUE_BAD;
    }
    if (value >= UINT32_C(0x0FFFFFF8)
        && value <= UINT32_C(0x0FFFFFFF)) {
        return INFERENCEFS_FAT_VALUE_END;
    }
    return inferencefs_fat_cluster_is_valid(fat, value)
        ? INFERENCEFS_FAT_VALUE_NEXT : INFERENCEFS_FAT_VALUE_INVALID;
}

inferenceos_result inferencefs_fat_walk(
    const inferencefs_fat *fat,
    inferenceos_u32 first_cluster,
    inferencefs_fat_visit_fn visit,
    void *context,
    inferenceos_u32 *cluster_count
)
{
    inferenceos_u32 current = first_cluster;

    if (fat == NULL || visit == NULL || cluster_count == NULL
        || !inferencefs_fat_cluster_is_valid(fat, first_cluster)) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    for (inferenceos_u32 index = 0U;
         index < fat->geometry.data_cluster_count;
         ++index) {
        inferenceos_u32 next;
        inferenceos_result result = visit(context, current, index);
        inferencefs_fat_value_kind kind;

        if (!inferenceos_result_is_success(result)) {
            return result;
        }
        result = inferencefs_fat_read(fat, current, &next);
        if (!inferenceos_result_is_success(result)) {
            return result;
        }
        kind = inferencefs_fat_classify(fat, next);
        if (kind == INFERENCEFS_FAT_VALUE_END) {
            *cluster_count = index + 1U;
            return INFERENCEOS_RESULT_OK;
        }
        if (kind != INFERENCEFS_FAT_VALUE_NEXT) {
            return INFERENCEOS_RESULT_CORRUPT;
        }
        current = next;
    }
    return INFERENCEOS_RESULT_CORRUPT;
}

inferenceos_result inferencefs_cluster_first_lba(
    const inferencefs_fat *fat,
    inferenceos_u32 cluster,
    inferenceos_u64 *lba
)
{
    inferenceos_u64 relative;
    inferenceos_u64 first;

    if (fat == NULL || lba == NULL
        || !inferencefs_fat_cluster_is_valid(fat, cluster)
        || !inferenceos_checked_mul_u64(
            cluster - 2U, INFERENCEFS_SECTORS_PER_CLUSTER, &relative)
        || !inferenceos_checked_add_u64(
            fat->geometry.data_start_lba, relative, &first)
        || !inferenceos_range_within_u64(
            first, INFERENCEFS_SECTORS_PER_CLUSTER,
            fat->geometry.total_sectors)) {
        return INFERENCEOS_RESULT_OUT_OF_RANGE;
    }
    *lba = first;
    return INFERENCEOS_RESULT_OK;
}
