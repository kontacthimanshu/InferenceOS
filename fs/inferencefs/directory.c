#include <inferencefs/directory.h>
#include <inferencefs/name.h>
#include <inferenceos/endian.h>
#include <inferenceos/memory.h>

static bool supported_name(const inferenceos_u8 name[INFERENCEFS_SHORT_NAME_SIZE])
{
    bool padding = false;

    if (name[0] == (inferenceos_u8)'.') {
        inferenceos_size index = name[1] == (inferenceos_u8)'.' ? 2U : 1U;
        for (; index < INFERENCEFS_SHORT_NAME_SIZE; ++index) {
            if (name[index] != INFERENCEFS_SHORT_NAME_PADDING) {
                return false;
            }
        }
        return true;
    }
    for (inferenceos_size index = 0U;
         index < INFERENCEFS_SHORT_NAME_BASE_SIZE;
         ++index) {
        const inferenceos_u8 byte = name[index];
        if (byte == INFERENCEFS_SHORT_NAME_PADDING) {
            padding = true;
        } else if (padding || !inferencefs_name_character_is_supported((char)byte)
            || (byte >= (inferenceos_u8)'a'
                && byte <= (inferenceos_u8)'z')) {
            return false;
        }
    }
    if (name[0] == INFERENCEFS_SHORT_NAME_PADDING) {
        return false;
    }
    padding = false;
    for (inferenceos_size index = INFERENCEFS_SHORT_NAME_BASE_SIZE;
         index < INFERENCEFS_SHORT_NAME_SIZE;
         ++index) {
        const inferenceos_u8 byte = name[index];
        if (byte == INFERENCEFS_SHORT_NAME_PADDING) {
            padding = true;
        } else if (padding || !inferencefs_name_character_is_supported((char)byte)
            || (byte >= (inferenceos_u8)'a'
                && byte <= (inferenceos_u8)'z')) {
            return false;
        }
    }
    return true;
}

inferencefs_directory_slot_kind inferencefs_directory_classify_slot(
    const void *slot
)
{
    const inferencefs_primary_record_disk *disk = slot;

    if (slot == NULL) {
        return INFERENCEFS_DIRECTORY_SLOT_KIND_CORRUPT;
    }
    if (disk->name[0] == INFERENCEFS_DIRECTORY_SLOT_END) {
        return INFERENCEFS_DIRECTORY_SLOT_KIND_END;
    }
    if (disk->name[0] == INFERENCEFS_DIRECTORY_SLOT_DELETED) {
        return INFERENCEFS_DIRECTORY_SLOT_KIND_DELETED;
    }
    if (disk->name[0] == INFERENCEFS_COMPANION_RECORD_TYPE) {
        return INFERENCEFS_DIRECTORY_SLOT_KIND_COMPANION;
    }
    if (disk->attributes == INFERENCEFS_PRIMARY_ATTRIBUTE_REGULAR_FILE) {
        return INFERENCEFS_DIRECTORY_SLOT_KIND_REGULAR;
    }
    if (disk->attributes == INFERENCEFS_PRIMARY_ATTRIBUTE_DIRECTORY) {
        return INFERENCEFS_DIRECTORY_SLOT_KIND_DIRECTORY;
    }
    return INFERENCEFS_DIRECTORY_SLOT_KIND_UNSUPPORTED;
}

inferenceos_result inferencefs_directory_decode_primary(
    const inferencefs_primary_record_disk *disk,
    inferencefs_primary_record *record
)
{
    inferencefs_primary_record value;

    if (disk == NULL || record == NULL) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    if ((disk->attributes != INFERENCEFS_PRIMARY_ATTRIBUTE_REGULAR_FILE
            && disk->attributes != INFERENCEFS_PRIMARY_ATTRIBUTE_DIRECTORY)
        || !supported_name(disk->name)
        || disk->reserved != 0U || disk->create_tenth != 0U
        || inferenceos_load_le16(disk->create_time_le) != 0U
        || inferenceos_load_le16(disk->create_date_le) != 0U
        || inferenceos_load_le16(disk->access_date_le) != 0U
        || inferenceos_load_le16(disk->write_time_le) != 0U
        || inferenceos_load_le16(disk->write_date_le) != 0U) {
        return INFERENCEOS_RESULT_CORRUPT;
    }
    (void)memcpy(value.name, disk->name, sizeof(value.name));
    value.attributes = disk->attributes;
    value.first_cluster = ((inferenceos_u32)inferenceos_load_le16(
        disk->first_cluster_high_le) << 16U)
        | inferenceos_load_le16(disk->first_cluster_low_le);
    value.file_size = inferenceos_load_le32(disk->file_size_le);
    if (value.attributes == INFERENCEFS_PRIMARY_ATTRIBUTE_DIRECTORY
        && value.file_size != 0U) {
        return INFERENCEOS_RESULT_CORRUPT;
    }
    *record = value;
    return INFERENCEOS_RESULT_OK;
}
