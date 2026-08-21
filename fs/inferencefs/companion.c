#include <inferencefs/companion.h>
#include <inferencefs/name.h>
#include <inferenceos/crc32.h>
#include <inferenceos/endian.h>
#include <inferenceos/fnv1a.h>
#include <inferenceos/memory.h>

static inferencefs_companion_outcome companion_outcome(
    inferenceos_result result,
    inferencefs_companion_error error,
    inferenceos_u32 stored_crc,
    inferenceos_u32 computed_crc
)
{
    const inferencefs_companion_outcome value = {
        .result = result,
        .error = error,
        .stored_crc32 = stored_crc,
        .computed_crc32 = computed_crc
    };
    return value;
}

static inferenceos_u32 companion_crc(const inferencefs_companion_record_disk *disk)
{
    static const inferenceos_u8 zero_crc[4] = { 0U, 0U, 0U, 0U };
    const inferenceos_u8 *bytes = (const inferenceos_u8 *)disk;
    inferenceos_u32 state = inferenceos_crc32_begin();

    state = inferenceos_crc32_update(state, bytes, 0x0CU);
    state = inferenceos_crc32_update(state, zero_crc, sizeof(zero_crc));
    state = inferenceos_crc32_update(state, bytes + 0x10U, 16U);
    return inferenceos_crc32_finish(state);
}

static bool reserved_is_zero(const inferencefs_companion_record_disk *disk)
{
    for (inferenceos_size index = 0U; index < sizeof(disk->reserved0); ++index) {
        if (disk->reserved0[index] != 0U) {
            return false;
        }
    }
    for (inferenceos_size index = 0U; index < sizeof(disk->reserved1); ++index) {
        if (disk->reserved1[index] != 0U) {
            return false;
        }
    }
    return true;
}

inferencefs_companion_outcome inferencefs_companion_decode(
    const inferencefs_companion_record_disk *disk,
    const inferenceos_u8 primary_name[INFERENCEFS_SHORT_NAME_SIZE],
    bool require_committed,
    inferencefs_companion *decoded
)
{
    inferenceos_u8 extension[INFERENCEFS_SHORT_NAME_EXTENSION_SIZE];
    inferenceos_u8 extension_length;
    inferencefs_companion value;
    inferenceos_u32 stored_crc;
    inferenceos_u32 computed_crc;

    if (disk == NULL || primary_name == NULL || decoded == NULL) {
        return companion_outcome(INFERENCEOS_RESULT_INVALID_ARGUMENT,
            INFERENCEFS_COMPANION_ERROR_ARGUMENT, 0U, 0U);
    }
    stored_crc = inferenceos_load_le32(disk->record_crc32_le);
    computed_crc = companion_crc(disk);
    if (disk->record_type != INFERENCEFS_COMPANION_RECORD_TYPE) {
        return companion_outcome(INFERENCEOS_RESULT_CORRUPT,
            INFERENCEFS_COMPANION_ERROR_TYPE, stored_crc, computed_crc);
    }
    if (disk->record_version != INFERENCEFS_COMPANION_RECORD_VERSION) {
        return companion_outcome(INFERENCEOS_RESULT_UNSUPPORTED,
            INFERENCEFS_COMPANION_ERROR_VERSION, stored_crc, computed_crc);
    }
    if (disk->hash_algorithm_id != INFERENCEFS_HASH_ALGORITHM_FNV1A32) {
        return companion_outcome(INFERENCEOS_RESULT_UNSUPPORTED,
            INFERENCEFS_COMPANION_ERROR_ALGORITHM, stored_crc, computed_crc);
    }
    if ((disk->flags & (inferenceos_u8)~INFERENCEFS_COMPANION_SUPPORTED_FLAGS) != 0U
        || (require_committed
            && (disk->flags & INFERENCEFS_COMPANION_FLAG_COMMITTED) == 0U)) {
        return companion_outcome(INFERENCEOS_RESULT_CORRUPT,
            INFERENCEFS_COMPANION_ERROR_FLAGS, stored_crc, computed_crc);
    }
    if (disk->extension_length > INFERENCEFS_SHORT_NAME_EXTENSION_SIZE) {
        return companion_outcome(INFERENCEOS_RESULT_CORRUPT,
            INFERENCEFS_COMPANION_ERROR_LENGTH, stored_crc, computed_crc);
    }
    if (!reserved_is_zero(disk)) {
        return companion_outcome(INFERENCEOS_RESULT_CORRUPT,
            INFERENCEFS_COMPANION_ERROR_RESERVED, stored_crc, computed_crc);
    }
    if (stored_crc != computed_crc) {
        return companion_outcome(INFERENCEOS_RESULT_CORRUPT,
            INFERENCEFS_COMPANION_ERROR_CRC, stored_crc, computed_crc);
    }
    if (inferencefs_primary_name_checksum(primary_name)
        != disk->primary_name_checksum) {
        return companion_outcome(INFERENCEOS_RESULT_INCONSISTENT,
            INFERENCEFS_COMPANION_ERROR_NAME_CHECKSUM, stored_crc, computed_crc);
    }
    if (!inferenceos_result_is_success(inferencefs_name_extension(
            primary_name, extension, &extension_length))
        || extension_length != disk->extension_length) {
        return companion_outcome(INFERENCEOS_RESULT_INCONSISTENT,
            INFERENCEFS_COMPANION_ERROR_LENGTH, stored_crc, computed_crc);
    }
    value.extension_hash = inferenceos_load_le32(disk->extension_hash_le);
    if (value.extension_hash != inferenceos_fnv1a32(extension, extension_length)) {
        return companion_outcome(INFERENCEOS_RESULT_INCONSISTENT,
            INFERENCEFS_COMPANION_ERROR_EXTENSION_HASH,
            stored_crc, computed_crc);
    }
    value.extension_length = extension_length;
    value.primary_name_checksum = disk->primary_name_checksum;
    value.stored_crc32 = stored_crc;
    value.committed = (disk->flags
        & INFERENCEFS_COMPANION_FLAG_COMMITTED) != 0U;
    *decoded = value;
    return companion_outcome(INFERENCEOS_RESULT_OK,
        INFERENCEFS_COMPANION_ERROR_NONE, stored_crc, computed_crc);
}

inferenceos_result inferencefs_companion_encode(
    inferencefs_companion_record_disk *disk,
    const inferenceos_u8 primary_name[INFERENCEFS_SHORT_NAME_SIZE],
    bool committed
)
{
    inferenceos_u8 extension[INFERENCEFS_SHORT_NAME_EXTENSION_SIZE];
    inferenceos_u8 extension_length;

    if (disk == NULL || primary_name == NULL) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    if (!inferenceos_result_is_success(inferencefs_name_extension(
            primary_name, extension, &extension_length))) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    (void)memset(disk, 0, sizeof(*disk));
    disk->record_type = INFERENCEFS_COMPANION_RECORD_TYPE;
    disk->record_version = INFERENCEFS_COMPANION_RECORD_VERSION;
    disk->hash_algorithm_id = INFERENCEFS_HASH_ALGORITHM_FNV1A32;
    disk->flags = committed ? INFERENCEFS_COMPANION_FLAG_COMMITTED : 0U;
    disk->extension_length = extension_length;
    disk->primary_name_checksum = inferencefs_primary_name_checksum(primary_name);
    inferenceos_store_le32(
        disk->extension_hash_le,
        inferenceos_fnv1a32(extension, extension_length));
    inferenceos_store_le32(disk->record_crc32_le, 0U);
    inferenceos_store_le32(disk->record_crc32_le, companion_crc(disk));
    return INFERENCEOS_RESULT_OK;
}
