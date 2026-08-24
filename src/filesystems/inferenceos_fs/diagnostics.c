#include <inferenceos/fs_diagnostic.h>

struct diagnostic_writer {
    ios_fs_diagnostic_character_sink sink;
    void *context;
};

static bool write_character(struct diagnostic_writer *writer, char character)
{
    return writer->sink(writer->context, character);
}

static bool write_text(struct diagnostic_writer *writer, const char *text)
{
    while (*text != '\0') {
        if (!write_character(writer, *text++)) return false;
    }
    return true;
}

static bool write_u64(struct diagnostic_writer *writer, ios_u64 value)
{
    char digits[20];
    ios_size count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (count != 0) {
        if (!write_character(writer, digits[--count])) return false;
    }
    return true;
}

static bool write_i64(struct diagnostic_writer *writer, ios_i64 value)
{
    ios_u64 magnitude;
    if (value >= 0) return write_u64(writer, (ios_u64)value);
    if (!write_character(writer, '-')) return false;
    magnitude = (ios_u64)(-(value + 1)) + 1;
    return write_u64(writer, magnitude);
}

static bool write_hex_bytes(
    struct diagnostic_writer *writer, const ios_u8 *bytes, ios_size length
)
{
    static const char digits[] = "0123456789abcdef";
    for (ios_size index = 0; index < length; ++index) {
        if (!write_character(writer, digits[bytes[index] >> 4])
            || !write_character(writer, digits[bytes[index] & 0x0f])) return false;
    }
    return true;
}

static const char *query_name(enum ios_fs_diagnostic_query query)
{
    switch (query) {
    case IOS_FS_DIAGNOSTIC_QUERY_FILESYSTEM: return "filesystem";
    case IOS_FS_DIAGNOSTIC_QUERY_FILE: return "file";
    case IOS_FS_DIAGNOSTIC_QUERY_HASH: return "hash";
    case IOS_FS_DIAGNOSTIC_QUERY_FAT: return "fat";
    default: return NULL;
    }
}

#define WRITE_TEXT(value) do { if (!write_text(&writer, (value))) goto io_error; } while (0)
#define WRITE_U64(value) do { if (!write_u64(&writer, (ios_u64)(value))) goto io_error; } while (0)
#define WRITE_I64(value) do { if (!write_i64(&writer, (ios_i64)(value))) goto io_error; } while (0)
#define WRITE_HEX(bytes, length) do { \
    if (!write_hex_bytes(&writer, (bytes), (length))) goto io_error; \
} while (0)

