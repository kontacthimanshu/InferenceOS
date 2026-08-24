#include <inferenceos/fs/format.h>

#include <inferenceos/runtime.h>

static void write_u16(ios_u8 *bytes, ios_u16 value)
{
    *bytes = (ios_u8)value;
    bytes[1] = (ios_u8)(value >> 8);
}
static void write_u32(ios_u8 *bytes, ios_u32 value)
{
    for (ios_size index = 0; index < 4; ++index) bytes[index] = (ios_u8)(value >> (index * 8));
}
static void write_u64(ios_u8 *bytes, ios_u64 value)
{
    for (ios_size index = 0; index < 8; ++index) bytes[index] = (ios_u8)(value >> (index * 8));
}
static ios_u16 read_u16(const ios_u8 *bytes)
{
    return (ios_u16)((ios_u16)*bytes | (ios_u16)((ios_u16)bytes[1] << 8));
}
static ios_u32 read_u32(const ios_u8 *bytes)
{
    ios_u32 value = 0;
    for (ios_size index = 0; index < 4; ++index) value |= (ios_u32)bytes[index] << (index * 8);
    return value;
}
static ios_u64 read_u64(const ios_u8 *bytes)
{
    ios_u64 value = 0;
    for (ios_size index = 0; index < 8; ++index) value |= (ios_u64)bytes[index] << (index * 8);
    return value;
}
static bool all_zero(const ios_u8 *bytes, ios_size count)
{
    for (ios_size index = 0; index < count; ++index) if (bytes[index] != 0) return false;
    return true;
}
static bool valid_label(const ios_u8 *label)
{
    for (ios_size index = 0; index < IOS_FS_VOLUME_LABEL_SIZE; ++index) {
        if (label[index] != ' ' && (label[index] < 'A' || label[index] > 'Z')
            && (label[index] < '0' || label[index] > '9') && label[index] != '_') return false;
    }
    return true;
}

static ios_u32 superblock_crc(const struct ios_fs_superblock_disk *disk)
{
    const ios_u8 *bytes = (const ios_u8 *)disk;
    ios_u32 crc = UINT32_C(0xffffffff);
    for (ios_size index = 0; index < sizeof(*disk); ++index) {
        ios_u8 value = index >= 0x48 && index < 0x4c ? 0 : bytes[index];
        crc ^= value;
        for (ios_u32 bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1) ? UINT32_C(0xedb88320) : 0);
        }
    }
    return crc ^ UINT32_C(0xffffffff);
}

ios_status ios_fs_superblock_encode(
    const struct ios_fs_superblock *values, struct ios_fs_superblock_disk *disk)
{
    if (values == NULL || disk == NULL || values->total_sectors == 0
        || values->sectors_per_fat == 0 || values->flags != 0
        || values->registry_start_sector != IOS_FS_SUPERBLOCK_SECTORS + values->sectors_per_fat
        || values->registry_start_sector > UINT64_MAX - IOS_FS_REGISTRY_SECTORS
        || values->registry_start_sector + IOS_FS_REGISTRY_SECTORS >= values->total_sectors
        || !valid_label(values->volume_label)) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    memset(disk, 0, sizeof(*disk));
    memcpy(disk->magic, IOS_FS_MAGIC, sizeof(disk->magic));
    write_u16(disk->format_version, IOS_FS_FORMAT_VERSION);
    write_u16(disk->header_size, IOS_FS_SUPERBLOCK_HEADER_SIZE);
    write_u16(disk->bytes_per_sector, IOS_FS_SECTOR_SIZE);
    disk->sectors_per_cluster = IOS_FS_SECTORS_PER_CLUSTER;
    disk->fat_count = 1;
    write_u16(disk->reserved_superblock_sectors, IOS_FS_SUPERBLOCK_SECTORS);
    write_u16(disk->primary_record_size, IOS_FS_PRIMARY_RECORD_SIZE);
    write_u16(disk->companion_record_size, IOS_FS_COMPANION_RECORD_SIZE);
    write_u16(disk->flags, values->flags);
    write_u64(disk->total_sectors, values->total_sectors);
    write_u32(disk->sectors_per_fat, values->sectors_per_fat);
    write_u32(disk->root_cluster, IOS_FS_ROOT_CLUSTER);
    write_u64(disk->registry_start_sector, values->registry_start_sector);
    write_u32(disk->registry_sector_count, IOS_FS_REGISTRY_SECTORS);
    write_u32(disk->volume_serial, values->volume_serial);
    memcpy(disk->volume_label, values->volume_label, sizeof(disk->volume_label));
    disk->hash_algorithm_id = IOS_FS_HASH_FNV1A32;
    write_u16(disk->companion_record_version, IOS_FS_COMPANION_VERSION);
    write_u16(disk->registry_record_version, IOS_FS_REGISTRY_VERSION);
    write_u16(disk->trailer_signature, IOS_FS_SUPERBLOCK_TRAILER);
    write_u32(disk->crc32, superblock_crc(disk));
    return IOS_OK;
}

