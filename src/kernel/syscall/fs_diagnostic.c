#include <inferenceos/fs_diagnostic.h>

#include <inferenceos/fs/fat.h>
#include <inferenceos/runtime.h>

static ios_u32 required_scope(enum ios_fs_diagnostic_query query)
{
    switch (query) {
    case IOS_FS_DIAGNOSTIC_QUERY_FILESYSTEM:
        return IOS_FS_DIAGNOSTIC_SCOPE_FILESYSTEM;
    case IOS_FS_DIAGNOSTIC_QUERY_FILE:
    case IOS_FS_DIAGNOSTIC_QUERY_HASH:
        return IOS_FS_DIAGNOSTIC_SCOPE_RECORDS;
    case IOS_FS_DIAGNOSTIC_QUERY_FAT:
        return IOS_FS_DIAGNOSTIC_SCOPE_ALLOCATION;
    default:
        return 0;
    }
}

static ios_status begin_bounded_read(struct ios_fs_mount *mount)
{
    if (mount == NULL || !mount->report.bounds_trusted
        || mount->vfs.state == IOS_MOUNT_REJECTED) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    return mount->vfs.state == IOS_MOUNT_DIAGNOSTIC
        ? ios_fs_diagnostic_begin_read(mount)
        : vfs_mount_begin_operation(&mount->vfs, false);
}

static ios_status end_bounded_read(struct ios_fs_mount *mount)
{
    return mount->vfs.state == IOS_MOUNT_DIAGNOSTIC
        ? ios_fs_diagnostic_end_read(mount)
        : vfs_mount_end_operation(&mount->vfs);
}

static ios_status authorize(
    const struct ios_process *caller,
    const struct ios_fs_diagnostic_request *request
)
{
    const struct ios_fs_diagnostic_authority *authority;
    void *object = NULL;
    ios_status status = handle_table_resolve(
        &caller->handles, request->authority, IOS_OBJECT_DIAGNOSTIC_CAPABILITY,
        IOS_RIGHT_DIAGNOSTIC, &object
    );
    if (IOS_FAILED(status)) return status;
    authority = object;
    if (authority->owner_process_id != caller->process_id
        || authority->owner_application_identity != caller->application_identity
        || (authority->scope & required_scope(request->query)) == 0) {
        return IOS_ERROR(IOS_E_ACCESS_DENIED);
    }
    return IOS_OK;
}

static void fill_filesystem(
    const struct ios_fs_diagnostic_service *service,
    const struct ios_fs_diagnostic_source *source,
    struct ios_fs_diagnostic_filesystem_info *info
)
{
    static const ios_u8 identity[8] = { 'I', 'N', 'F', 'O', 'S', 'F', 'S', '1' };
    memcpy(info->identity, identity, sizeof(info->identity));
    info->format_version = IOS_FS_FORMAT_VERSION;
    info->primary_record_size = IOS_FS_PRIMARY_RECORD_SIZE;
    info->companion_record_size = IOS_FS_COMPANION_RECORD_SIZE;
    info->hash_algorithm_id = IOS_FS_HASH_FNV1A32;
    info->sectors_per_cluster = IOS_FS_SECTORS_PER_CLUSTER;
    info->volume_capacity_bytes = service->mount->geometry.total_sectors * IOS_FS_SECTOR_SIZE;
    info->usable_bytes = service->mount->geometry.usable_bytes;
    info->data_start_sector = service->mount->geometry.data_start_sector;
    info->fat_sectors = service->mount->geometry.fat_sectors;
    info->cluster_count = service->mount->geometry.cluster_count;
    info->mount_state = service->mount->vfs.state;
    info->registry_health = source->registry_health;
    info->registry_active_type_count = source->registry_active_type_count;
    info->free_space_known = source->free_space_known;
    info->free_bytes = source->free_space_known ? source->free_bytes : 0;
}

static ios_status decode_primary(
    const struct ios_fs_diagnostic_source *source,
    struct ios_fs_primary *primary
)
{
    if (!source->has_primary) return IOS_ERROR(IOS_E_NOT_FOUND);
    return ios_fs_primary_decode(&source->primary, primary);
}

static ios_status fill_file(
    const struct ios_fs_diagnostic_source *source,
    struct ios_fs_diagnostic_file_info *info
)
{
    struct ios_fs_primary primary;
    ios_status status = decode_primary(source, &primary);
    if (IOS_FAILED(status)) return status;
    memcpy(info->canonical_name, primary.name, sizeof(info->canonical_name));
    info->object_type = primary.attributes == IOS_FS_ATTRIBUTE_DIRECTORY
        ? IOS_FS_DIRECTORY_ENTRY_DIRECTORY : IOS_FS_DIRECTORY_ENTRY_REGULAR;
    info->attributes = primary.attributes;
    info->size = primary.file_size;
    info->first_cluster = primary.first_cluster;
    info->primary_record_location = source->primary_record_location;
    info->companion_record_location = source->has_companion
        ? source->companion_record_location : UINT64_MAX;
    return IOS_OK;
}

static ios_u32 read_u32(const ios_u8 bytes[4])
{
    return (ios_u32)bytes[0] | (ios_u32)bytes[1] << 8
        | (ios_u32)bytes[2] << 16 | (ios_u32)bytes[3] << 24;
}

