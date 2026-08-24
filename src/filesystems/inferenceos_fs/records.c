#include <inferenceos/fs/records.h>

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

static ios_u16 read_u16(const ios_u8 *bytes)
{
    return (ios_u16)((ios_u16)*bytes | ((ios_u16)bytes[1] << 8));
}

static ios_u32 read_u32(const ios_u8 *bytes)
{
    ios_u32 value = 0;
    for (ios_size index = 0; index < 4; ++index) value |= (ios_u32)bytes[index] << (index * 8);
    return value;
}

static bool supported_name_byte(ios_u8 value)
{
    return (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9')
           || value == '_' || value == '-';
}

static bool valid_stored_name(const ios_u8 name[IOS_FS_NAME_SIZE])
{
    bool base_padding = false;
    bool extension_padding = false;
    if (name == NULL || *name == ' ') return false;
    for (ios_size index = 0; index < 8; ++index) {
        if (name[index] == ' ') base_padding = true;
        else if (base_padding || !supported_name_byte(name[index])) return false;
    }
    for (ios_size index = 8; index < IOS_FS_NAME_SIZE; ++index) {
        if (name[index] == ' ') extension_padding = true;
        else if (extension_padding || !supported_name_byte(name[index])) return false;
    }
    return true;
}

static bool all_zero(const ios_u8 *bytes, ios_size length)
{
    for (ios_size index = 0; index < length; ++index) if (bytes[index] != 0) return false;
    return true;
}

static bool valid_hash_text(const ios_u8 text[IOS_FS_HASH_TEXT_SIZE])
{
    for (ios_size index = 0; index < IOS_FS_HASH_TEXT_SIZE; ++index) {
        if (!((text[index] >= '0' && text[index] <= '9')
              || (text[index] >= 'A' && text[index] <= 'F'))) return false;
    }
    return true;
}

static ios_u32 companion_crc(const struct ios_fs_companion_disk *disk)
{
    struct ios_fs_companion_disk copy = *disk;
    memset(copy.crc32, 0, sizeof(copy.crc32));
    return ios_fs_crc32_iso_hdlc(&copy, sizeof(copy));
}

ios_status ios_fs_name_canonicalize_83(const char *input, ios_u8 output[IOS_FS_NAME_SIZE])
{
    ios_size base_length = 0;
    ios_size extension_length = 0;
    bool in_extension = false;
    if (input == NULL || output == NULL || *input == '\0') return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    memset(output, ' ', IOS_FS_NAME_SIZE);
    for (const char *cursor = input; *cursor != '\0'; ++cursor) {
        ios_u8 value = (ios_u8)*cursor;
        if (value == '.') {
            if (in_extension || base_length == 0) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
            in_extension = true;
            continue;
        }
        if (value >= 'a' && value <= 'z') value = (ios_u8)(value - ('a' - 'A'));
        if (!supported_name_byte(value)) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
        if (!in_extension) {
            if (base_length == 8) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
            output[base_length++] = value;
        } else {
            if (extension_length == 3) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
            output[8 + extension_length++] = value;
        }
    }
    return IOS_OK;
}

ios_status ios_fs_name_extension(
    const ios_u8 name[IOS_FS_NAME_SIZE],
    ios_u8 output[IOS_FS_EXTENSION_SIZE],
    ios_size *length
)
{
    ios_size count = IOS_FS_EXTENSION_SIZE;
    if (name == NULL || output == NULL || length == NULL || !valid_stored_name(name)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    while (count != 0 && name[8 + count - 1] == ' ') --count;
    memset(output, 0, IOS_FS_EXTENSION_SIZE);
    memcpy(output, name + 8, count);
    *length = count;
    return IOS_OK;
}

ios_u8 ios_fs_primary_name_checksum(const ios_u8 name[IOS_FS_NAME_SIZE])
{
    ios_u8 checksum = 0;
    if (name == NULL) return 0;
    for (ios_size index = 0; index < IOS_FS_NAME_SIZE; ++index) {
        checksum = (ios_u8)(((checksum & 1U) != 0U ? 0x80U : 0U)
                            + (checksum >> 1) + name[index]);
    }
    return checksum;
}

ios_status ios_fs_primary_encode(
    const struct ios_fs_primary *value, struct ios_fs_primary_disk *disk
)
{
    if (value == NULL || disk == NULL || !valid_stored_name(value->name)
        || (value->attributes != IOS_FS_ATTRIBUTE_REGULAR
            && value->attributes != IOS_FS_ATTRIBUTE_DIRECTORY)
        || (value->attributes == IOS_FS_ATTRIBUTE_DIRECTORY && value->file_size != 0)
        || (value->attributes == IOS_FS_ATTRIBUTE_REGULAR && value->file_size == 0
            && value->first_cluster != 0)
        || ((value->file_size != 0 || value->attributes == IOS_FS_ATTRIBUTE_DIRECTORY)
            && value->first_cluster < 2)
        || value->first_cluster > IOS_FS_MAX_DATA_CLUSTER) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    memset(disk, 0, sizeof(*disk));
    memcpy(disk->name, value->name, sizeof(disk->name));
    disk->attributes = value->attributes;
    write_u16(disk->first_cluster_high, (ios_u16)(value->first_cluster >> 16));
    write_u16(disk->first_cluster_low, (ios_u16)value->first_cluster);
    write_u32(disk->file_size, value->file_size);
    return IOS_OK;
}

ios_status ios_fs_primary_decode(
    const struct ios_fs_primary_disk *disk, struct ios_fs_primary *value
)
{
    ios_u32 first_cluster;
    ios_u32 file_size;
    if (disk == NULL || value == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    memset(value, 0, sizeof(*value));
    if (*disk->name == 0 || *disk->name == 0xe5) return IOS_ERROR(IOS_E_NOT_FOUND);
    first_cluster = ((ios_u32)read_u16(disk->first_cluster_high) << 16)
                    | read_u16(disk->first_cluster_low);
    file_size = read_u32(disk->file_size);
    if (!valid_stored_name(disk->name)
        || (disk->attributes != IOS_FS_ATTRIBUTE_REGULAR
            && disk->attributes != IOS_FS_ATTRIBUTE_DIRECTORY)
        || disk->reserved != 0 || disk->create_tenth != 0
        || !all_zero(disk->create_time, sizeof(disk->create_time))
        || !all_zero(disk->create_date, sizeof(disk->create_date))
        || !all_zero(disk->access_date, sizeof(disk->access_date))
        || !all_zero(disk->write_time, sizeof(disk->write_time))
        || !all_zero(disk->write_date, sizeof(disk->write_date))
        || (disk->attributes == IOS_FS_ATTRIBUTE_DIRECTORY && file_size != 0)
        || (disk->attributes == IOS_FS_ATTRIBUTE_REGULAR && file_size == 0
            && first_cluster != 0)
        || ((file_size != 0 || disk->attributes == IOS_FS_ATTRIBUTE_DIRECTORY)
            && first_cluster < 2)
        || first_cluster > IOS_FS_MAX_DATA_CLUSTER) return IOS_ERROR(IOS_E_PROTOCOL);
    memcpy(value->name, disk->name, sizeof(value->name));
    value->attributes = disk->attributes;
    value->first_cluster = first_cluster;
    value->file_size = file_size;
    return IOS_OK;
}

ios_status ios_fs_companion_encode(
    const ios_u8 primary_name[IOS_FS_NAME_SIZE],
    bool committed,
    struct ios_fs_companion_disk *disk
)
{
    ios_u8 extension[IOS_FS_EXTENSION_SIZE];
    ios_size length;
    ios_status status;
    if (disk == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    status = ios_fs_name_extension(primary_name, extension, &length);
    if (IOS_FAILED(status)) return status;
    memset(disk, 0, sizeof(*disk));
    disk->record_type = IOS_FS_COMPANION_RECORD_TYPE;
    disk->record_version = IOS_FS_COMPANION_VERSION;
    disk->hash_algorithm_id = IOS_FS_HASH_FNV1A32;
    disk->flags = committed ? IOS_FS_COMPANION_FLAG_COMMITTED : 0;
    disk->extension_length = (ios_u8)length;
    disk->primary_name_checksum = ios_fs_primary_name_checksum(primary_name);
    ios_fs_hash_text(ios_fs_fnv1a32(extension, length), disk->extension_hash_text);
    write_u32(disk->crc32, companion_crc(disk));
    return IOS_OK;
}

ios_status ios_fs_companion_decode(
    const struct ios_fs_companion_disk *disk, struct ios_fs_companion *value
)
{
    if (disk == NULL || value == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    memset(value, 0, sizeof(*value));
    if (companion_crc(disk) != read_u32(disk->crc32)) return IOS_ERROR(IOS_E_CORRUPT);
    if (disk->record_type != IOS_FS_COMPANION_RECORD_TYPE
        || disk->record_version != IOS_FS_COMPANION_VERSION
        || disk->hash_algorithm_id != IOS_FS_HASH_FNV1A32
        || (disk->flags & (ios_u8)~IOS_FS_COMPANION_FLAG_COMMITTED) != 0
        || disk->extension_length > IOS_FS_EXTENSION_SIZE
        || !all_zero(disk->reserved0, sizeof(disk->reserved0))
        || !valid_hash_text(disk->extension_hash_text)
        || !all_zero(disk->reserved1, sizeof(disk->reserved1))) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    value->committed = (disk->flags & IOS_FS_COMPANION_FLAG_COMMITTED) != 0;
    value->extension_length = disk->extension_length;
    value->primary_name_checksum = disk->primary_name_checksum;
    memcpy(value->extension_hash_text, disk->extension_hash_text, sizeof(value->extension_hash_text));
    return IOS_OK;
}

ios_status ios_fs_record_pair_validate(
    const struct ios_fs_companion_disk *companion_disk,
    const struct ios_fs_primary_disk *primary_disk
)
{
    struct ios_fs_companion companion;
    struct ios_fs_primary primary;
    ios_u8 extension[IOS_FS_EXTENSION_SIZE];
    ios_u8 expected_hash[IOS_FS_HASH_TEXT_SIZE];
    ios_size length;
    ios_status status;
    status = ios_fs_companion_decode(companion_disk, &companion);
    if (IOS_FAILED(status)) return status;
    status = ios_fs_primary_decode(primary_disk, &primary);
    if (IOS_FAILED(status)) return status == IOS_ERROR(IOS_E_NOT_FOUND)
        ? IOS_ERROR(IOS_E_CORRUPT) : status;
    status = ios_fs_name_extension(primary.name, extension, &length);
    if (IOS_FAILED(status)) return IOS_ERROR(IOS_E_CORRUPT);
    ios_fs_hash_text(ios_fs_fnv1a32(extension, length), expected_hash);
    if (!companion.committed || primary.attributes != IOS_FS_ATTRIBUTE_REGULAR
        || companion.extension_length != length
        || companion.primary_name_checksum != ios_fs_primary_name_checksum(primary.name)
        || memcmp(companion.extension_hash_text, expected_hash, sizeof(expected_hash)) != 0) {
        return IOS_ERROR(IOS_E_CORRUPT);
    }
    return IOS_OK;
}

bool ios_fs_primary_types_equal(
    const struct ios_fs_primary *left,
    const struct ios_fs_primary *right,
    const ios_u8 left_hash[IOS_FS_HASH_TEXT_SIZE],
    const ios_u8 right_hash[IOS_FS_HASH_TEXT_SIZE]
)
{
    ios_u8 left_extension[IOS_FS_EXTENSION_SIZE];
    ios_u8 right_extension[IOS_FS_EXTENSION_SIZE];
    ios_size left_length;
    ios_size right_length;
    if (left == NULL || right == NULL || left_hash == NULL || right_hash == NULL
        || memcmp(left_hash, right_hash, IOS_FS_HASH_TEXT_SIZE) != 0
        || IOS_FAILED(ios_fs_name_extension(left->name, left_extension, &left_length))
        || IOS_FAILED(ios_fs_name_extension(right->name, right_extension, &right_length))) {
        return false;
    }
    return left_length == right_length
           && memcmp(left_extension, right_extension, left_length) == 0;
}
