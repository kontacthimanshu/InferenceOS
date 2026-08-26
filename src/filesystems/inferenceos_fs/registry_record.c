#include <inferenceos/fs/registry.h>

#include <inferenceos/runtime.h>

static void write_u16(ios_u8 bytes[2], ios_u16 value)
{
    bytes[0] = (ios_u8)value;
    bytes[1] = (ios_u8)(value >> 8);
}

static void write_u32(ios_u8 bytes[4], ios_u32 value)
{
    for (ios_size index = 0; index < 4; ++index) {
        bytes[index] = (ios_u8)(value >> (index * 8));
    }
}

static ios_u16 read_u16(const ios_u8 bytes[2])
{
    return (ios_u16)((ios_u16)bytes[0] | ((ios_u16)bytes[1] << 8));
}

static ios_u32 read_u32(const ios_u8 bytes[4])
{
    ios_u32 value = 0;
    for (ios_size index = 0; index < 4; ++index) {
        value |= (ios_u32)bytes[index] << (index * 8);
    }
    return value;
}

static bool all_zero(const ios_u8 *bytes, ios_size length)
{
    for (ios_size index = 0; index < length; ++index) {
        if (bytes[index] != 0) return false;
    }
    return true;
}

static bool supported_extension_byte(ios_u8 value)
{
    return (value >= 'A' && value <= 'Z')
           || (value >= '0' && value <= '9')
           || value == '_' || value == '-';
}

static bool valid_value_extension(const struct ios_fs_registry_record *value)
{
    if (value->extension_length > IOS_FS_EXTENSION_SIZE) return false;
    for (ios_size index = 0; index < value->extension_length; ++index) {
        if (!supported_extension_byte(value->canonical_extension[index])) return false;
    }
    return all_zero(
        value->canonical_extension + value->extension_length,
        IOS_FS_EXTENSION_SIZE - value->extension_length
    );
}

static bool valid_disk_extension(const struct ios_fs_registry_record_disk *disk)
{
    if (disk->extension_length > IOS_FS_EXTENSION_SIZE) return false;
    for (ios_size index = 0; index < disk->extension_length; ++index) {
        if (!supported_extension_byte(disk->canonical_extension[index])) return false;
    }
    for (ios_size index = disk->extension_length;
         index < IOS_FS_EXTENSION_SIZE; ++index) {
        if (disk->canonical_extension[index] != ' ') return false;
    }
    return true;
}

static ios_u32 registry_record_crc(
    const struct ios_fs_registry_record_disk *disk
)
{
    struct ios_fs_registry_record_disk copy = *disk;
    memset(copy.crc32, 0, sizeof(copy.crc32));
    return ios_fs_crc32_iso_hdlc(&copy, sizeof(copy));
}

ios_status ios_fs_registry_record_encode(
    const struct ios_fs_registry_record *value,
    struct ios_fs_registry_record_disk *disk
)
{
    if (value == NULL || disk == NULL || !valid_value_extension(value)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    memset(disk, 0, sizeof(*disk));
    disk->record_type = IOS_FS_REGISTRY_RECORD_TYPE;
    disk->record_version = IOS_FS_REGISTRY_VERSION;
    disk->flags = value->active ? IOS_FS_REGISTRY_FLAG_ACTIVE : 0;
    disk->extension_length = value->extension_length;
    disk->hash_algorithm_id = IOS_FS_HASH_FNV1A32;
    memset(disk->canonical_extension, ' ', sizeof(disk->canonical_extension));
    memcpy(
        disk->canonical_extension, value->canonical_extension,
        value->extension_length
    );
    ios_fs_hash_text(
        ios_fs_fnv1a32(value->canonical_extension, value->extension_length),
        disk->extension_hash_text
    );
    write_u32(disk->last_directory_cluster, value->last_directory_cluster);
    write_u16(disk->last_directory_slot, value->last_directory_slot);
    write_u16(disk->update_generation, value->update_generation);
    write_u32(disk->crc32, registry_record_crc(disk));
    return IOS_OK;
}

ios_status ios_fs_registry_record_decode(
    const struct ios_fs_registry_record_disk *disk,
    struct ios_fs_registry_record *value
)
{
    ios_u8 expected_hash[IOS_FS_HASH_TEXT_SIZE];
    if (disk == NULL || value == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    memset(value, 0, sizeof(*value));
    if (disk->record_type != IOS_FS_REGISTRY_RECORD_TYPE) {
        return IOS_ERROR(IOS_E_CORRUPT);
    }
    if (disk->record_version != IOS_FS_REGISTRY_VERSION) {
        return IOS_ERROR(IOS_E_UNSUPPORTED_VERSION);
    }
    if (registry_record_crc(disk) != read_u32(disk->crc32)
        || (disk->flags & (ios_u8)~IOS_FS_REGISTRY_FLAG_ACTIVE) != 0
        || disk->hash_algorithm_id != IOS_FS_HASH_FNV1A32
        || !all_zero(disk->reserved0, sizeof(disk->reserved0))
        || disk->reserved1 != 0
        || !valid_disk_extension(disk)) {
        return IOS_ERROR(IOS_E_CORRUPT);
    }
    ios_fs_hash_text(
        ios_fs_fnv1a32(
            disk->canonical_extension, disk->extension_length
        ),
        expected_hash
    );
    if (memcmp(
        disk->extension_hash_text, expected_hash, sizeof(expected_hash)
    ) != 0) {
        return IOS_ERROR(IOS_E_CORRUPT);
    }
    value->active = (disk->flags & IOS_FS_REGISTRY_FLAG_ACTIVE) != 0;
    value->extension_length = disk->extension_length;
    memcpy(
        value->canonical_extension, disk->canonical_extension,
        disk->extension_length
    );
    memcpy(
        value->extension_hash_text, disk->extension_hash_text,
        sizeof(value->extension_hash_text)
    );
    value->last_directory_cluster = read_u32(disk->last_directory_cluster);
    value->last_directory_slot = read_u16(disk->last_directory_slot);
    value->update_generation = read_u16(disk->update_generation);
    return IOS_OK;
}
