#include <inferenceos/fs/file_service.h>

#include <inferenceos/fs/fat.h>
#include <inferenceos/runtime.h>

enum {
    FAT_ENTRIES_PER_SECTOR = IOS_FS_SECTOR_SIZE / sizeof(ios_u32),
    DIRECTORY_SLOTS_PER_SECTOR = IOS_FS_SECTOR_SIZE / IOS_FS_PRIMARY_RECORD_SIZE,
    COMPONENT_CAPACITY = 13
};

#define IOS_FS_FILE_OBJECT_BIT (UINT64_C(1) << 63)

struct located_entry {
    struct ios_fs_primary_disk primary_disk;
    struct ios_fs_companion_disk companion_disk;
    struct ios_fs_primary primary;
    ios_u32 directory_cluster;
    ios_u16 primary_slot;
    ios_u16 companion_slot;
    bool has_companion;
};

struct extension_search_state {
    struct ios_vfs_search_result *entries;
    ios_size capacity;
    ios_size count;
    ios_u8 extension[IOS_FS_EXTENSION_SIZE];
    ios_size extension_length;
    ios_u8 hash[IOS_FS_HASH_TEXT_SIZE];
    bool truncated;
    bool stop;
};

static ios_status validate_service(const struct ios_fs_file_service *service, bool mutation)
{
    if (service == NULL || !service->initialized || service->mount == NULL
        || service->sync == NULL || service->sync->cache == NULL || service->fat == NULL) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    if (mutation && service->mount->vfs.state != IOS_MOUNT_RW) {
        return IOS_ERROR(IOS_E_READ_ONLY);
    }
    return IOS_OK;
}

static bool valid_directory_cluster(
    const struct ios_fs_file_service *service, ios_u64 identity
)
{
    return service != NULL && identity >= 2 && identity < service->fat_entry_count
        && (identity & IOS_FS_FILE_OBJECT_BIT) == 0;
}

static ios_u64 cluster_sector(
    const struct ios_fs_file_service *service, ios_u32 cluster
)
{
    return service->mount->geometry.data_start_sector
        + (ios_u64)(cluster - 2U) * IOS_FS_SECTORS_PER_CLUSTER;
}

static ios_u64 record_location(
    const struct ios_fs_file_service *service, ios_u32 cluster, ios_u16 slot
)
{
    return (cluster_sector(service, cluster)
            + slot / DIRECTORY_SLOTS_PER_SECTOR) * IOS_FS_SECTOR_SIZE
        + (slot % DIRECTORY_SLOTS_PER_SECTOR) * IOS_FS_PRIMARY_RECORD_SIZE;
}

static ios_u64 file_identity(
    const struct ios_fs_file_service *service, const struct located_entry *entry
)
{
    return IOS_FS_FILE_OBJECT_BIT
        | record_location(service, entry->directory_cluster, entry->primary_slot);
}

