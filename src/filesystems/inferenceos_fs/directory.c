#include <inferenceos/fs/directory.h>

#include <inferenceos/runtime.h>

static const ios_u8 *slot_at(const ios_u8 *slots, ios_size index)
{
    return slots + index * IOS_FS_PRIMARY_RECORD_SIZE;
}

static bool is_internal_name(const ios_u8 name[IOS_FS_NAME_SIZE])
{
    if (*name != '.') return false;
    if (name[1] == '.') {
        for (ios_size index = 2; index < IOS_FS_NAME_SIZE; ++index) {
            if (name[index] != ' ') return false;
        }
        return true;
    }
    for (ios_size index = 1; index < IOS_FS_NAME_SIZE; ++index) {
        if (name[index] != ' ') return false;
    }
    return true;
}

static ios_status decode_internal(
    const struct ios_fs_primary_disk *disk, struct ios_fs_primary *primary
)
{
    const ios_u8 *bytes = (const ios_u8 *)disk;
    ios_u32 cluster;
    if (!is_internal_name(disk->name) || disk->attributes != IOS_FS_ATTRIBUTE_DIRECTORY) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    for (ios_size index = 0x0c; index < 0x14; ++index) if (bytes[index] != 0) return IOS_ERROR(IOS_E_PROTOCOL);
    for (ios_size index = 0x16; index < 0x1a; ++index) if (bytes[index] != 0) return IOS_ERROR(IOS_E_PROTOCOL);
    for (ios_size index = 0x1c; index < 0x20; ++index) if (bytes[index] != 0) return IOS_ERROR(IOS_E_PROTOCOL);
    cluster = (ios_u32)*disk->first_cluster_high << 16
              | (ios_u32)disk->first_cluster_high[1] << 24
              | (ios_u32)*disk->first_cluster_low
              | (ios_u32)disk->first_cluster_low[1] << 8;
    if (cluster < 2 || cluster > IOS_FS_MAX_DATA_CLUSTER) return IOS_ERROR(IOS_E_PROTOCOL);
    memset(primary, 0, sizeof(*primary));
    memcpy(primary->name, disk->name, sizeof(primary->name));
    primary->attributes = disk->attributes;
    primary->first_cluster = cluster;
    return IOS_OK;
}

static bool duplicate_name(
    const struct ios_fs_directory_entry *entries,
    ios_size entry_count,
    const ios_u8 name[IOS_FS_NAME_SIZE]
)
{
    for (ios_size index = 0; index < entry_count; ++index) {
        if (entries[index].kind != IOS_FS_DIRECTORY_ENTRY_INTERNAL
            && memcmp(entries[index].primary.name, name, IOS_FS_NAME_SIZE) == 0) return true;
    }
    return false;
}

ios_status ios_fs_directory_scan(
    const ios_u8 *slots,
    ios_size slot_count,
    ios_size slots_per_cluster,
    struct ios_fs_directory_entry *entries,
    ios_size entry_capacity,
    ios_size *entry_count
)
{
    ios_size output_count = 0;
    if (slots == NULL || entries == NULL || entry_count == NULL || slot_count == 0
        || slot_count > SIZE_MAX / IOS_FS_PRIMARY_RECORD_SIZE
        || slots_per_cluster == 0 || slot_count % slots_per_cluster != 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *entry_count = 0;
    for (ios_size slot = 0; slot < slot_count; ++slot) {
        const ios_u8 *bytes = slot_at(slots, slot);
        struct ios_fs_directory_entry entry;
        ios_status status;
        if (*bytes == 0) break;
        if (*bytes == 0xe5) continue;
        memset(&entry, 0, sizeof(entry));
        if (*bytes == IOS_FS_COMPANION_RECORD_TYPE) {
            if (slot + 1 >= slot_count || slot % slots_per_cluster + 1 >= slots_per_cluster) {
                return IOS_ERROR(IOS_E_CORRUPT);
            }
            status = ios_fs_record_pair_validate(
                (const struct ios_fs_companion_disk *)bytes,
                (const struct ios_fs_primary_disk *)slot_at(slots, slot + 1)
            );
            if (IOS_FAILED(status)) return IOS_ERROR(IOS_E_CORRUPT);
            status = ios_fs_primary_decode(
                (const struct ios_fs_primary_disk *)slot_at(slots, slot + 1), &entry.primary
            );
            if (IOS_FAILED(status)) return IOS_ERROR(IOS_E_CORRUPT);
            entry.kind = IOS_FS_DIRECTORY_ENTRY_REGULAR;
            entry.companion_slot = slot;
            entry.primary_slot = slot + 1;
            ++slot;
        } else {
            const struct ios_fs_primary_disk *disk = (const struct ios_fs_primary_disk *)bytes;
            if (is_internal_name(disk->name)) {
                status = decode_internal(disk, &entry.primary);
                entry.kind = IOS_FS_DIRECTORY_ENTRY_INTERNAL;
            } else {
                status = ios_fs_primary_decode(disk, &entry.primary);
                entry.kind = IOS_FS_DIRECTORY_ENTRY_DIRECTORY;
            }
            if (IOS_FAILED(status)) return status;
            if (entry.kind == IOS_FS_DIRECTORY_ENTRY_DIRECTORY
                && entry.primary.attributes != IOS_FS_ATTRIBUTE_DIRECTORY) {
                return IOS_ERROR(IOS_E_CORRUPT);
            }
            entry.primary_slot = slot;
            entry.companion_slot = SIZE_MAX;
        }
        if (duplicate_name(entries, output_count, entry.primary.name)) {
            return IOS_ERROR(IOS_E_ALREADY_EXISTS);
        }
        if (output_count == entry_capacity) return IOS_ERROR(IOS_E_NO_SPACE);
        entries[output_count++] = entry;
        *entry_count = output_count;
    }
    return IOS_OK;
}

static bool slot_available(const ios_u8 *bytes, bool after_end)
{
    return after_end || *bytes == 0 || *bytes == 0xe5;
}

ios_status ios_fs_directory_find_pair_slots(
    const ios_u8 *slots,
    ios_size slot_count,
    ios_size slots_per_cluster,
    ios_size *companion_slot
)
{
    bool after_end = false;
    if (slots == NULL || companion_slot == NULL || slot_count < 2
        || slot_count > SIZE_MAX / IOS_FS_PRIMARY_RECORD_SIZE
        || slots_per_cluster < 2 || slot_count % slots_per_cluster != 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    for (ios_size slot = 0; slot + 1 < slot_count; ++slot) {
        const ios_u8 *current = slot_at(slots, slot);
        const ios_u8 *next = slot_at(slots, slot + 1);
        if (*current == 0) after_end = true;
        if (slot % slots_per_cluster + 1 < slots_per_cluster
            && slot_available(current, after_end)
            && slot_available(next, after_end || *next == 0)) {
            *companion_slot = slot;
            return IOS_OK;
        }
    }
    return IOS_ERROR(IOS_E_NO_SPACE);
}