static ios_status fill_hash(
    const struct ios_fs_diagnostic_source *source,
    struct ios_fs_diagnostic_hash_info *info
)
{
    struct ios_fs_companion_disk crc_copy;
    struct ios_fs_companion companion;
    struct ios_fs_primary primary;
    ios_size extension_length;
    ios_status primary_status = decode_primary(source, &primary);
    ios_status companion_status;
    if (!source->has_companion) return IOS_ERROR(IOS_E_NOT_FOUND);
    crc_copy = source->companion;
    memset(crc_copy.crc32, 0, sizeof(crc_copy.crc32));
    info->crc_valid = ios_fs_crc32_iso_hdlc(&crc_copy, sizeof(crc_copy))
        == read_u32(source->companion.crc32);
    info->record_version = source->companion.record_version;
    info->hash_algorithm_id = source->companion.hash_algorithm_id;
    info->committed = (source->companion.flags & IOS_FS_COMPANION_FLAG_COMMITTED) != 0;
    memcpy(info->stored_hash, source->companion.extension_hash_text, sizeof(info->stored_hash));
    if (IOS_SUCCEEDED(primary_status)) {
        info->association_checksum_valid = source->companion.primary_name_checksum
            == ios_fs_primary_name_checksum(primary.name);
        if (IOS_SUCCEEDED(ios_fs_name_extension(
                primary.name, info->extension, &extension_length))) {
            info->extension_length = (ios_u8)extension_length;
            ios_fs_hash_text(
                ios_fs_fnv1a32(info->extension, extension_length), info->recomputed_hash
            );
        }
    }
    companion_status = ios_fs_companion_decode(&source->companion, &companion);
    info->validation_status = IOS_FAILED(primary_status) ? primary_status
        : IOS_FAILED(companion_status) ? companion_status
        : ios_fs_record_pair_validate(&source->companion, &source->primary);
    return IOS_OK;
}

static ios_status fill_fat(
    const struct ios_fs_diagnostic_source *source,
    ios_u32 maximum_chain_entries,
    struct ios_fs_diagnostic_fat_info *info
)
{
    struct ios_fs_primary primary;
    ios_size count;
    ios_status status = decode_primary(source, &primary);
    if (IOS_FAILED(status)) return status;
    if (source->fat == NULL || source->fat_entry_count < 3
        || maximum_chain_entries == 0
        || maximum_chain_entries > IOS_FS_DIAGNOSTIC_CHAIN_CAPACITY) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (primary.first_cluster == 0) {
        info->end_of_chain = true;
        return IOS_OK;
    }
    status = ios_fs_fat_traverse(
        source->fat, source->fat_entry_count, primary.first_cluster,
        info->clusters, maximum_chain_entries, &count
    );
    if (IOS_FAILED(status)) return status;
    info->cluster_count = (ios_u32)count;
    info->end_of_chain = true;
    return IOS_OK;
}

ios_status ios_fs_diagnostic_service_initialize(
    struct ios_fs_diagnostic_service *service,
    struct ios_fs_mount *mount,
    ios_fs_diagnostic_snapshot_provider snapshot,
    void *snapshot_context
)
{
    if (service == NULL || mount == NULL || snapshot == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *service = (struct ios_fs_diagnostic_service){ mount, snapshot, snapshot_context };
    return IOS_OK;
}

ios_status ios_fs_diagnostic_dispatch(
    struct ios_fs_diagnostic_service *service,
    const struct ios_process *caller,
    const struct ios_fs_diagnostic_request *request,
    struct ios_fs_diagnostic_reply *reply
)
{
    struct ios_fs_diagnostic_source source;
    ios_status status;
    ios_status end_status;
    if (service == NULL || caller == NULL || request == NULL || reply == NULL
        || service->mount == NULL || service->snapshot == NULL
        || caller->process_id == 0 || caller->application_identity == 0
        || request->size != sizeof(*request)
        || request->version != IOS_FS_DIAGNOSTIC_ABI_VERSION
        || request->reserved != 0 || required_scope(request->query) == 0
        || (request->query != IOS_FS_DIAGNOSTIC_QUERY_FILESYSTEM
            && request->object_identity == 0)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    memset(reply, 0, sizeof(*reply));
    status = authorize(caller, request);
    if (IOS_FAILED(status)) return status;
    status = begin_bounded_read(service->mount);
    if (IOS_FAILED(status)) return status;
    memset(&source, 0, sizeof(source));
    status = service->snapshot(
        service->snapshot_context, request->query, request->object_identity, &source
    );
    if (IOS_SUCCEEDED(status)) {
        reply->size = sizeof(*reply);
        reply->version = IOS_FS_DIAGNOSTIC_ABI_VERSION;
        reply->query = request->query;
        switch (request->query) {
        case IOS_FS_DIAGNOSTIC_QUERY_FILESYSTEM:
            fill_filesystem(service, &source, &reply->value.filesystem);
            break;
        case IOS_FS_DIAGNOSTIC_QUERY_FILE:
            status = fill_file(&source, &reply->value.file);
            break;
        case IOS_FS_DIAGNOSTIC_QUERY_HASH:
            status = fill_hash(&source, &reply->value.hash);
            break;
        case IOS_FS_DIAGNOSTIC_QUERY_FAT:
            status = fill_fat(
                &source, request->maximum_chain_entries, &reply->value.fat
            );
            break;
        default:
            status = IOS_ERROR(IOS_E_INVALID_ARGUMENT);
            break;
        }
    }
    end_status = end_bounded_read(service->mount);
    if (IOS_FAILED(status)) {
        memset(reply, 0, sizeof(*reply));
        return status;
    }
    if (IOS_FAILED(end_status)) {
        memset(reply, 0, sizeof(*reply));
        return end_status;
    }
    return IOS_OK;
}