static ios_status canonical_component(
    const char *component, ios_size length, ios_u8 name[IOS_FS_NAME_SIZE]
)
{
    char text[COMPONENT_CAPACITY];
    if (component == NULL || name == NULL || length == 0 || length >= sizeof(text)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    memcpy(text, component, length);
    text[length] = '\0';
    return ios_fs_name_canonicalize_83(text, name);
}

static ios_status read_cluster(
    struct ios_fs_file_service *service, ios_u32 cluster, ios_u8 *buffer
)
{
    if (!valid_directory_cluster(service, cluster) || buffer == NULL) {
        return IOS_ERROR(IOS_E_CORRUPT);
    }
    for (ios_size sector = 0; sector < IOS_FS_SECTORS_PER_CLUSTER; ++sector) {
        ios_status status = block_cache_read(
            service->sync->cache, cluster_sector(service, cluster) + sector,
            buffer + sector * IOS_FS_SECTOR_SIZE
        );
        if (IOS_FAILED(status)) return status;
    }
    return IOS_OK;
}

static ios_status write_cluster(
    struct ios_fs_file_service *service,
    ios_u32 cluster,
    const ios_u8 *buffer,
    enum ios_fs_dirty_component component
)
{
    if (!valid_directory_cluster(service, cluster) || buffer == NULL) {
        return IOS_ERROR(IOS_E_CORRUPT);
    }
    for (ios_size sector = 0; sector < IOS_FS_SECTORS_PER_CLUSTER; ++sector) {
        ios_status status = ios_fs_sync_write_sector(
            service->sync, cluster_sector(service, cluster) + sector,
            buffer + sector * IOS_FS_SECTOR_SIZE, component
        );
        if (IOS_FAILED(status)) return status;
    }
    return IOS_OK;
}

static ios_status load_fat(struct ios_fs_file_service *service)
{
    ios_u8 *bytes = (ios_u8 *)service->fat;
    for (ios_u32 sector = 0; sector < service->mount->geometry.fat_sectors; ++sector) {
        ios_status status = block_cache_read(
            service->sync->cache, IOS_FS_SUPERBLOCK_SECTORS + sector,
            bytes + (ios_size)sector * IOS_FS_SECTOR_SIZE
        );
        if (IOS_FAILED(status)) return status;
    }
    service->dirty_fat_sector_count = 0;
    return IOS_OK;
}

static ios_status directory_chain(
    struct ios_fs_file_service *service, ios_u32 first_cluster, ios_size *count
)
{
    return ios_fs_fat_traverse(
        service->fat, service->fat_entry_count, first_cluster,
        service->chain, IOS_ARRAY_COUNT(service->chain), count
    );
}

static ios_status scan_loaded_directory(
    struct ios_fs_file_service *service, ios_size *entry_count
)
{
    return ios_fs_directory_scan(
        service->directory,
        IOS_FS_FILE_SERVICE_DIRECTORY_SLOTS,
        IOS_FS_FILE_SERVICE_DIRECTORY_SLOTS,
        service->directory_entries,
        IOS_ARRAY_COUNT(service->directory_entries),
        entry_count
    );
}

static ios_size display_base_length(const ios_u8 name[IOS_FS_NAME_SIZE])
{
    ios_size length = 8;
    while (length != 0 && name[length - 1U] == ' ') --length;
    return length;
}

static ios_status append_display_component(
    char path[IOS_VFS_PATH_CAPACITY],
    ios_size *path_length,
    const ios_u8 name[IOS_FS_NAME_SIZE]
)
{
    const ios_size component_length = display_base_length(name);
    const bool needs_separator = *path_length > 1U;
    const ios_size additional = component_length + (needs_separator ? 1U : 0U);
    if (component_length == 0
        || *path_length > IOS_VFS_PATH_MAX
        || additional > IOS_VFS_PATH_MAX - *path_length) {
        return IOS_ERROR(IOS_E_OVERFLOW);
    }
    if (needs_separator) path[(*path_length)++] = '/';
    memcpy(path + *path_length, name, component_length);
    *path_length += component_length;
    path[*path_length] = '\0';
    return IOS_OK;
}

ios_status ios_fs_record_pair_matches_extension(
    const struct ios_fs_primary_disk *primary_disk,
    const struct ios_fs_companion_disk *companion_disk,
    const ios_u8 canonical_extension[IOS_FS_EXTENSION_SIZE],
    ios_size extension_length,
    const ios_u8 extension_hash[IOS_FS_HASH_TEXT_SIZE],
    bool *matches
)
{
    struct ios_fs_primary primary;
    ios_u8 authoritative[IOS_FS_EXTENSION_SIZE];
    ios_size authoritative_length;
    ios_status status;
    if (primary_disk == NULL || companion_disk == NULL
        || canonical_extension == NULL || extension_hash == NULL || matches == NULL
        || extension_length == 0 || extension_length > IOS_FS_EXTENSION_SIZE) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *matches = false;
    status = ios_fs_record_pair_validate(companion_disk, primary_disk);
    if (IOS_FAILED(status)) return status;
    status = ios_fs_primary_decode(primary_disk, &primary);
    if (IOS_FAILED(status)) return status;
    status = ios_fs_name_extension(primary.name, authoritative, &authoritative_length);
    if (IOS_FAILED(status)) return IOS_ERROR(IOS_E_CORRUPT);
    if (memcmp(
            companion_disk->extension_hash_text,
            extension_hash,
            IOS_FS_HASH_TEXT_SIZE
        ) != 0) return IOS_OK;
    *matches = authoritative_length == extension_length
        && memcmp(authoritative, canonical_extension, extension_length) == 0;
    return IOS_OK;
}

static ios_status search_directory(
    struct ios_fs_file_service *service,
    ios_u32 directory_cluster,
    ios_size depth,
    char path[IOS_VFS_PATH_CAPACITY],
    ios_size path_length,
    struct extension_search_state *search
)
{
    ios_size chain_count;
    ios_status status = directory_chain(service, directory_cluster, &chain_count);
    if (IOS_FAILED(status)) return status;
    for (ios_size chain_index = 0;
         chain_index < chain_count && !search->stop; ++chain_index) {
        ios_u32 cluster;
        ios_size entry_count;
        status = directory_chain(service, directory_cluster, &chain_count);
        if (IOS_FAILED(status)) return status;
        if (chain_index >= chain_count) return IOS_ERROR(IOS_E_CORRUPT);
        cluster = service->chain[chain_index];
        status = read_cluster(service, cluster, service->directory);
        if (IOS_FAILED(status)) return status;
        status = scan_loaded_directory(service, &entry_count);
        if (IOS_FAILED(status)) return status;
        for (ios_size index = 0; index < entry_count && !search->stop; ++index) {
            const struct ios_fs_directory_entry entry =
                service->directory_entries[index];
            char child_path[IOS_VFS_PATH_CAPACITY];
            ios_size child_length = path_length;
            if (entry.kind == IOS_FS_DIRECTORY_ENTRY_INTERNAL) continue;
            memcpy(child_path, path, path_length + 1U);
            status = append_display_component(
                child_path, &child_length, entry.primary.name
            );
            if (IOS_FAILED(status)) return status;
            if (entry.kind == IOS_FS_DIRECTORY_ENTRY_DIRECTORY) {
                if (depth >= IOS_VFS_MAX_DIRECTORY_LEVELS) {
                    return IOS_ERROR(IOS_E_CORRUPT);
                }
                status = search_directory(
                    service, entry.primary.first_cluster, depth + 1U,
                    child_path, child_length, search
                );
                if (IOS_FAILED(status)) return status;
                if (search->stop) break;
                status = read_cluster(service, cluster, service->directory);
                if (IOS_FAILED(status)) return status;
                status = scan_loaded_directory(service, &entry_count);
                if (IOS_FAILED(status) || index >= entry_count) {
                    return IOS_FAILED(status) ? status : IOS_ERROR(IOS_E_CORRUPT);
                }
            } else {
                struct ios_fs_primary_disk primary_disk;
                struct ios_fs_companion_disk companion_disk;
                bool matches;
                if (entry.companion_slot == SIZE_MAX
                    || entry.primary_slot >= IOS_FS_FILE_SERVICE_DIRECTORY_SLOTS
                    || entry.companion_slot >= IOS_FS_FILE_SERVICE_DIRECTORY_SLOTS) {
                    return IOS_ERROR(IOS_E_CORRUPT);
                }
                memcpy(
                    &primary_disk,
                    service->directory
                        + entry.primary_slot * IOS_FS_PRIMARY_RECORD_SIZE,
                    sizeof(primary_disk)
                );
                memcpy(
                    &companion_disk,
                    service->directory
                        + entry.companion_slot * IOS_FS_PRIMARY_RECORD_SIZE,
                    sizeof(companion_disk)
                );
                status = ios_fs_record_pair_matches_extension(
                    &primary_disk, &companion_disk,
                    search->extension, search->extension_length,
                    search->hash, &matches
                );
                if (IOS_FAILED(status)) return status;
                if (!matches) continue;
                if (search->count == search->capacity) {
                    search->truncated = true;
                    search->stop = true;
                    break;
                }
                search->entries[search->count] = (struct ios_vfs_search_result){
                    .object_identity = IOS_FS_FILE_OBJECT_BIT
                        | record_location(service, cluster, (ios_u16)entry.primary_slot),
                    .display_path_length = child_length
                };
                memcpy(
                    search->entries[search->count].display_path,
                    child_path, child_length + 1U
                );
                ++search->count;
            }
        }
    }
    return IOS_OK;
}

static ios_status find_in_directory(
    struct ios_fs_file_service *service,
    ios_u32 directory_cluster,
    const ios_u8 name[IOS_FS_NAME_SIZE],
    struct located_entry *located
)
{
    ios_size chain_count;
    ios_status status = directory_chain(service, directory_cluster, &chain_count);
    if (IOS_FAILED(status)) return status;
    for (ios_size chain_index = 0; chain_index < chain_count; ++chain_index) {
        ios_size entry_count;
        status = read_cluster(service, service->chain[chain_index], service->directory);
        if (IOS_FAILED(status)) return status;
        status = scan_loaded_directory(service, &entry_count);
        if (IOS_FAILED(status)) return status;
        for (ios_size index = 0; index < entry_count; ++index) {
            const struct ios_fs_directory_entry *entry =
                &service->directory_entries[index];
            if (entry->kind == IOS_FS_DIRECTORY_ENTRY_INTERNAL
                || memcmp(entry->primary.name, name, IOS_FS_NAME_SIZE) != 0) continue;
            if (entry->primary_slot > UINT16_MAX
                || (entry->companion_slot != SIZE_MAX
                    && entry->companion_slot > UINT16_MAX)) {
                return IOS_ERROR(IOS_E_OVERFLOW);
            }
            if (located != NULL) {
                memset(located, 0, sizeof(*located));
                located->primary = entry->primary;
                located->directory_cluster = service->chain[chain_index];
                located->primary_slot = (ios_u16)entry->primary_slot;
                located->has_companion = entry->companion_slot != SIZE_MAX;
                memcpy(
                    &located->primary_disk,
                    service->directory
                        + entry->primary_slot * IOS_FS_PRIMARY_RECORD_SIZE,
                    sizeof(located->primary_disk)
                );
                if (located->has_companion) {
                    located->companion_slot = (ios_u16)entry->companion_slot;
                    memcpy(
                        &located->companion_disk,
                        service->directory
                            + entry->companion_slot * IOS_FS_PRIMARY_RECORD_SIZE,
                        sizeof(located->companion_disk)
                    );
                }
            }
            return IOS_OK;
        }
    }
    return IOS_ERROR(IOS_E_NOT_FOUND);
}

static ios_status find_display_base_in_directory(
    struct ios_fs_file_service *service,
    ios_u32 directory_cluster,
    const ios_u8 name[IOS_FS_NAME_SIZE],
    const struct located_entry *ignored
)
{
    ios_size chain_count;
    ios_status status = directory_chain(service, directory_cluster, &chain_count);
    if (IOS_FAILED(status)) return status;
    for (ios_size chain_index = 0; chain_index < chain_count; ++chain_index) {
        ios_size entry_count;
        status = read_cluster(service, service->chain[chain_index], service->directory);
        if (IOS_FAILED(status)) return status;
        status = scan_loaded_directory(service, &entry_count);
        if (IOS_FAILED(status)) return status;
        for (ios_size index = 0; index < entry_count; ++index) {
            const struct ios_fs_directory_entry *entry =
                &service->directory_entries[index];
            if (entry->kind == IOS_FS_DIRECTORY_ENTRY_INTERNAL
                || memcmp(
                    entry->primary.name, name,
                    IOS_FS_NAME_SIZE - IOS_FS_EXTENSION_SIZE
                ) != 0) continue;
            if (ignored != NULL
                && ignored->directory_cluster == service->chain[chain_index]
                && ignored->primary_slot == entry->primary_slot) continue;
            return IOS_OK;
        }
    }
    return IOS_ERROR(IOS_E_NOT_FOUND);
}

static ios_status locate_file_object(
    struct ios_fs_file_service *service,
    ios_u64 object_identity,
    struct located_entry *located
)
{
    const ios_u64 location = object_identity & ~IOS_FS_FILE_OBJECT_BIT;
    const ios_u64 sector = location / IOS_FS_SECTOR_SIZE;
    const ios_size byte_offset = (ios_size)(location % IOS_FS_SECTOR_SIZE);
    ios_u64 relative_sector;
    ios_u64 cluster_value;
    ios_size primary_slot;
    ios_size entry_count;
    ios_status status;
    if (service == NULL || located == NULL
        || (object_identity & IOS_FS_FILE_OBJECT_BIT) == 0
        || byte_offset % IOS_FS_PRIMARY_RECORD_SIZE != 0
        || service->mount == NULL
        || sector < service->mount->geometry.data_start_sector
        || sector >= service->mount->geometry.total_sectors) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    relative_sector = sector - service->mount->geometry.data_start_sector;
    cluster_value = UINT64_C(2) + relative_sector / IOS_FS_SECTORS_PER_CLUSTER;
    if (cluster_value > UINT32_MAX
        || !valid_directory_cluster(service, cluster_value)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    primary_slot = (ios_size)(relative_sector % IOS_FS_SECTORS_PER_CLUSTER)
        * DIRECTORY_SLOTS_PER_SECTOR + byte_offset / IOS_FS_PRIMARY_RECORD_SIZE;
    if (primary_slot >= IOS_FS_FILE_SERVICE_DIRECTORY_SLOTS) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    status = read_cluster(service, (ios_u32)cluster_value, service->directory);
    if (IOS_FAILED(status)) return status;
    status = scan_loaded_directory(service, &entry_count);
    if (IOS_FAILED(status)) return status;
    for (ios_size index = 0; index < entry_count; ++index) {
        const struct ios_fs_directory_entry *entry = &service->directory_entries[index];
        if (entry->kind != IOS_FS_DIRECTORY_ENTRY_REGULAR
            || entry->primary_slot != primary_slot) continue;
        if (entry->companion_slot == SIZE_MAX
            || entry->primary_slot > UINT16_MAX
            || entry->companion_slot > UINT16_MAX) {
            return IOS_ERROR(IOS_E_CORRUPT);
        }
        memset(located, 0, sizeof(*located));
        located->primary = entry->primary;
        located->directory_cluster = (ios_u32)cluster_value;
        located->primary_slot = (ios_u16)entry->primary_slot;
        located->companion_slot = (ios_u16)entry->companion_slot;
        located->has_companion = true;
        memcpy(
            &located->primary_disk,
            service->directory + entry->primary_slot * IOS_FS_PRIMARY_RECORD_SIZE,
            sizeof(located->primary_disk)
        );
        memcpy(
            &located->companion_disk,
            service->directory + entry->companion_slot * IOS_FS_PRIMARY_RECORD_SIZE,
            sizeof(located->companion_disk)
        );
        return file_identity(service, located) == object_identity
            ? IOS_OK : IOS_ERROR(IOS_E_CORRUPT);
    }
    return IOS_ERROR(IOS_E_NOT_FOUND);
}

static ios_status resolve_directory(
    struct ios_fs_file_service *service,
    const char *path,
    ios_size length,
    ios_u32 *directory_cluster
)
{
    ios_u32 current = IOS_FS_ROOT_CLUSTER;
    ios_size cursor = 0;
    if (path == NULL || directory_cluster == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    while (cursor < length && path[cursor] == '/') ++cursor;
    while (cursor < length) {
        struct located_entry entry;
        ios_u8 name[IOS_FS_NAME_SIZE];
        const ios_size begin = cursor;
        while (cursor < length && path[cursor] != '/') ++cursor;
        ios_status status = canonical_component(path + begin, cursor - begin, name);
        if (IOS_FAILED(status)) return status;
        status = find_in_directory(service, current, name, &entry);
        if (IOS_FAILED(status)) return status;
        if (entry.primary.attributes != IOS_FS_ATTRIBUTE_DIRECTORY
            || entry.primary.first_cluster < 2) {
            return IOS_ERROR(IOS_E_NOT_FOUND);
        }
        current = entry.primary.first_cluster;
        while (cursor < length && path[cursor] == '/') ++cursor;
    }
    *directory_cluster = current;
    return IOS_OK;
}

static ios_status resolve_parent(
    struct ios_fs_file_service *service,
    const char *path,
    ios_u32 *parent_cluster,
    ios_u8 name[IOS_FS_NAME_SIZE]
)
{
    ios_size length;
    ios_size leaf;
    if (path == NULL || parent_cluster == NULL || name == NULL || *path == '\0') {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    length = strlen(path);
    while (length > 1 && path[length - 1] == '/') --length;
    leaf = length;
    while (leaf != 0 && path[leaf - 1] != '/') --leaf;
    if (leaf == length) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    ios_status status = canonical_component(path + leaf, length - leaf, name);
    if (IOS_FAILED(status)) return status;
    return resolve_directory(service, path, leaf, parent_cluster);
}

static ios_status resolve_path(
    struct ios_fs_file_service *service, const char *path, struct located_entry *located
)
{
    ios_u32 parent;
    ios_u8 name[IOS_FS_NAME_SIZE];
    ios_status status = resolve_parent(service, path, &parent, name);
    return IOS_FAILED(status) ? status : find_in_directory(service, parent, name, located);
}

static void mark_fat_cluster(struct ios_fs_file_service *service, ios_u32 cluster)
{
    const ios_u32 sector = cluster / FAT_ENTRIES_PER_SECTOR;
    for (ios_size index = 0; index < service->dirty_fat_sector_count; ++index) {
        if (service->dirty_fat_sectors[index] == sector) return;
    }
    if (service->dirty_fat_sector_count < IOS_ARRAY_COUNT(service->dirty_fat_sectors)) {
        service->dirty_fat_sectors[service->dirty_fat_sector_count++] = sector;
    }
}

static ios_status persist_fat(struct ios_fs_file_service *service)
{
    const ios_u8 *fat_bytes = (const ios_u8 *)service->fat;
    for (ios_size index = 0; index < service->dirty_fat_sector_count; ++index) {
        const ios_u32 sector = service->dirty_fat_sectors[index];
        ios_status status;
        if (sector >= service->mount->geometry.fat_sectors) {
            return IOS_ERROR(IOS_E_CORRUPT);
        }
        status = ios_fs_sync_write_sector(
            service->sync, IOS_FS_SUPERBLOCK_SECTORS + sector,
            fat_bytes + (ios_size)sector * IOS_FS_SECTOR_SIZE,
            IOS_FS_DIRTY_ALLOCATION
        );
        if (IOS_FAILED(status)) return status;
    }
    service->dirty_fat_sector_count = 0;
    return IOS_OK;
}

static ios_status persist_directory_slot(
    struct ios_fs_file_service *service,
    ios_u32 directory_cluster,
    ios_u16 slot,
    const void *record,
    enum ios_fs_dirty_component component
)
{
    const ios_size offset = (ios_size)slot * IOS_FS_PRIMARY_RECORD_SIZE;
    const ios_size sector = offset / IOS_FS_SECTOR_SIZE;
    ios_status status;
    if (record == NULL || slot >= IOS_FS_FILE_SERVICE_DIRECTORY_SLOTS) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    status = read_cluster(service, directory_cluster, service->directory);
    if (IOS_FAILED(status)) return status;
    memcpy(service->directory + offset, record, IOS_FS_PRIMARY_RECORD_SIZE);
    return ios_fs_sync_write_sector(
        service->sync,
        cluster_sector(service, directory_cluster) + sector,
        service->directory + sector * IOS_FS_SECTOR_SIZE,
        component
    );
}

static ios_status persist_companion(
    struct ios_fs_file_service *service,
    const struct located_entry *located,
    const ios_u8 name[IOS_FS_NAME_SIZE],
    bool committed
)
{
    struct ios_fs_companion_disk companion;
    ios_status status = ios_fs_companion_encode(name, committed, &companion);
    return IOS_FAILED(status) ? status : persist_directory_slot(
        service, located->directory_cluster, located->companion_slot,
        &companion, IOS_FS_DIRTY_COMPANION
    );
}

static ios_status barrier(struct ios_fs_file_service *service)
{
    ios_status status = ios_fs_sync_barrier(service->sync);
    if (IOS_FAILED(status)) service->initialized = false;
    return status;
}

static ios_status collect_chain(
    struct ios_fs_file_service *service,
    const struct ios_fs_primary *primary,
    ios_u32 *workspace,
    ios_size *count
)
{
    if (primary->file_size == 0) {
        *count = 0;
        return primary->first_cluster == 0 ? IOS_OK : IOS_ERROR(IOS_E_CORRUPT);
    }
    return ios_fs_fat_traverse(
        service->fat, service->fat_entry_count, primary->first_cluster,
        workspace, IOS_FS_FILE_SERVICE_MAX_CLUSTERS, count
    );
}

static ios_status allocate_chain(
    struct ios_fs_file_service *service, ios_size count
)
{
    ios_size allocated;
    ios_status status;
    if (count == 0) return IOS_OK;
    status = ios_fs_fat_allocate(
        service->fat, service->fat_entry_count, count,
        service->chain, IOS_ARRAY_COUNT(service->chain), &allocated
    );
    if (IOS_FAILED(status)) return status;
    for (ios_size index = 0; index < allocated; ++index) {
        mark_fat_cluster(service, service->chain[index]);
    }
    return IOS_OK;
}

static ios_status release_chain(
    struct ios_fs_file_service *service, ios_u32 first_cluster
)
{
    ios_size count;
    ios_status status;
    if (first_cluster == 0) return IOS_OK;
    status = ios_fs_fat_traverse(
        service->fat, service->fat_entry_count, first_cluster,
        service->old_chain, IOS_ARRAY_COUNT(service->old_chain), &count
    );
    if (IOS_FAILED(status)) return status;
    status = ios_fs_fat_free(
        service->fat, service->fat_entry_count, first_cluster,
        service->old_chain, IOS_ARRAY_COUNT(service->old_chain)
    );
    if (IOS_FAILED(status)) return status;
    for (ios_size index = 0; index < count; ++index) {
        mark_fat_cluster(service, service->old_chain[index]);
    }
    status = persist_fat(service);
    return IOS_FAILED(status) ? status : barrier(service);
}

static ios_status commit_primary(
    struct ios_fs_file_service *service,
    const struct located_entry *located,
    const struct ios_fs_primary *primary
)
{
    struct ios_fs_primary_disk disk;
    ios_status status = persist_companion(service, located, primary->name, false);
    if (IOS_FAILED(status)) return status;
    status = barrier(service);
    if (IOS_FAILED(status)) return status;
    status = persist_fat(service);
    if (IOS_FAILED(status)) return status;
    status = barrier(service);
    if (IOS_FAILED(status)) return status;
    status = ios_fs_primary_encode(primary, &disk);
    if (IOS_FAILED(status)) return status;
    status = persist_directory_slot(
        service, located->directory_cluster, located->primary_slot,
        &disk, IOS_FS_DIRTY_PRIMARY
    );
    if (IOS_FAILED(status)) return status;
    status = persist_companion(service, located, primary->name, true);
    return IOS_FAILED(status) ? status : barrier(service);
}

static ios_status find_file_slots(
    struct ios_fs_file_service *service,
    ios_u32 directory_cluster,
    struct located_entry *located
)
{
    ios_size chain_count;
    ios_status status = directory_chain(service, directory_cluster, &chain_count);
    if (IOS_FAILED(status)) return status;
    for (ios_size index = 0; index < chain_count; ++index) {
        ios_size companion_slot;
        status = read_cluster(service, service->chain[index], service->directory);
        if (IOS_FAILED(status)) return status;
        status = ios_fs_directory_find_pair_slots(
            service->directory,
            IOS_FS_FILE_SERVICE_DIRECTORY_SLOTS,
            IOS_FS_FILE_SERVICE_DIRECTORY_SLOTS,
            &companion_slot
        );
        if (IOS_SUCCEEDED(status)) {
            located->directory_cluster = service->chain[index];
            located->companion_slot = (ios_u16)companion_slot;
            located->primary_slot = (ios_u16)(companion_slot + 1U);
            located->has_companion = true;
            return IOS_OK;
        }
        if (status != IOS_ERROR(IOS_E_NO_SPACE)) return status;
    }
    return IOS_ERROR(IOS_E_NO_SPACE);
}

static ios_status find_directory_slot(
    struct ios_fs_file_service *service,
    ios_u32 directory_cluster,
    struct located_entry *located
)
{
    ios_size chain_count;
    ios_status status = directory_chain(service, directory_cluster, &chain_count);
    if (IOS_FAILED(status)) return status;
    for (ios_size chain_index = 0; chain_index < chain_count; ++chain_index) {
        status = read_cluster(service, service->chain[chain_index], service->directory);
        if (IOS_FAILED(status)) return status;
        for (ios_size slot = 0; slot < IOS_FS_FILE_SERVICE_DIRECTORY_SLOTS; ++slot) {
            const ios_u8 first = service->directory[slot * IOS_FS_PRIMARY_RECORD_SIZE];
            if (first == 0 || first == UINT8_C(0xe5)) {
                located->directory_cluster = service->chain[chain_index];
                located->primary_slot = (ios_u16)slot;
                located->has_companion = false;
                return IOS_OK;
            }
        }
    }
    return IOS_ERROR(IOS_E_NO_SPACE);
}

ios_status ios_fs_file_service_initialize(
    struct ios_fs_file_service *service,
    struct ios_fs_mount *mount,
    struct ios_fs_sync *sync,
    ios_u32 *fat_storage,
    ios_size fat_storage_size
)
{
    ios_size required_fat_bytes;
    ios_status status;
    if (service == NULL || mount == NULL || sync == NULL || sync->cache == NULL
        || fat_storage == NULL || mount->geometry.fat_sectors == 0
        || mount->geometry.cluster_count == 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    required_fat_bytes = (ios_size)mount->geometry.fat_sectors * IOS_FS_SECTOR_SIZE;
    if (fat_storage_size < required_fat_bytes
        || (ios_size)(mount->geometry.cluster_count + 2U) * sizeof(ios_u32)
            > required_fat_bytes) {
        return IOS_ERROR(IOS_E_NO_SPACE);
    }
    memset(service, 0, sizeof(*service));
    service->mount = mount;
    service->sync = sync;
    service->fat = fat_storage;
    service->fat_entry_count = (ios_size)mount->geometry.cluster_count + 2U;
    service->fat_storage_size = fat_storage_size;
    status = load_fat(service);
    if (IOS_FAILED(status)) return status;
    if (service->fat[0] < IOS_FS_FAT_EOC_FIRST
        || service->fat[1] < IOS_FS_FAT_EOC_FIRST
        || service->fat[IOS_FS_ROOT_CLUSTER] < IOS_FS_FAT_EOC_FIRST) {
        return IOS_ERROR(IOS_E_CORRUPT);
    }
    mount->vfs.driver_context = service;
    mount->vfs.enumerate = ios_fs_file_service_enumerate;
    mount->vfs.lookup = ios_fs_file_service_lookup;
    mount->vfs.create_directory = ios_fs_file_service_create_directory;
    mount->vfs.create_file = ios_fs_file_service_create_file;
    mount->vfs.remove_directory = ios_fs_file_service_remove_directory;
    mount->vfs.rename = ios_fs_file_service_vfs_rename;
    mount->vfs.search_extension = ios_fs_file_service_search_extension;
    mount->vfs.read_object = ios_fs_file_service_read_object;
    mount->vfs.replace_object = ios_fs_file_service_replace_object;
    mount->vfs.append_object = ios_fs_file_service_append_object;
    mount->vfs.remove_object = ios_fs_file_service_remove_object;
    mount->vfs.rename_object = ios_fs_file_service_rename_object;
    service->initialized = true;
    return IOS_OK;
}

ios_status ios_fs_file_service_search_extension(
    void *context,
    const char *extension,
    ios_size extension_length,
    struct ios_vfs_search_result *entries,
    ios_size capacity,
    ios_size *entry_count,
    bool *truncated
)
{
    struct ios_fs_file_service *service = context;
    struct extension_search_state search = {
        .entries = entries,
        .capacity = capacity
    };
    char root_path[IOS_VFS_PATH_CAPACITY] = "/";
    ios_status status;
    if (entries == NULL || capacity == 0
        || capacity > IOS_VFS_SEARCH_RESULT_CAPACITY
        || entry_count == NULL || truncated == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *entry_count = 0;
    *truncated = false;
    status = validate_service(service, false);
    if (IOS_FAILED(status)) return status;
    status = ios_fs_extension_query_canonicalize(
        extension, extension_length, search.extension, &search.extension_length
    );
    if (IOS_FAILED(status)) return status;
    ios_fs_hash_text(
        ios_fs_fnv1a32(search.extension, search.extension_length), search.hash
    );
    memset(entries, 0, capacity * sizeof(*entries));
    status = search_directory(
        service, IOS_FS_ROOT_CLUSTER, 0, root_path, 1, &search
    );
    if (IOS_FAILED(status)) {
        memset(entries, 0, capacity * sizeof(*entries));
        return status;
    }
    *entry_count = search.count;
    *truncated = search.truncated;
    return IOS_OK;
}

static ios_status create_file_in_directory(
    struct ios_fs_file_service *service,
    ios_u32 parent,
    const ios_u8 name[IOS_FS_NAME_SIZE],
    struct ios_vfs_object *object
)
{
    struct ios_fs_primary primary = { .attributes = IOS_FS_ATTRIBUTE_REGULAR };
    struct located_entry located;
    ios_status status = find_display_base_in_directory(service, parent, name, NULL);
    if (IOS_SUCCEEDED(status)) return IOS_ERROR(IOS_E_ALREADY_EXISTS);
    if (status != IOS_ERROR(IOS_E_NOT_FOUND)) return status;
    memset(&located, 0, sizeof(located));
    status = find_file_slots(service, parent, &located);
    if (IOS_FAILED(status)) return status;
    memcpy(primary.name, name, sizeof(primary.name));
    status = commit_primary(service, &located, &primary);
    if (IOS_FAILED(status)) return status;
    if (object != NULL) {
        *object = (struct ios_vfs_object){
            file_identity(service, &located), IOS_VFS_OBJECT_REGULAR_FILE, 0
        };
    }
    return IOS_OK;
}

ios_status ios_fs_file_service_create(
    struct ios_fs_file_service *service, const char *path
)
{
    ios_u32 parent;
    ios_u8 name[IOS_FS_NAME_SIZE];
    ios_status status = validate_service(service, true);
    if (IOS_FAILED(status)) return status;
    status = resolve_parent(service, path, &parent, name);
    return IOS_FAILED(status) ? status
        : create_file_in_directory(service, parent, name, NULL);
}

ios_status ios_fs_file_service_create_file(
    void *context,
    ios_u64 parent_identity,
    const char *component,
    ios_size component_length,
    struct ios_vfs_object *object
)
{
    struct ios_fs_file_service *service = context;
    ios_u8 name[IOS_FS_NAME_SIZE];
    ios_status status;
    if (object == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    *object = (struct ios_vfs_object){ 0, IOS_VFS_OBJECT_REGULAR_FILE, 0 };
    status = validate_service(service, true);
    if (IOS_FAILED(status)) return status;
    if (!valid_directory_cluster(service, parent_identity)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    status = canonical_component(component, component_length, name);
    return IOS_FAILED(status) ? status
        : create_file_in_directory(
            service, (ios_u32)parent_identity, name, object
        );
}

static ios_status save_content(
    struct ios_fs_file_service *service,
    const char *path,
    ios_u64 object_identity,
    const void *bytes,
    ios_size length,
    bool append
)
{
    struct located_entry located;
    ios_size old_count;
    ios_size new_count;
    ios_u64 new_size;
    ios_status status = validate_service(service, true);
    if (IOS_FAILED(status)) return status;
    if (bytes == NULL && length != 0) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    status = path != NULL
        ? resolve_path(service, path, &located)
        : locate_file_object(service, object_identity, &located);
    if (IOS_FAILED(status)) return status;
    if (!located.has_companion
        || located.primary.attributes != IOS_FS_ATTRIBUTE_REGULAR) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    status = ios_fs_record_pair_validate(
        &located.companion_disk, &located.primary_disk
    );
    if (IOS_FAILED(status)) return status;
    status = collect_chain(service, &located.primary, service->old_chain, &old_count);
    if (IOS_FAILED(status)) return status;
    new_size = append ? (ios_u64)located.primary.file_size + length : length;
    if (new_size > UINT32_MAX
        || new_size > (ios_u64)IOS_FS_FILE_SERVICE_MAX_CLUSTERS
            * IOS_FS_FILE_SERVICE_CLUSTER_BYTES) {
        return IOS_ERROR(IOS_E_NO_SPACE);
    }
    new_count = ((ios_size)new_size + IOS_FS_FILE_SERVICE_CLUSTER_BYTES - 1U)
        / IOS_FS_FILE_SERVICE_CLUSTER_BYTES;
    service->dirty_fat_sector_count = 0;
    status = allocate_chain(service, new_count);
    if (IOS_FAILED(status)) return status;
    for (ios_size index = 0; index < new_count; ++index) {
        const ios_u64 start = (ios_u64)index * IOS_FS_FILE_SERVICE_CLUSTER_BYTES;
        const ios_u64 input_start = append ? located.primary.file_size : 0;
        const ios_u64 input_end = input_start + length;
        memset(service->cluster, 0, sizeof(service->cluster));
        if (append && start < located.primary.file_size && index < old_count) {
            status = read_cluster(service, service->old_chain[index], service->cluster);
            if (IOS_FAILED(status)) goto rollback;
        }
        if (input_start < start + sizeof(service->cluster) && input_end > start) {
            const ios_u64 copy_start = input_start > start ? input_start : start;
            const ios_u64 copy_end = input_end < start + sizeof(service->cluster)
                ? input_end : start + sizeof(service->cluster);
            memcpy(
                service->cluster + (ios_size)(copy_start - start),
                (const ios_u8 *)bytes + (ios_size)(copy_start - input_start),
                (ios_size)(copy_end - copy_start)
            );
        }
        status = write_cluster(
            service, service->chain[index], service->cluster, IOS_FS_DIRTY_CONTENT
        );
        if (IOS_FAILED(status)) goto rollback;
        status = barrier(service);
        if (IOS_FAILED(status)) return status;
    }
    located.primary.first_cluster = new_count == 0 ? 0 : *service->chain;
    located.primary.file_size = (ios_u32)new_size;
    status = commit_primary(service, &located, &located.primary);
    if (IOS_FAILED(status)) return status;
    return release_chain(service, old_count == 0 ? 0 : *service->old_chain);

rollback:
    (void)load_fat(service);
    return status;
}

ios_status ios_fs_file_service_replace(
    struct ios_fs_file_service *service,
    const char *name,
    const void *bytes,
    ios_size length
)
{
    return save_content(service, name, 0, bytes, length, false);
}

ios_status ios_fs_file_service_append(
    struct ios_fs_file_service *service,
    const char *name,
    const void *bytes,
    ios_size length
)
{
    return save_content(service, name, 0, bytes, length, true);
}

ios_status ios_fs_file_service_replace_object(
    void *context,
    ios_u64 object_identity,
    const void *bytes,
    ios_size length
)
{
    return save_content(context, NULL, object_identity, bytes, length, false);
}

ios_status ios_fs_file_service_append_object(
    void *context,
    ios_u64 object_identity,
    const void *bytes,
    ios_size length
)
{
    return save_content(context, NULL, object_identity, bytes, length, true);
}

static ios_status read_content(
    struct ios_fs_file_service *service,
    const char *path,
    ios_u64 object_identity,
    ios_u64 offset,
    void *buffer,
    ios_size capacity,
    ios_size *transferred,
    bool *complete
)
{
    struct located_entry located;
    ios_size chain_count;
    ios_size remaining;
    ios_status status = validate_service(service, false);
    if (transferred == NULL || complete == NULL || (buffer == NULL && capacity != 0)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *transferred = 0;
    *complete = false;
    if (IOS_FAILED(status)) return status;
    status = path != NULL
        ? resolve_path(service, path, &located)
        : locate_file_object(service, object_identity, &located);
    if (IOS_FAILED(status)) return status;
    if (!located.has_companion
        || located.primary.attributes != IOS_FS_ATTRIBUTE_REGULAR) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    status = ios_fs_record_pair_validate(
        &located.companion_disk, &located.primary_disk
    );
    if (IOS_FAILED(status)) return status;
    if (offset > located.primary.file_size) return IOS_ERROR(IOS_E_OUT_OF_RANGE);
    status = collect_chain(service, &located.primary, service->chain, &chain_count);
    if (IOS_FAILED(status)) return status;
    remaining = located.primary.file_size - offset;
    if (remaining > capacity) remaining = capacity;
    while (*transferred < remaining) {
        const ios_u64 position = offset + *transferred;
        const ios_size chain_index = position / IOS_FS_FILE_SERVICE_CLUSTER_BYTES;
        const ios_size cluster_offset = position % IOS_FS_FILE_SERVICE_CLUSTER_BYTES;
        ios_size chunk = IOS_FS_FILE_SERVICE_CLUSTER_BYTES - cluster_offset;
        if (chain_index >= chain_count) return IOS_ERROR(IOS_E_CORRUPT);
        if (chunk > remaining - *transferred) chunk = remaining - *transferred;
        status = read_cluster(service, service->chain[chain_index], service->cluster);
        if (IOS_FAILED(status)) return status;
        memcpy((ios_u8 *)buffer + *transferred, service->cluster + cluster_offset, chunk);
        *transferred += chunk;
    }
    *complete = offset + *transferred == located.primary.file_size;
    return IOS_OK;
}

ios_status ios_fs_file_service_read(
    struct ios_fs_file_service *service,
    const char *path,
    ios_u32 offset,
    void *buffer,
    ios_size capacity,
    ios_size *transferred,
    bool *complete
)
{
    if (path == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    return read_content(
        service, path, 0, offset, buffer, capacity, transferred, complete
    );
}

ios_status ios_fs_file_service_read_object(
    void *context,
    ios_u64 object_identity,
    ios_u64 offset,
    void *buffer,
    ios_size capacity,
    ios_size *transferred,
    bool *complete
)
{
    if (object_identity == 0) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    return read_content(
        context, NULL, object_identity, offset, buffer, capacity, transferred, complete
    );
}

static ios_status delete_located(
    struct ios_fs_file_service *service, const struct located_entry *located
)
{
    ios_u8 deleted[IOS_FS_PRIMARY_RECORD_SIZE] = { 0 };
    ios_status status;
    *deleted = UINT8_C(0xe5);
    if (located->has_companion) {
        status = persist_companion(service, located, located->primary.name, false);
        if (IOS_FAILED(status)) return status;
        status = barrier(service);
        if (IOS_FAILED(status)) return status;
        status = persist_directory_slot(
            service, located->directory_cluster, located->companion_slot,
            deleted, IOS_FS_DIRTY_COMPANION
        );
        if (IOS_FAILED(status)) return status;
    }
    status = persist_directory_slot(
        service, located->directory_cluster, located->primary_slot,
        deleted, IOS_FS_DIRTY_PRIMARY
    );
    return IOS_FAILED(status) ? status : barrier(service);
}

static ios_status rename_located(
    struct ios_fs_file_service *service,
    struct located_entry *source_entry,
    ios_u32 destination_parent,
    const ios_u8 destination_name[IOS_FS_NAME_SIZE]
)
{
    struct located_entry destination_entry;
    ios_status status;
    if (!source_entry->has_companion
        || source_entry->primary.attributes != IOS_FS_ATTRIBUTE_REGULAR) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    status = ios_fs_record_pair_validate(
        &source_entry->companion_disk, &source_entry->primary_disk
    );
    if (IOS_FAILED(status)) return status;
    status = find_in_directory(service, destination_parent, destination_name, NULL);
    if (IOS_SUCCEEDED(status)) {
        return source_entry->directory_cluster == destination_parent
            && memcmp(source_entry->primary.name, destination_name, IOS_FS_NAME_SIZE) == 0
            ? IOS_OK : IOS_ERROR(IOS_E_ALREADY_EXISTS);
    }
    if (status != IOS_ERROR(IOS_E_NOT_FOUND)) return status;
    status = find_display_base_in_directory(
        service, destination_parent, destination_name,
        source_entry->directory_cluster == destination_parent ? source_entry : NULL
    );
    if (IOS_SUCCEEDED(status)) return IOS_ERROR(IOS_E_ALREADY_EXISTS);
    if (status != IOS_ERROR(IOS_E_NOT_FOUND)) return status;
    if (source_entry->directory_cluster == destination_parent) {
        status = persist_companion(
            service, source_entry, source_entry->primary.name, false
        );
        if (IOS_FAILED(status)) return status;
        status = barrier(service);
        if (IOS_FAILED(status)) return status;
        memcpy(source_entry->primary.name, destination_name, IOS_FS_NAME_SIZE);
        return commit_primary(service, source_entry, &source_entry->primary);
    }
    memset(&destination_entry, 0, sizeof(destination_entry));
    status = find_file_slots(service, destination_parent, &destination_entry);
    if (IOS_FAILED(status)) return status;
    memcpy(source_entry->primary.name, destination_name, IOS_FS_NAME_SIZE);
    status = commit_primary(service, &destination_entry, &source_entry->primary);
    if (IOS_FAILED(status)) return status;
    return delete_located(service, source_entry);
}

ios_status ios_fs_file_service_rename(
    struct ios_fs_file_service *service,
    const char *source,
    const char *destination
)
{
    struct located_entry source_entry;
    ios_u32 destination_parent;
    ios_u8 destination_name[IOS_FS_NAME_SIZE];
    ios_status status = validate_service(service, true);
    if (IOS_FAILED(status)) return status;
    status = resolve_path(service, source, &source_entry);
    if (IOS_FAILED(status)) return status;
    status = resolve_parent(service, destination, &destination_parent, destination_name);
    if (IOS_FAILED(status)) return status;
    return rename_located(
        service, &source_entry, destination_parent, destination_name
    );
}

ios_status ios_fs_file_service_rename_object(
    void *context,
    ios_u64 object_identity,
    ios_u64 destination_parent_identity,
    const char *destination_base,
    ios_size destination_base_length
)
{
    struct ios_fs_file_service *service = context;
    struct located_entry source_entry;
    ios_u8 destination_name[IOS_FS_NAME_SIZE];
    ios_status status = validate_service(service, true);
    if (IOS_FAILED(status)) return status;
    if (!valid_directory_cluster(service, destination_parent_identity)
        || destination_base == NULL || destination_base_length == 0
        || destination_base_length > 8) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    for (ios_size index = 0; index < destination_base_length; ++index) {
        if (destination_base[index] == '.' || destination_base[index] == '/'
            || destination_base[index] == '\\') {
            return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
        }
    }
    status = locate_file_object(service, object_identity, &source_entry);
    if (IOS_FAILED(status)) return status;
    status = canonical_component(
        destination_base, destination_base_length, destination_name
    );
    if (IOS_FAILED(status)) return status;
    for (ios_size index = 8; index < IOS_FS_NAME_SIZE; ++index) {
        if (destination_name[index] != ' ') return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
        destination_name[index] = source_entry.primary.name[index];
    }
    return rename_located(
        service, &source_entry, (ios_u32)destination_parent_identity,
        destination_name
    );
}

static ios_status remove_located_file(
    struct ios_fs_file_service *service,
    struct located_entry *located
)
{
    ios_status status;
    if (!located->has_companion
        || located->primary.attributes != IOS_FS_ATTRIBUTE_REGULAR) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    status = ios_fs_record_pair_validate(
        &located->companion_disk, &located->primary_disk
    );
    if (IOS_FAILED(status)) return status;
    status = delete_located(service, located);
    return IOS_FAILED(status) ? status
        : release_chain(service, located->primary.first_cluster);
}

ios_status ios_fs_file_service_remove(
    struct ios_fs_file_service *service, const char *path
)
{
    struct located_entry located;
    ios_status status = validate_service(service, true);
    if (IOS_FAILED(status)) return status;
    status = resolve_path(service, path, &located);
    if (IOS_FAILED(status)) return status;
    return remove_located_file(service, &located);
}

ios_status ios_fs_file_service_remove_object(
    void *context,
    ios_u64 object_identity
)
{
    struct ios_fs_file_service *service = context;
    struct located_entry located;
    ios_status status = validate_service(service, true);
    if (IOS_FAILED(status)) return status;
    status = locate_file_object(service, object_identity, &located);
    return IOS_FAILED(status) ? status : remove_located_file(service, &located);
}

static void display_base_name(
    const ios_u8 canonical[IOS_FS_NAME_SIZE], char output[IOS_VFS_DISPLAY_NAME_CAPACITY]
)
{
    ios_size length = 8;
    while (length != 0 && canonical[length - 1] == ' ') --length;
    memcpy(output, canonical, length);
    output[length] = '\0';
}

static ios_status file_type_metadata(
    const ios_u8 canonical[IOS_FS_NAME_SIZE],
    ios_u64 *type_identity,
    ios_u64 *type_prefilter
)
{
    ios_u8 extension[IOS_FS_EXTENSION_SIZE];
    ios_size length;
    ios_u64 identity = 0;
    ios_status status;

    if (type_identity == NULL || type_prefilter == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    status = ios_fs_name_extension(canonical, extension, &length);
    if (IOS_FAILED(status)) return status;
    for (ios_size index = 0; index < length; ++index) {
        identity = (identity << 8) | extension[index];
    }
    /* Zero is reserved by VFS. Keep extensionless files as one private type. */
    *type_identity = length == 0 ? UINT64_C(1) : identity;
    *type_prefilter = ios_fs_fnv1a32(extension, length);
    return IOS_OK;
}

ios_status ios_fs_file_service_enumerate(
    void *context,
    ios_u64 directory_identity,
    ios_u64 continuation,
    struct ios_vfs_directory_entry *entries,
    ios_size capacity,
    ios_size *entry_count,
    ios_u64 *next_continuation
)
{
    struct ios_fs_file_service *service = context;
    ios_size chain_count;
    ios_u64 ordinal = 0;
    ios_status status = validate_service(service, false);
    if (entries == NULL || capacity == 0 || entry_count == NULL
        || next_continuation == NULL || !valid_directory_cluster(service, directory_identity)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *entry_count = 0;
    *next_continuation = 0;
    if (IOS_FAILED(status)) return status;
    status = directory_chain(service, (ios_u32)directory_identity, &chain_count);
    if (IOS_FAILED(status)) return status;
    for (ios_size chain_index = 0; chain_index < chain_count; ++chain_index) {
        ios_size count;
        status = read_cluster(service, service->chain[chain_index], service->directory);
        if (IOS_FAILED(status)) return status;
        status = scan_loaded_directory(service, &count);
        if (IOS_FAILED(status)) return status;
        for (ios_size index = 0; index < count; ++index) {
            const struct ios_fs_directory_entry *source =
                &service->directory_entries[index];
            struct located_entry located;
            struct ios_vfs_directory_entry *destination;
            if (source->kind == IOS_FS_DIRECTORY_ENTRY_INTERNAL) continue;
            if (ordinal++ < continuation) continue;
            if (*entry_count == capacity) {
                *next_continuation = ordinal - 1U;
                return IOS_OK;
            }
            memset(&located, 0, sizeof(located));
            located.directory_cluster = service->chain[chain_index];
            located.primary_slot = (ios_u16)source->primary_slot;
            destination = &entries[(*entry_count)++];
            memset(destination, 0, sizeof(*destination));
            display_base_name(source->primary.name, destination->display_base_name);
            destination->byte_size = source->primary.file_size;
            destination->generic_attributes = service->mount->vfs.state == IOS_MOUNT_RW
                ? 0 : IOS_VFS_ATTRIBUTE_READ_ONLY;
            if (source->kind == IOS_FS_DIRECTORY_ENTRY_DIRECTORY) {
                destination->object_identity = source->primary.first_cluster;
                destination->kind = IOS_VFS_OBJECT_DIRECTORY;
                destination->allowed_operations = IOS_VFS_FILE_OPEN
                    | IOS_VFS_FILE_READ | IOS_VFS_FILE_ENUMERATE;
            } else {
                status = file_type_metadata(
                    source->primary.name,
                    &destination->internal_type_identity,
                    &destination->type_prefilter
                );
                if (IOS_FAILED(status)) {
                    *entry_count = 0;
                    *next_continuation = 0;
                    return status;
                }
                destination->object_identity = file_identity(service, &located);
                destination->kind = IOS_VFS_OBJECT_REGULAR_FILE;
                destination->allowed_operations = IOS_VFS_FILE_OPEN | IOS_VFS_FILE_READ;
            }
            if (service->mount->vfs.state == IOS_MOUNT_RW) {
                destination->allowed_operations |= IOS_VFS_FILE_WRITE
                    | IOS_VFS_FILE_RENAME | IOS_VFS_FILE_DELETE;
            }
        }
    }
    return IOS_OK;
}

ios_status ios_fs_file_service_lookup(
    void *context,
    ios_u64 directory_identity,
    const char *component,
    ios_size component_length,
    struct ios_vfs_object *object
)
{
    struct ios_fs_file_service *service = context;
    struct located_entry located;
    ios_u8 name[IOS_FS_NAME_SIZE];
    ios_status status = validate_service(service, false);
    if (object == NULL || !valid_directory_cluster(service, directory_identity)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (IOS_FAILED(status)) return status;
    status = canonical_component(component, component_length, name);
    if (IOS_FAILED(status)) return status;
    status = find_in_directory(service, (ios_u32)directory_identity, name, &located);
    if (IOS_FAILED(status)) return status;
    object->reference_count = 0;
    if (located.primary.attributes == IOS_FS_ATTRIBUTE_DIRECTORY) {
        object->identity = located.primary.first_cluster;
        object->kind = IOS_VFS_OBJECT_DIRECTORY;
    } else {
        object->identity = file_identity(service, &located);
        object->kind = IOS_VFS_OBJECT_REGULAR_FILE;
    }
    return IOS_OK;
}

static void write_cluster_number(ios_u8 low[2], ios_u8 high[2], ios_u32 cluster)
{
    low[0] = (ios_u8)cluster;
    low[1] = (ios_u8)(cluster >> 8);
    high[0] = (ios_u8)(cluster >> 16);
    high[1] = (ios_u8)(cluster >> 24);
}

static void make_internal_record(
    struct ios_fs_primary_disk *disk, bool parent, ios_u32 cluster
)
{
    memset(disk, 0, sizeof(*disk));
    memset(disk->name, ' ', sizeof(disk->name));
    disk->name[0] = '.';
    if (parent) disk->name[1] = '.';
    disk->attributes = IOS_FS_ATTRIBUTE_DIRECTORY;
    write_cluster_number(disk->first_cluster_low, disk->first_cluster_high, cluster);
}

ios_status ios_fs_file_service_create_directory(
    void *context,
    ios_u64 parent_identity,
    const char *component,
    ios_size component_length,
    struct ios_vfs_object *object
)
{
    struct ios_fs_file_service *service = context;
    struct located_entry located;
    struct ios_fs_primary primary = { .attributes = IOS_FS_ATTRIBUTE_DIRECTORY };
    struct ios_fs_primary_disk primary_disk;
    struct ios_fs_primary_disk current;
    struct ios_fs_primary_disk parent;
    ios_u8 name[IOS_FS_NAME_SIZE];
    ios_status status = validate_service(service, true);
    if (object == NULL || !valid_directory_cluster(service, parent_identity)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (IOS_FAILED(status)) return status;
    status = canonical_component(component, component_length, name);
    if (IOS_FAILED(status)) return status;
    status = find_display_base_in_directory(
        service, (ios_u32)parent_identity, name, NULL
    );
    if (IOS_SUCCEEDED(status)) return IOS_ERROR(IOS_E_ALREADY_EXISTS);
    if (status != IOS_ERROR(IOS_E_NOT_FOUND)) return status;
    memset(&located, 0, sizeof(located));
    status = find_directory_slot(service, (ios_u32)parent_identity, &located);
    if (IOS_FAILED(status)) return status;
    service->dirty_fat_sector_count = 0;
    status = allocate_chain(service, 1);
    if (IOS_FAILED(status)) return status;
    memset(service->cluster, 0, sizeof(service->cluster));
    make_internal_record(&current, false, service->chain[0]);
    make_internal_record(&parent, true, (ios_u32)parent_identity);
    memcpy(service->cluster, &current, sizeof(current));
    memcpy(service->cluster + IOS_FS_PRIMARY_RECORD_SIZE, &parent, sizeof(parent));
    status = write_cluster(
        service, service->chain[0], service->cluster, IOS_FS_DIRTY_CONTENT
    );
    if (IOS_FAILED(status)) goto rollback;
    status = barrier(service);
    if (IOS_FAILED(status)) return status;
    status = persist_fat(service);
    if (IOS_FAILED(status)) return status;
    status = barrier(service);
    if (IOS_FAILED(status)) return status;
    memcpy(primary.name, name, sizeof(primary.name));
    primary.first_cluster = *service->chain;
    status = ios_fs_primary_encode(&primary, &primary_disk);
    if (IOS_FAILED(status)) return status;
    status = persist_directory_slot(
        service, located.directory_cluster, located.primary_slot,
        &primary_disk, IOS_FS_DIRTY_PRIMARY
    );
    if (IOS_FAILED(status)) return status;
    status = barrier(service);
    if (IOS_FAILED(status)) return status;
    *object = (struct ios_vfs_object){
        primary.first_cluster, IOS_VFS_OBJECT_DIRECTORY, 0
    };
    return IOS_OK;

rollback:
    (void)load_fat(service);
    return status;
}

static ios_status directory_is_empty(
    struct ios_fs_file_service *service, ios_u32 directory_cluster, bool *empty
)
{
    ios_size chain_count;
    ios_status status;
    *empty = true;
    status = directory_chain(service, directory_cluster, &chain_count);
    if (IOS_FAILED(status)) return status;
    for (ios_size chain_index = 0; chain_index < chain_count; ++chain_index) {
        ios_size count;
        status = read_cluster(service, service->chain[chain_index], service->directory);
        if (IOS_FAILED(status)) return status;
        status = scan_loaded_directory(service, &count);
        if (IOS_FAILED(status)) return status;
        for (ios_size index = 0; index < count; ++index) {
            if (service->directory_entries[index].kind != IOS_FS_DIRECTORY_ENTRY_INTERNAL) {
                *empty = false;
                return IOS_OK;
            }
        }
    }
    return IOS_OK;
}

ios_status ios_fs_file_service_remove_directory(
    void *context,
    ios_u64 parent_identity,
    const char *component,
    ios_size component_length
)
{
    struct ios_fs_file_service *service = context;
    struct located_entry located;
    ios_u8 name[IOS_FS_NAME_SIZE];
    bool empty;
    ios_status status = validate_service(service, true);
    if (!valid_directory_cluster(service, parent_identity)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (IOS_FAILED(status)) return status;
    status = canonical_component(component, component_length, name);
    if (IOS_FAILED(status)) return status;
    status = find_in_directory(service, (ios_u32)parent_identity, name, &located);
    if (IOS_FAILED(status)) return status;
    if (located.primary.attributes != IOS_FS_ATTRIBUTE_DIRECTORY) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    status = directory_is_empty(service, located.primary.first_cluster, &empty);
    if (IOS_FAILED(status)) return status;
    if (!empty) return IOS_ERROR(IOS_E_NOT_EMPTY);
    status = delete_located(service, &located);
    return IOS_FAILED(status) ? status : release_chain(service, located.primary.first_cluster);
}

ios_status ios_fs_file_service_vfs_rename(
    void *context,
    ios_u64 source_parent_identity,
    const char *source_component,
    ios_size source_component_length,
    ios_u64 destination_parent_identity,
    const char *destination_component,
    ios_size destination_component_length
)
{
    struct ios_fs_file_service *service = context;
    struct located_entry source;
    struct located_entry destination;
    struct ios_fs_primary_disk disk;
    ios_u8 source_name[IOS_FS_NAME_SIZE];
    ios_u8 destination_name[IOS_FS_NAME_SIZE];
    ios_status status = validate_service(service, true);
    if (!valid_directory_cluster(service, source_parent_identity)
        || !valid_directory_cluster(service, destination_parent_identity)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (IOS_FAILED(status)) return status;
    status = canonical_component(source_component, source_component_length, source_name);
    if (IOS_FAILED(status)) return status;
    status = canonical_component(
        destination_component, destination_component_length, destination_name
    );
    if (IOS_FAILED(status)) return status;
    status = find_in_directory(
        service, (ios_u32)source_parent_identity, source_name, &source
    );
    if (IOS_FAILED(status)) return status;
    status = find_in_directory(
        service, (ios_u32)destination_parent_identity, destination_name, NULL
    );
    if (IOS_SUCCEEDED(status)) return IOS_ERROR(IOS_E_ALREADY_EXISTS);
    if (status != IOS_ERROR(IOS_E_NOT_FOUND)) return status;
    status = find_display_base_in_directory(
        service, (ios_u32)destination_parent_identity, destination_name,
        source_parent_identity == destination_parent_identity ? &source : NULL
    );
    if (IOS_SUCCEEDED(status)) return IOS_ERROR(IOS_E_ALREADY_EXISTS);
    if (status != IOS_ERROR(IOS_E_NOT_FOUND)) return status;

    memcpy(source.primary.name, destination_name, IOS_FS_NAME_SIZE);
    if (source.has_companion) {
        if (source_parent_identity == destination_parent_identity) {
            return commit_primary(service, &source, &source.primary);
        }
        memset(&destination, 0, sizeof(destination));
        status = find_file_slots(
            service, (ios_u32)destination_parent_identity, &destination
        );
        if (IOS_FAILED(status)) return status;
        status = commit_primary(service, &destination, &source.primary);
        if (IOS_FAILED(status)) return status;
        return delete_located(service, &source);
    }

    status = ios_fs_primary_encode(&source.primary, &disk);
    if (IOS_FAILED(status)) return status;
    if (source_parent_identity == destination_parent_identity) {
        status = persist_directory_slot(
            service, source.directory_cluster, source.primary_slot,
            &disk, IOS_FS_DIRTY_PRIMARY
        );
        return IOS_FAILED(status) ? status : barrier(service);
    }
    memset(&destination, 0, sizeof(destination));
    status = find_directory_slot(
        service, (ios_u32)destination_parent_identity, &destination
    );
    if (IOS_FAILED(status)) return status;
    status = persist_directory_slot(
        service, destination.directory_cluster, destination.primary_slot,
        &disk, IOS_FS_DIRTY_PRIMARY
    );
    if (IOS_FAILED(status)) return status;
    status = barrier(service);
    if (IOS_FAILED(status)) return status;
    {
        struct ios_fs_primary_disk parent;
        make_internal_record(&parent, true, (ios_u32)destination_parent_identity);
        status = persist_directory_slot(
            service, source.primary.first_cluster, 1,
            &parent, IOS_FS_DIRTY_PRIMARY
        );
    }
    if (IOS_FAILED(status)) return status;
    status = barrier(service);
    if (IOS_FAILED(status)) return status;
    return delete_located(service, &source);
}

ios_status ios_fs_file_service_get_record(
    struct ios_fs_file_service *service,
    ios_u64 object_identity,
    struct ios_fs_primary_disk *primary,
    struct ios_fs_companion_disk *companion,
    ios_u64 *primary_record_location,
    ios_u64 *companion_record_location,
    bool *has_companion
)
{
    ios_u8 sector_bytes[IOS_FS_SECTOR_SIZE];
    const ios_u64 location = object_identity & ~IOS_FS_FILE_OBJECT_BIT;
    const ios_u64 sector = location / IOS_FS_SECTOR_SIZE;
    const ios_size offset = (ios_size)(location % IOS_FS_SECTOR_SIZE);
    ios_status status = validate_service(service, false);
    if (primary == NULL || companion == NULL || primary_record_location == NULL
        || companion_record_location == NULL || has_companion == NULL
        || (object_identity & IOS_FS_FILE_OBJECT_BIT) == 0
        || offset % IOS_FS_PRIMARY_RECORD_SIZE != 0
        || offset > IOS_FS_SECTOR_SIZE - IOS_FS_PRIMARY_RECORD_SIZE
        || service == NULL || service->mount == NULL
        || sector < service->mount->geometry.data_start_sector
        || sector >= service->mount->geometry.total_sectors) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (IOS_FAILED(status)) return status;
    status = block_cache_read(service->sync->cache, sector, sector_bytes);
    if (IOS_FAILED(status)) return status;
    memcpy(primary, sector_bytes + offset, sizeof(*primary));
    if (primary->attributes != IOS_FS_ATTRIBUTE_REGULAR) {
        return IOS_ERROR(IOS_E_NOT_FOUND);
    }
    if (location < IOS_FS_PRIMARY_RECORD_SIZE) return IOS_ERROR(IOS_E_CORRUPT);
    {
        const ios_u64 location_companion = location - IOS_FS_PRIMARY_RECORD_SIZE;
        const ios_u64 sector_companion = location_companion / IOS_FS_SECTOR_SIZE;
        const ios_size offset_companion =
            (ios_size)(location_companion % IOS_FS_SECTOR_SIZE);
        if (sector_companion != sector) {
            status = block_cache_read(service->sync->cache, sector_companion, sector_bytes);
            if (IOS_FAILED(status)) return status;
        }
        memcpy(companion, sector_bytes + offset_companion, sizeof(*companion));
        status = ios_fs_record_pair_validate(companion, primary);
        if (IOS_FAILED(status)) return status;
        *primary_record_location = location;
        *companion_record_location = location_companion;
    }
    *has_companion = true;
    return IOS_OK;
}

ios_u64 ios_fs_file_service_free_bytes(const struct ios_fs_file_service *service)
{
    ios_u64 free_clusters = 0;
    if (IOS_FAILED(validate_service(service, false))) return 0;
    for (ios_size cluster = 2; cluster < service->fat_entry_count; ++cluster) {
        if (service->fat[cluster] == IOS_FS_FAT_FREE) ++free_clusters;
    }
    return free_clusters * IOS_FS_FILE_SERVICE_CLUSTER_BYTES;
}
