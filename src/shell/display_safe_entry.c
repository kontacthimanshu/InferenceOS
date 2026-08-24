#include <inferenceos/display_safe_entry.h>

#include <inferenceos/runtime.h>

#define IOS_DISPLAY_SAFE_ATTRIBUTE_MASK ((ios_u32)IOS_DISPLAY_SAFE_ATTRIBUTE_READ_ONLY)
#define IOS_DISPLAY_SAFE_OPERATION_MASK ( \
    (ios_u64)IOS_DISPLAY_SAFE_OPERATION_OPEN \
    | (ios_u64)IOS_DISPLAY_SAFE_OPERATION_READ \
    | (ios_u64)IOS_DISPLAY_SAFE_OPERATION_WRITE \
    | (ios_u64)IOS_DISPLAY_SAFE_OPERATION_RENAME \
    | (ios_u64)IOS_DISPLAY_SAFE_OPERATION_DELETE \
    | (ios_u64)IOS_DISPLAY_SAFE_OPERATION_ENUMERATE \
)

static bool valid_kind(enum ios_display_safe_object_kind kind)
{
    return kind == IOS_DISPLAY_SAFE_REGULAR_FILE || kind == IOS_DISPLAY_SAFE_DIRECTORY;
}

static ios_status validate_base_name(const char *name, ios_size *length)
{
    ios_size count = 0;
    if (name == NULL || length == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    while (count < IOS_DISPLAY_SAFE_NAME_CAPACITY && name[count] != '\0') {
        const unsigned char character = (unsigned char)name[count];
        if (character < 0x20 || character > 0x7e || character == '.'
            || character == '/' || character == '\\') {
            return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
        }
        ++count;
    }
    if (count == 0 || count == IOS_DISPLAY_SAFE_NAME_CAPACITY) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *length = count;
    return IOS_OK;
}

static ios_status validate_entry(const struct ios_display_safe_entry *entry)
{
    ios_size length;
    if (entry == NULL || entry->size != sizeof(*entry)
        || entry->version != IOS_DISPLAY_SAFE_ENTRY_VERSION || entry->object_handle == 0
        || !valid_kind((enum ios_display_safe_object_kind)entry->object_kind)
        || (entry->generic_attributes & ~IOS_DISPLAY_SAFE_ATTRIBUTE_MASK) != 0
        || (entry->allowed_operations & ~IOS_DISPLAY_SAFE_OPERATION_MASK) != 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (entry->object_kind == IOS_DISPLAY_SAFE_REGULAR_FILE
        && entry->type_icon_capability == 0) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    for (ios_size index = 0; index < sizeof(entry->reserved); ++index) {
        if (entry->reserved[index] != 0) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (IOS_FAILED(validate_base_name(entry->display_name, &length))
        || length != entry->display_name_length) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    return IOS_OK;
}

ios_status ios_display_safe_entry_convert(
    const struct ios_display_safe_source_entry *source,
    struct ios_display_safe_entry *entry
)
{
    ios_size name_length;
    ios_status status;
    if (entry == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    memset(entry, 0, sizeof(*entry));
    if (source == NULL || source->object_handle == 0 || !valid_kind(source->object_kind)
        || (source->generic_attributes & ~IOS_DISPLAY_SAFE_ATTRIBUTE_MASK) != 0
        || (source->allowed_operations & ~IOS_DISPLAY_SAFE_OPERATION_MASK) != 0
        || (source->object_kind == IOS_DISPLAY_SAFE_REGULAR_FILE
            && source->type_icon_capability == 0)) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    status = validate_base_name(source->base_name, &name_length);
    if (IOS_FAILED(status)) return status;
    entry->size = sizeof(*entry);
    entry->version = IOS_DISPLAY_SAFE_ENTRY_VERSION;
    entry->generic_attributes = source->generic_attributes;
    entry->object_handle = source->object_handle;
    entry->type_icon_capability = source->type_icon_capability;
    entry->byte_size = source->byte_size;
    entry->allowed_operations = source->allowed_operations;
    entry->object_kind = (ios_u8)source->object_kind;
    entry->display_name_length = (ios_u8)name_length;
    memcpy(entry->display_name, source->base_name, name_length + 1);
    return IOS_OK;
}

static ios_size decimal_digits(ios_size value)
{
    ios_size count = 1;
    while (value >= 10) {
        value /= 10;
        ++count;
    }
    return count;
}

static void append_rank(struct ios_display_safe_entry *entry, ios_size rank)
{
    char reversed[sizeof(ios_size) * 3];
    ios_size digit_count = 0;
    ios_size cursor = entry->display_name_length;
    do {
        reversed[digit_count++] = (char)('0' + rank % 10);
        rank /= 10;
    } while (rank != 0);
    entry->display_name[cursor++] = ' ';
    entry->display_name[cursor++] = '(';
    while (digit_count != 0) entry->display_name[cursor++] = reversed[--digit_count];
    entry->display_name[cursor++] = ')';
    entry->display_name[cursor] = '\0';
    entry->display_name_length = (ios_u8)cursor;
}

ios_status ios_display_safe_entries_disambiguate(
    struct ios_display_safe_entry *entries, ios_size count,
    ios_size *rank_workspace, ios_size workspace_count
)
{
    if ((entries == NULL && count != 0) || (rank_workspace == NULL && count != 0)
        || workspace_count < count) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    for (ios_size index = 0; index < count; ++index) {
        ios_status status = validate_entry(&entries[index]);
        if (IOS_FAILED(status)) return status;
        rank_workspace[index] = 1;
        for (ios_size other = 0; other < count; ++other) {
            if (other == index) continue;
            if (entries[other].object_handle == entries[index].object_handle) {
                return IOS_ERROR(IOS_E_CORRUPT);
            }
            if (entries[other].display_name_length == entries[index].display_name_length
                && memcmp(entries[other].display_name, entries[index].display_name,
                          entries[index].display_name_length) == 0
                && entries[other].object_handle < entries[index].object_handle) {
                ++rank_workspace[index];
            }
        }
    }
    for (ios_size index = 0; index < count; ++index) {
        if (rank_workspace[index] > 1
            && entries[index].display_name_length + decimal_digits(rank_workspace[index]) + 3
                >= IOS_DISPLAY_SAFE_NAME_CAPACITY) return IOS_ERROR(IOS_E_NO_SPACE);
    }
    for (ios_size index = 0; index < count; ++index) {
        if (rank_workspace[index] > 1) append_rank(&entries[index], rank_workspace[index]);
    }
    return IOS_OK;
}