ios_status ios_fs_superblock_decode(
    const struct ios_fs_superblock_disk *disk, struct ios_fs_superblock *values)
{
    ios_u64 total_sectors;
    ios_u64 registry_start;
    ios_u32 fat_sectors;
    if (disk == NULL || values == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    memset(values, 0, sizeof(*values));
    if (superblock_crc(disk) != read_u32(disk->crc32)) return IOS_ERROR(IOS_E_CORRUPT);
    total_sectors = read_u64(disk->total_sectors);
    fat_sectors = read_u32(disk->sectors_per_fat);
    registry_start = read_u64(disk->registry_start_sector);
    if (memcmp(disk->magic, IOS_FS_MAGIC, sizeof(disk->magic)) != 0
        || read_u16(disk->format_version) != IOS_FS_FORMAT_VERSION
        || read_u16(disk->header_size) != IOS_FS_SUPERBLOCK_HEADER_SIZE
        || read_u16(disk->bytes_per_sector) != IOS_FS_SECTOR_SIZE
        || disk->sectors_per_cluster != IOS_FS_SECTORS_PER_CLUSTER || disk->fat_count != 1
        || read_u16(disk->reserved_superblock_sectors) != IOS_FS_SUPERBLOCK_SECTORS
        || read_u16(disk->primary_record_size) != IOS_FS_PRIMARY_RECORD_SIZE
        || read_u16(disk->companion_record_size) != IOS_FS_COMPANION_RECORD_SIZE
        || read_u16(disk->flags) != 0 || total_sectors == 0 || fat_sectors == 0
        || read_u32(disk->root_cluster) != IOS_FS_ROOT_CLUSTER
        || registry_start != IOS_FS_SUPERBLOCK_SECTORS + fat_sectors
        || registry_start > UINT64_MAX - IOS_FS_REGISTRY_SECTORS
        || registry_start + IOS_FS_REGISTRY_SECTORS >= total_sectors
        || read_u32(disk->registry_sector_count) != IOS_FS_REGISTRY_SECTORS
        || !valid_label(disk->volume_label) || disk->hash_algorithm_id != IOS_FS_HASH_FNV1A32
        || read_u16(disk->companion_record_version) != IOS_FS_COMPANION_VERSION
        || read_u16(disk->registry_record_version) != IOS_FS_REGISTRY_VERSION
        || !all_zero(disk->reserved_header, sizeof(disk->reserved_header))
        || !all_zero(disk->reserved, sizeof(disk->reserved))
        || read_u16(disk->trailer_signature) != IOS_FS_SUPERBLOCK_TRAILER) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    values->total_sectors = total_sectors;
    values->sectors_per_fat = fat_sectors;
    values->registry_start_sector = registry_start;
    values->volume_serial = read_u32(disk->volume_serial);
    values->flags = read_u16(disk->flags);
    memcpy(values->volume_label, disk->volume_label, sizeof(values->volume_label));
    return IOS_OK;
}

enum ios_fs_superblock_pair_state ios_fs_superblock_classify_pair(
    const struct ios_fs_superblock_disk *primary, const struct ios_fs_superblock_disk *backup)
{
    struct ios_fs_superblock ignored;
    bool primary_valid;
    bool backup_valid;
    if (primary == NULL || backup == NULL) return IOS_FS_SUPERBLOCK_PAIR_REJECTED;
    primary_valid = IOS_SUCCEEDED(ios_fs_superblock_decode(primary, &ignored));
    backup_valid = IOS_SUCCEEDED(ios_fs_superblock_decode(backup, &ignored));
    if (!primary_valid && !backup_valid) return IOS_FS_SUPERBLOCK_PAIR_REJECTED;
    if (!primary_valid || !backup_valid) return IOS_FS_SUPERBLOCK_PAIR_DIAGNOSTIC;
    return memcmp(primary, backup, sizeof(*primary)) == 0
        ? IOS_FS_SUPERBLOCK_PAIR_READ_WRITE : IOS_FS_SUPERBLOCK_PAIR_DIAGNOSTIC;
}