ios_status ios_fs_diagnostic_format(
    const struct ios_fs_diagnostic_reply *reply,
    ios_fs_diagnostic_character_sink sink,
    void *sink_context
)
{
    struct diagnostic_writer writer = { sink, sink_context };
    const char *name;
    if (reply == NULL || sink == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    if (reply->size != sizeof(*reply) || reply->version != IOS_FS_DIAGNOSTIC_ABI_VERSION) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    name = query_name(reply->query);
    if (name == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);

    WRITE_TEXT("inferenceos.fsdiag version="); WRITE_U64(reply->version);
    WRITE_TEXT(" query="); WRITE_TEXT(name);
    switch (reply->query) {
    case IOS_FS_DIAGNOSTIC_QUERY_FILESYSTEM: {
        const struct ios_fs_diagnostic_filesystem_info *info = &reply->value.filesystem;
        WRITE_TEXT(" identity_hex="); WRITE_HEX(info->identity, sizeof(info->identity));
        WRITE_TEXT(" format_version="); WRITE_U64(info->format_version);
        WRITE_TEXT(" volume_capacity_bytes="); WRITE_U64(info->volume_capacity_bytes);
        WRITE_TEXT(" usable_bytes="); WRITE_U64(info->usable_bytes);
        WRITE_TEXT(" free_space_known="); WRITE_U64(info->free_space_known ? 1 : 0);
        if (info->free_space_known) { WRITE_TEXT(" free_bytes="); WRITE_U64(info->free_bytes); }
        WRITE_TEXT(" sectors_per_cluster="); WRITE_U64(info->sectors_per_cluster);
        WRITE_TEXT(" data_start_sector="); WRITE_U64(info->data_start_sector);
        WRITE_TEXT(" fat_sectors="); WRITE_U64(info->fat_sectors);
        WRITE_TEXT(" cluster_count="); WRITE_U64(info->cluster_count);
        WRITE_TEXT(" primary_record_size="); WRITE_U64(info->primary_record_size);
        WRITE_TEXT(" companion_record_size="); WRITE_U64(info->companion_record_size);
        WRITE_TEXT(" hash_algorithm_id="); WRITE_U64(info->hash_algorithm_id);
        WRITE_TEXT(" mount_state="); WRITE_U64(info->mount_state);
        WRITE_TEXT(" registry_health="); WRITE_U64(info->registry_health);
        WRITE_TEXT(" registry_active_type_count="); WRITE_U64(info->registry_active_type_count);
        break;
    }
    case IOS_FS_DIAGNOSTIC_QUERY_FILE: {
        const struct ios_fs_diagnostic_file_info *info = &reply->value.file;
        WRITE_TEXT(" canonical_name_hex="); WRITE_HEX(info->canonical_name, sizeof(info->canonical_name));
        WRITE_TEXT(" object_type="); WRITE_U64(info->object_type);
        WRITE_TEXT(" attributes="); WRITE_U64(info->attributes);
        WRITE_TEXT(" size="); WRITE_U64(info->size);
        WRITE_TEXT(" first_cluster="); WRITE_U64(info->first_cluster);
        WRITE_TEXT(" primary_record_location="); WRITE_U64(info->primary_record_location);
        WRITE_TEXT(" companion_record_location="); WRITE_U64(info->companion_record_location);
        break;
    }
    case IOS_FS_DIAGNOSTIC_QUERY_HASH: {
        const struct ios_fs_diagnostic_hash_info *info = &reply->value.hash;
        if (info->extension_length > IOS_FS_EXTENSION_SIZE) return IOS_ERROR(IOS_E_PROTOCOL);
        WRITE_TEXT(" extension_hex="); WRITE_HEX(info->extension, info->extension_length);
        WRITE_TEXT(" extension_length="); WRITE_U64(info->extension_length);
        WRITE_TEXT(" hash_algorithm_id="); WRITE_U64(info->hash_algorithm_id);
        WRITE_TEXT(" stored_hash_hex="); WRITE_HEX(info->stored_hash, sizeof(info->stored_hash));
        WRITE_TEXT(" recomputed_hash_hex="); WRITE_HEX(info->recomputed_hash, sizeof(info->recomputed_hash));
        WRITE_TEXT(" record_version="); WRITE_U64(info->record_version);
        WRITE_TEXT(" committed="); WRITE_U64(info->committed ? 1 : 0);
        WRITE_TEXT(" association_checksum_valid="); WRITE_U64(info->association_checksum_valid ? 1 : 0);
        WRITE_TEXT(" crc_valid="); WRITE_U64(info->crc_valid ? 1 : 0);
        WRITE_TEXT(" validation_status="); WRITE_I64(info->validation_status);
        break;
    }
    case IOS_FS_DIAGNOSTIC_QUERY_FAT: {
        const struct ios_fs_diagnostic_fat_info *info = &reply->value.fat;
        if (info->cluster_count > IOS_FS_DIAGNOSTIC_CHAIN_CAPACITY) {
            return IOS_ERROR(IOS_E_PROTOCOL);
        }
        WRITE_TEXT(" cluster_count="); WRITE_U64(info->cluster_count);
        WRITE_TEXT(" chain=");
        for (ios_size index = 0; index < info->cluster_count; ++index) {
            if (index != 0 && !write_character(&writer, ',')) goto io_error;
            WRITE_U64(info->clusters[index]);
        }
        WRITE_TEXT(" end_of_chain="); WRITE_U64(info->end_of_chain ? 1 : 0);
        break;
    }
    default: return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (!write_character(&writer, '\n')) goto io_error;
    return IOS_OK;

io_error:
    return IOS_ERROR(IOS_E_IO);
}

#undef WRITE_TEXT
#undef WRITE_U64
#undef WRITE_I64
#undef WRITE_HEX
