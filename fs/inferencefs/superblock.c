#include <inferencefs/format.h>
#include <inferencefs/superblock.h>
#include <inferenceos/crc32.h>
#include <inferenceos/endian.h>
#include <inferenceos/memory.h>

#define INFERENCEFS_MINIMUM_SECTORS UINT32_C(32768)
#define INFERENCEFS_MAXIMUM_SECTORS UINT32_C(2097152)
#define INFERENCEFS_FAT_ENTRY_SIZE UINT32_C(4)

static inferencefs_superblock_outcome outcome(
    inferenceos_result result,
    inferencefs_superblock_error error,
    inferenceos_u32 stored_crc,
    inferenceos_u32 computed_crc
)
{
    const inferencefs_superblock_outcome value = {
        .result = result,
        .error = error,
        .stored_crc32 = stored_crc,
        .computed_crc32 = computed_crc
    };
    return value;
}

static inferenceos_u32 superblock_crc(const inferenceos_u8 *bytes)
{
    static const inferenceos_u8 zero_crc[4] = { 0U, 0U, 0U, 0U };
    inferenceos_u32 state = inferenceos_crc32_begin();

    state = inferenceos_crc32_update(state, bytes, 0x38U);
    state = inferenceos_crc32_update(state, zero_crc, sizeof(zero_crc));
    state = inferenceos_crc32_update(state, bytes + 0x3CU, 4U);
    return inferenceos_crc32_finish(state);
}

static bool bytes_are_zero(const inferenceos_u8 *bytes, inferenceos_size size)
{
    for (inferenceos_size index = 0U; index < size; ++index) {
        if (bytes[index] != 0U) {
            return false;
        }
    }
    return true;
}

static bool label_is_canonical(const inferenceos_u8 label[11])
{
    bool padding = false;

    for (inferenceos_size index = 0U; index < 11U; ++index) {
        const inferenceos_u8 byte = label[index];

        if (byte == (inferenceos_u8)' ') {
            padding = true;
        } else if (padding || byte < (inferenceos_u8)'!'
            || byte > (inferenceos_u8)'~'
            || (byte >= (inferenceos_u8)'a'
                && byte <= (inferenceos_u8)'z')) {
            return false;
        }
    }
    return true;
}

static bool derive_geometry(
    inferenceos_u32 total_sectors,
    inferenceos_u32 sectors_per_fat,
    inferencefs_geometry *geometry
)
{
    inferenceos_u64 data_start;
    inferenceos_u64 data_sectors;
    inferenceos_u64 data_clusters;
    inferenceos_u64 fat_entries;
    inferenceos_u64 fat_bytes;
    inferenceos_u64 required_fat_sectors;

    if (sectors_per_fat == 0U
        || !inferenceos_checked_add_u64(
            INFERENCEFS_RESERVED_SECTORS,
            sectors_per_fat,
            &data_start)
        || data_start >= total_sectors
        || !inferenceos_checked_sub_u64(
            total_sectors, data_start, &data_sectors)) {
        return false;
    }
    data_clusters = data_sectors / INFERENCEFS_SECTORS_PER_CLUSTER;
    if (data_clusters == 0U
        || !inferenceos_checked_add_u64(data_clusters, 2U, &fat_entries)
        || !inferenceos_checked_mul_u64(
            fat_entries, INFERENCEFS_FAT_ENTRY_SIZE, &fat_bytes)
        || !inferenceos_checked_add_u64(
            fat_bytes, INFERENCEFS_LOGICAL_SECTOR_SIZE - 1U, &fat_bytes)) {
        return false;
    }
    required_fat_sectors = fat_bytes / INFERENCEFS_LOGICAL_SECTOR_SIZE;
    if (required_fat_sectors != sectors_per_fat
        || data_start > UINT32_MAX || data_clusters > UINT32_MAX) {
        return false;
    }
    geometry->total_sectors = total_sectors;
    geometry->sectors_per_fat = sectors_per_fat;
    geometry->data_start_lba = (inferenceos_u32)data_start;
    geometry->data_cluster_count = (inferenceos_u32)data_clusters;
    return true;
}

