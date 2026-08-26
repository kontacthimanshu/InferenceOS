#include <inferenceos/fs/registry.h>

#include <inferenceos/runtime.h>

static bool all_zero(const ios_u8 *bytes, ios_size length)
{
    for (ios_size index = 0; index < length; ++index) {
        if (bytes[index] != 0) return false;
    }
    return true;
}

static bool empty_slot(const struct ios_fs_registry_record_disk *record)
{
    return all_zero((const ios_u8 *)record, sizeof(*record));
}

static bool supported_extension_byte(ios_u8 value)
{
    return (value >= 'A' && value <= 'Z')
           || (value >= '0' && value <= '9')
           || value == '_' || value == '-';
}

static bool valid_extension(const ios_u8 *extension, ios_size length)
{
    if (extension == NULL || length > IOS_FS_EXTENSION_SIZE) return false;
    for (ios_size index = 0; index < length; ++index) {
        if (!supported_extension_byte(extension[index])) return false;
    }
    return true;
}

static bool same_extension(
    const ios_u8 *left,
    ios_size left_length,
    const ios_u8 *right,
    ios_size right_length
)
{
    return left_length == right_length
           && memcmp(left, right, left_length) == 0;
}

static ios_status source_extension(
    const struct ios_fs_registry_source_entry *entry,
    ios_u8 extension[IOS_FS_EXTENSION_SIZE],
    ios_size *extension_length
)
{
    struct ios_fs_primary primary;
    ios_status status;
    if (entry == NULL || extension == NULL || extension_length == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    status = ios_fs_record_pair_validate(&entry->companion, &entry->primary);
    if (IOS_FAILED(status)) return status;
    status = ios_fs_primary_decode(&entry->primary, &primary);
    if (IOS_FAILED(status)) return status;
    return ios_fs_name_extension(primary.name, extension, extension_length);
}

static ios_status find_active_record(
    struct ios_fs_registry *registry,
    const ios_u8 *canonical_extension,
    ios_size extension_length,
    ios_size *record_index,
    struct ios_fs_registry_record *record
)
{
    bool found = false;
    for (ios_size index = 0; index < registry->capacity; ++index) {
        struct ios_fs_registry_record decoded;
        if (empty_slot(&registry->records[index])) continue;
        ios_status status = ios_fs_registry_record_decode(
            &registry->records[index], &decoded
        );
        if (IOS_FAILED(status)) {
            registry->health = IOS_FS_REGISTRY_CORRUPT;
            return IOS_ERROR(IOS_E_CORRUPT);
        }
        if (decoded.active && same_extension(
            decoded.canonical_extension, decoded.extension_length,
            canonical_extension, extension_length
        )) {
            if (found) {
                registry->health = IOS_FS_REGISTRY_CORRUPT;
                return IOS_ERROR(IOS_E_CORRUPT);
            }
            found = true;
            if (record_index != NULL) *record_index = index;
            if (record != NULL) *record = decoded;
        }
    }
    return found ? IOS_OK : IOS_ERROR(IOS_E_NOT_FOUND);
}

ios_status ios_fs_registry_initialize(
    struct ios_fs_registry *registry,
    struct ios_fs_registry_record_disk *records,
    ios_size capacity,
    bool enabled
)
{
    if (registry == NULL || records == NULL || capacity == 0
        || capacity > SIZE_MAX / sizeof(*records)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *registry = (struct ios_fs_registry){
        .records = records,
        .capacity = capacity,
        .enabled = enabled,
        .health = enabled ? IOS_FS_REGISTRY_HEALTHY : IOS_FS_REGISTRY_DISABLED
    };
    if (!enabled) return IOS_OK;
    for (ios_size index = 0; index < capacity; ++index) {
        struct ios_fs_registry_record current;
        if (empty_slot(&records[index])) continue;
        if (IOS_FAILED(ios_fs_registry_record_decode(&records[index], &current))) {
            registry->health = IOS_FS_REGISTRY_CORRUPT;
            return IOS_OK;
        }
        if (!current.active) continue;
        for (ios_size prior = 0; prior < index; ++prior) {
            struct ios_fs_registry_record previous;
            if (empty_slot(&records[prior])) continue;
            if (IOS_FAILED(ios_fs_registry_record_decode(
                &records[prior], &previous
            ))) {
                registry->health = IOS_FS_REGISTRY_CORRUPT;
                return IOS_OK;
            }
            if (previous.active && same_extension(
                current.canonical_extension, current.extension_length,
                previous.canonical_extension, previous.extension_length
            )) {
                registry->health = IOS_FS_REGISTRY_CORRUPT;
                return IOS_OK;
            }
        }
    }
    return IOS_OK;
}

bool ios_fs_registry_enabled(const struct ios_fs_registry *registry)
{
    return registry != NULL && registry->enabled;
}

enum ios_fs_registry_health ios_fs_registry_health(
    const struct ios_fs_registry *registry
)
{
    return registry == NULL ? IOS_FS_REGISTRY_CORRUPT : registry->health;
}

ios_size ios_fs_registry_active_count(const struct ios_fs_registry *registry)
{
    ios_size count = 0;
    if (registry == NULL || !registry->enabled
        || registry->health == IOS_FS_REGISTRY_CORRUPT
        || registry->health == IOS_FS_REGISTRY_REBUILDING) {
        return 0;
    }
    for (ios_size index = 0; index < registry->capacity; ++index) {
        struct ios_fs_registry_record record;
        if (empty_slot(&registry->records[index])) continue;
        if (IOS_FAILED(ios_fs_registry_record_decode(
            &registry->records[index], &record
        ))) {
            return 0;
        }
        if (record.active) ++count;
    }
    return count;
}

ios_status ios_fs_registry_find(
    struct ios_fs_registry *registry,
    const ios_u8 *canonical_extension,
    ios_size extension_length,
    ios_size *record_index,
    struct ios_fs_registry_record *record
)
{
    if (registry == NULL || record_index == NULL || record == NULL
        || !valid_extension(canonical_extension, extension_length)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *record_index = SIZE_MAX;
    memset(record, 0, sizeof(*record));
    if (!registry->enabled || registry->health == IOS_FS_REGISTRY_CORRUPT
        || registry->health == IOS_FS_REGISTRY_REBUILDING) {
        return IOS_ERROR(IOS_E_NOT_FOUND);
    }
    return find_active_record(
        registry, canonical_extension, extension_length, record_index, record
    );
}

ios_status ios_fs_registry_refresh(
    struct ios_fs_registry *registry,
    const struct ios_fs_companion_disk *companion,
    const struct ios_fs_primary_disk *primary,
    ios_u32 directory_cluster,
    ios_u16 primary_slot,
    ios_size *record_index
)
{
    struct ios_fs_registry_source_entry source;
    struct ios_fs_registry_record previous;
    struct ios_fs_registry_record replacement;
    struct ios_fs_registry_record_disk encoded;
    ios_u8 extension[IOS_FS_EXTENSION_SIZE];
    ios_size extension_length;
    ios_size target = SIZE_MAX;
    ios_size available = SIZE_MAX;
    bool was_full;
    ios_status status;
    if (registry == NULL || companion == NULL || primary == NULL
        || record_index == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *record_index = SIZE_MAX;
    if (!registry->enabled) return IOS_OK;
    was_full = registry->health == IOS_FS_REGISTRY_FULL;
    if (registry->health == IOS_FS_REGISTRY_CORRUPT
        || registry->health == IOS_FS_REGISTRY_STALE) {
        return IOS_ERROR(IOS_E_CORRUPT);
    }
    if (registry->health == IOS_FS_REGISTRY_REBUILDING) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    memset(&source, 0, sizeof(source));
    source.companion = *companion;
    source.primary = *primary;
    source.directory_cluster = directory_cluster;
    source.primary_slot = primary_slot;
    status = source_extension(&source, extension, &extension_length);
    if (IOS_FAILED(status)) return status;

    memset(&previous, 0, sizeof(previous));
    for (ios_size index = 0; index < registry->capacity; ++index) {
        struct ios_fs_registry_record decoded;
        if (empty_slot(&registry->records[index])) {
            if (available == SIZE_MAX) available = index;
            continue;
        }
        status = ios_fs_registry_record_decode(
            &registry->records[index], &decoded
        );
        if (IOS_FAILED(status)) {
            registry->health = IOS_FS_REGISTRY_CORRUPT;
            return IOS_ERROR(IOS_E_CORRUPT);
        }
        if (!decoded.active) {
            if (available == SIZE_MAX) available = index;
            continue;
        }
        if (same_extension(
            decoded.canonical_extension, decoded.extension_length,
            extension, extension_length
        )) {
            if (target != SIZE_MAX) {
                registry->health = IOS_FS_REGISTRY_CORRUPT;
                return IOS_ERROR(IOS_E_CORRUPT);
            }
            target = index;
            previous = decoded;
        }
    }
    if (target == SIZE_MAX && !was_full) target = available;
    if (target == SIZE_MAX) {
        registry->health = IOS_FS_REGISTRY_FULL;
        return IOS_ERROR(IOS_E_NO_SPACE);
    }
    memset(&replacement, 0, sizeof(replacement));
    replacement.active = true;
    replacement.extension_length = (ios_u8)extension_length;
    memcpy(
        replacement.canonical_extension, extension, extension_length
    );
    replacement.last_directory_cluster = directory_cluster;
    replacement.last_directory_slot = primary_slot;
    replacement.update_generation = previous.active
        ? (ios_u16)(previous.update_generation + 1) : 1;
    status = ios_fs_registry_record_encode(&replacement, &encoded);
    if (IOS_FAILED(status)) return status;
    registry->records[target] = encoded;
    *record_index = target;
    return IOS_OK;
}

static ios_status validate_rebuild_entries(
    const struct ios_fs_registry_source_entry *entries,
    ios_size entry_count,
    ios_size capacity
)
{
    ios_size unique_count = 0;
    for (ios_size index = 0; index < entry_count; ++index) {
        ios_u8 extension[IOS_FS_EXTENSION_SIZE];
        ios_size extension_length;
        bool duplicate = false;
        ios_status status = source_extension(
            &entries[index], extension, &extension_length
        );
        if (IOS_FAILED(status)) return status;
        for (ios_size prior = 0; prior < index; ++prior) {
            ios_u8 prior_extension[IOS_FS_EXTENSION_SIZE];
            ios_size prior_length;
            status = source_extension(
                &entries[prior], prior_extension, &prior_length
            );
            if (IOS_FAILED(status)) return status;
            if (same_extension(
                extension, extension_length, prior_extension, prior_length
            )) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate && ++unique_count > capacity) {
            return IOS_ERROR(IOS_E_NO_SPACE);
        }
    }
    return IOS_OK;
}

ios_status ios_fs_registry_rebuild(
    struct ios_fs_registry *registry,
    const struct ios_fs_registry_source_entry *entries,
    ios_size entry_count
)
{
    ios_size active_count = 0;
    ios_status status;
    if (registry == NULL || (entries == NULL && entry_count != 0)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (!registry->enabled) return IOS_OK;
    status = validate_rebuild_entries(entries, entry_count, registry->capacity);
    if (status == IOS_ERROR(IOS_E_NO_SPACE)) {
        registry->health = IOS_FS_REGISTRY_FULL;
        return status;
    }
    if (IOS_FAILED(status)) return status;
    registry->health = IOS_FS_REGISTRY_REBUILDING;
    memset(
        registry->records, 0,
        registry->capacity * sizeof(*registry->records)
    );
    for (ios_size index = 0; index < entry_count; ++index) {
        struct ios_fs_registry_record record;
        struct ios_fs_registry_record_disk encoded;
        ios_u8 extension[IOS_FS_EXTENSION_SIZE];
        ios_size extension_length;
        ios_size target = SIZE_MAX;

        status = source_extension(
            &entries[index], extension, &extension_length
        );
        if (IOS_FAILED(status)) {
            registry->health = IOS_FS_REGISTRY_CORRUPT;
            return status;
        }
        for (ios_size slot = 0; slot < active_count; ++slot) {
            status = ios_fs_registry_record_decode(
                &registry->records[slot], &record
            );
            if (IOS_FAILED(status)) {
                registry->health = IOS_FS_REGISTRY_CORRUPT;
                return status;
            }
            if (same_extension(
                record.canonical_extension, record.extension_length,
                extension, extension_length
            )) {
                target = slot;
                break;
            }
        }
        if (target == SIZE_MAX) {
            target = active_count++;
            memset(&record, 0, sizeof(record));
        }
        record.active = true;
        record.extension_length = (ios_u8)extension_length;
        memset(
            record.canonical_extension, 0,
            sizeof(record.canonical_extension)
        );
        memcpy(record.canonical_extension, extension, extension_length);
        record.last_directory_cluster = entries[index].directory_cluster;
        record.last_directory_slot = entries[index].primary_slot;
        record.update_generation = (ios_u16)(record.update_generation + 1);
        status = ios_fs_registry_record_encode(&record, &encoded);
        if (IOS_FAILED(status)) {
            registry->health = IOS_FS_REGISTRY_CORRUPT;
            return status;
        }
        registry->records[target] = encoded;
    }
    registry->health = IOS_FS_REGISTRY_HEALTHY;
    return IOS_OK;
}

ios_status ios_fs_registry_lookup(
    struct ios_fs_registry *registry,
    const struct ios_fs_registry_source_entry *entries,
    ios_size entry_count,
    const ios_u8 *canonical_extension,
    ios_size extension_length,
    ios_size *matches,
    ios_size match_capacity,
    ios_size *match_count
)
{
    struct ios_fs_registry_record hint;
    ios_size hint_index;
    ios_size output_count = 0;
    bool inspect_hint;
    bool hint_found = false;
    bool hint_valid = false;
    ios_status status;
    if (registry == NULL || (entries == NULL && entry_count != 0)
        || (matches == NULL && match_capacity != 0) || match_count == NULL
        || !valid_extension(canonical_extension, extension_length)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *match_count = 0;
    inspect_hint = registry->enabled
                   && registry->health == IOS_FS_REGISTRY_HEALTHY;
    if (inspect_hint) {
        status = find_active_record(
            registry, canonical_extension, extension_length,
            &hint_index, &hint
        );
        if (IOS_SUCCEEDED(status)) hint_found = true;
        else if (status != IOS_ERROR(IOS_E_NOT_FOUND)) inspect_hint = false;
    }
    for (ios_size index = 0; index < entry_count; ++index) {
        ios_u8 extension[IOS_FS_EXTENSION_SIZE];
        ios_size length;
        status = source_extension(&entries[index], extension, &length);
        if (IOS_FAILED(status)) return status;
        if (hint_found
            && entries[index].directory_cluster == hint.last_directory_cluster
            && entries[index].primary_slot == hint.last_directory_slot
            && same_extension(
                extension, length, canonical_extension, extension_length
            )) {
            hint_valid = true;
        }
        if (!same_extension(
            extension, length, canonical_extension, extension_length
        )) {
            continue;
        }
        if (output_count == match_capacity) {
            *match_count = output_count;
            return IOS_ERROR(IOS_E_NO_SPACE);
        }
        matches[output_count++] = index;
        *match_count = output_count;
    }
    if (inspect_hint
        && ((hint_found && !hint_valid)
            || (!hint_found && output_count != 0))) {
        registry->health = IOS_FS_REGISTRY_STALE;
    }
    return IOS_OK;
}