inferencefs_superblock_outcome inferencefs_superblock_decode(
    const void *sector,
    inferenceos_size sector_size,
    inferenceos_u64 device_sector_count,
    inferencefs_superblock *decoded
)
{
    static const inferenceos_u8 magic[8] = {
        'I', 'N', 'F', 'F', 'A', 'T', '3', '2'
    };
    const inferencefs_superblock_disk *disk = sector;
    const inferenceos_u8 *bytes = sector;
    inferencefs_superblock value;
    inferenceos_u32 stored_crc;
    inferenceos_u32 computed_crc;
    inferenceos_u32 total_sectors;
    inferenceos_u32 sectors_per_fat;

    if (sector == NULL || decoded == NULL
        || sector_size != INFERENCEFS_SUPERBLOCK_SIZE) {
        return outcome(
            INFERENCEOS_RESULT_INVALID_ARGUMENT,
            INFERENCEFS_SUPERBLOCK_ERROR_ARGUMENT, 0U, 0U);
    }
    if (memcmp(disk->magic, magic, sizeof(magic)) != 0) {
        return outcome(
            INFERENCEOS_RESULT_CORRUPT,
            INFERENCEFS_SUPERBLOCK_ERROR_MAGIC, 0U, 0U);
    }
    stored_crc = inferenceos_load_le32(disk->superblock_crc32_le);
    computed_crc = superblock_crc(bytes);
    if (stored_crc != computed_crc) {
        return outcome(
            INFERENCEOS_RESULT_CORRUPT,
            INFERENCEFS_SUPERBLOCK_ERROR_CRC, stored_crc, computed_crc);
    }
    if (inferenceos_load_le16(disk->format_version_le)
        != INFERENCEFS_FORMAT_VERSION) {
        return outcome(
            INFERENCEOS_RESULT_UNSUPPORTED,
            INFERENCEFS_SUPERBLOCK_ERROR_VERSION, stored_crc, computed_crc);
    }
    if (inferenceos_load_le16(disk->header_size_le)
            != INFERENCEFS_SUPERBLOCK_HEADER_SIZE
        || inferenceos_load_le16(disk->bytes_per_sector_le)
            != INFERENCEFS_LOGICAL_SECTOR_SIZE
        || disk->sectors_per_cluster != INFERENCEFS_SECTORS_PER_CLUSTER
        || disk->fat_count != INFERENCEFS_FAT_COUNT
        || inferenceos_load_le16(disk->reserved_sectors_le)
            != INFERENCEFS_RESERVED_SECTORS
        || inferenceos_load_le16(disk->directory_entry_size_le)
            != INFERENCEFS_DIRECTORY_RECORD_SIZE
        || inferenceos_load_le16(disk->hash_entry_size_le)
            != INFERENCEFS_COMPANION_RECORD_SIZE
        || inferenceos_load_le16(disk->flags_le) != 0U
        || inferenceos_load_le32(disk->root_cluster_le)
            != INFERENCEFS_ROOT_CLUSTER
        || disk->hash_algorithm_id != INFERENCEFS_HASH_ALGORITHM_FNV1A32
        || inferenceos_load_le16(disk->companion_record_version_le)
            != INFERENCEFS_COMPANION_RECORD_VERSION
        || inferenceos_load_le16(disk->primary_record_version_le)
            != INFERENCEFS_PRIMARY_RECORD_VERSION) {
        return outcome(
            INFERENCEOS_RESULT_CORRUPT,
            INFERENCEFS_SUPERBLOCK_ERROR_FIXED_FIELD,
            stored_crc, computed_crc);
    }
    if (!label_is_canonical(disk->volume_label)) {
        return outcome(
            INFERENCEOS_RESULT_CORRUPT,
            INFERENCEFS_SUPERBLOCK_ERROR_LABEL, stored_crc, computed_crc);
    }
    if (!bytes_are_zero(disk->reserved_header, sizeof(disk->reserved_header))
        || !bytes_are_zero(disk->reserved, sizeof(disk->reserved))) {
        return outcome(
            INFERENCEOS_RESULT_CORRUPT,
            INFERENCEFS_SUPERBLOCK_ERROR_RESERVED, stored_crc, computed_crc);
    }
    if (inferenceos_load_le16(disk->trailer_signature_le)
        != INFERENCEFS_SUPERBLOCK_TRAILER_SIGNATURE) {
        return outcome(
            INFERENCEOS_RESULT_CORRUPT,
            INFERENCEFS_SUPERBLOCK_ERROR_TRAILER, stored_crc, computed_crc);
    }
    total_sectors = inferenceos_load_le32(disk->total_sectors_le);
    if (device_sector_count > UINT32_MAX
        || total_sectors != device_sector_count
        || total_sectors < INFERENCEFS_MINIMUM_SECTORS
        || total_sectors > INFERENCEFS_MAXIMUM_SECTORS) {
        return outcome(
            INFERENCEOS_RESULT_INCONSISTENT,
            INFERENCEFS_SUPERBLOCK_ERROR_CAPACITY, stored_crc, computed_crc);
    }
    sectors_per_fat = inferenceos_load_le32(disk->sectors_per_fat_le);
    if (!derive_geometry(
            total_sectors, sectors_per_fat, &value.geometry)) {
        return outcome(
            INFERENCEOS_RESULT_CORRUPT,
            INFERENCEFS_SUPERBLOCK_ERROR_GEOMETRY, stored_crc, computed_crc);
    }

    value.root_cluster = inferenceos_load_le32(disk->root_cluster_le);
    value.volume_serial = inferenceos_load_le32(disk->volume_serial_le);
    (void)memcpy(value.volume_label, disk->volume_label, sizeof(value.volume_label));
    value.hash_algorithm_id = disk->hash_algorithm_id;
    value.companion_record_version = inferenceos_load_le16(
        disk->companion_record_version_le);
    value.primary_record_version = inferenceos_load_le16(
        disk->primary_record_version_le);
    *decoded = value;
    return outcome(
        INFERENCEOS_RESULT_OK,
        INFERENCEFS_SUPERBLOCK_ERROR_NONE, stored_crc, computed_crc);
}

bool inferencefs_superblock_equal(
    const inferencefs_superblock *left,
    const inferencefs_superblock *right
)
{
    if (left == NULL || right == NULL) {
        return false;
    }
    return left->geometry.total_sectors == right->geometry.total_sectors
        && left->geometry.sectors_per_fat == right->geometry.sectors_per_fat
        && left->geometry.data_start_lba == right->geometry.data_start_lba
        && left->geometry.data_cluster_count
            == right->geometry.data_cluster_count
        && left->root_cluster == right->root_cluster
        && left->volume_serial == right->volume_serial
        && memcmp(
            left->volume_label,
            right->volume_label,
            sizeof(left->volume_label)) == 0
        && left->hash_algorithm_id == right->hash_algorithm_id
        && left->companion_record_version == right->companion_record_version
        && left->primary_record_version == right->primary_record_version;
}
