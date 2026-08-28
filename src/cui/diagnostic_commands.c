#include <inferenceos/cui_fs.h>

#include <inferenceos/runtime.h>

static void write_u64(struct ios_cui_io *io, ios_u64 value)
{
    char buffer[21];
    ios_size cursor = sizeof(buffer) - 1;
    buffer[cursor] = '\0';
    do {
        buffer[--cursor] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    io->write(buffer + cursor, io->write_context);
}

static void write_i64(struct ios_cui_io *io, ios_i64 value)
{
    if (value < 0) {
        io->write("-", io->write_context);
        write_u64(io, (ios_u64)(-(value + 1)) + 1);
    } else {
        write_u64(io, (ios_u64)value);
    }
}

static void write_bytes(struct ios_cui_io *io, const ios_u8 *bytes, ios_size length)
{
    char text[IOS_FS_NAME_SIZE + 1];
    if (length > IOS_FS_NAME_SIZE) length = IOS_FS_NAME_SIZE;
    memcpy(text, bytes, length);
    text[length] = '\0';
    io->write(text, io->write_context);
}

static const char *yes_no(bool value) { return value ? "yes" : "no"; }

static const char *mount_state_name(enum ios_mount_state state)
{
    switch (state) {
    case IOS_MOUNT_RW: return "read_write";
    case IOS_MOUNT_DIAGNOSTIC: return "diagnostic_read_only";
    default: return "rejected";
    }
}

static const char *registry_name(enum ios_fs_diagnostic_registry_health health)
{
    switch (health) {
    case IOS_FS_DIAGNOSTIC_REGISTRY_DISABLED: return "disabled";
    case IOS_FS_DIAGNOSTIC_REGISTRY_HEALTHY: return "healthy";
    default: return "invalid";
    }
}

static const char *object_type_name(enum ios_fs_directory_entry_kind kind)
{
    switch (kind) {
    case IOS_FS_DIRECTORY_ENTRY_REGULAR: return "regular_file";
    case IOS_FS_DIRECTORY_ENTRY_DIRECTORY: return "directory";
    default: return "internal";
    }
}

static ios_status dispatch_query(
    struct ios_cui_fs_context *context,
    enum ios_fs_diagnostic_query query,
    const char *path,
    struct ios_fs_diagnostic_reply *reply
)
{
    ios_u64 object_identity = 0;
    ios_status status;
    if (context == NULL || reply == NULL || context->diagnostic.service == NULL
        || context->diagnostic.caller == NULL
        || context->diagnostic.authority == IOS_INVALID_HANDLE) {
        return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    }
    if (query != IOS_FS_DIAGNOSTIC_QUERY_FILESYSTEM) {
        if (path == NULL || context->diagnostic.resolve_object == NULL) {
            return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
        }
        status = context->diagnostic.resolve_object(
            context->diagnostic.resolver_context, path, &object_identity
        );
        if (IOS_FAILED(status)) return status;
        if (object_identity == 0) return IOS_ERROR(IOS_E_PROTOCOL);
    }
    const struct ios_fs_diagnostic_request request = {
        sizeof(struct ios_fs_diagnostic_request), IOS_FS_DIAGNOSTIC_ABI_VERSION,
        query, context->diagnostic.authority, object_identity,
        IOS_FS_DIAGNOSTIC_CHAIN_CAPACITY, 0
    };
    return ios_fs_diagnostic_dispatch(
        context->diagnostic.service, context->diagnostic.caller, &request, reply
    );
}

ios_status ios_cui_write_expanded_fsinfo(struct ios_cui_io *io)
{
    struct ios_cui_fs_context *context = io == NULL ? NULL : io->command_context;
    struct ios_fs_diagnostic_reply reply;
    ios_status status = dispatch_query(
        context, IOS_FS_DIAGNOSTIC_QUERY_FILESYSTEM, NULL, &reply
    );
    if (IOS_FAILED(status)) return status;
    const struct ios_fs_diagnostic_filesystem_info *info = &reply.value.filesystem;
    io->write("filesystem=", io->write_context);
    write_bytes(io, info->identity, sizeof(info->identity));
    io->write(" format_version=", io->write_context); write_u64(io, info->format_version);
    io->write(" mount_state=", io->write_context); io->write(mount_state_name(info->mount_state), io->write_context);
    io->write(" total_bytes=", io->write_context); write_u64(io, info->volume_capacity_bytes);
    io->write(" usable_bytes=", io->write_context); write_u64(io, info->usable_bytes);
    io->write(" free_state=", io->write_context); io->write(info->free_space_known ? "known" : "unknown", io->write_context);
    if (info->free_space_known) {
        io->write(" free_bytes=", io->write_context); write_u64(io, info->free_bytes);
    }
    io->write(" sectors_per_cluster=", io->write_context); write_u64(io, info->sectors_per_cluster);
    io->write(" data_start_sector=", io->write_context); write_u64(io, info->data_start_sector);
    io->write(" fat_sectors=", io->write_context); write_u64(io, info->fat_sectors);
    io->write(" cluster_count=", io->write_context); write_u64(io, info->cluster_count);
    io->write(" primary_record_size=", io->write_context); write_u64(io, info->primary_record_size);
    io->write(" companion_record_size=", io->write_context); write_u64(io, info->companion_record_size);
    io->write(" hash_algorithm=", io->write_context);
    io->write(info->hash_algorithm_id == IOS_FS_HASH_FNV1A32 ? "FNV-1a-32" : "unsupported", io->write_context);
    io->write(" registry=", io->write_context); io->write(registry_name(info->registry_health), io->write_context);
    io->write(" registry_active_types=", io->write_context); write_u64(io, info->registry_active_type_count);
    io->write(" registry_records=", io->write_context);
    io->write(
        info->registry_health == IOS_FS_DIAGNOSTIC_REGISTRY_DISABLED ? "not_present"
        : info->registry_health == IOS_FS_DIAGNOSTIC_REGISTRY_HEALTHY ? "valid" : "invalid",
        io->write_context
    );
    io->write("\n", io->write_context);
    return IOS_OK;
}

static ios_status fileinfo_command(
    ios_size count, const char *const *arguments, struct ios_cui_io *io
)
{
    struct ios_fs_diagnostic_reply reply;
    if (count != 2 || io == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    ios_status status = dispatch_query(
        io->command_context, IOS_FS_DIAGNOSTIC_QUERY_FILE, arguments[1], &reply
    );
    if (IOS_FAILED(status)) return status;
    const struct ios_fs_diagnostic_file_info *info = &reply.value.file;
    io->write("canonical_name=", io->write_context); write_bytes(io, info->canonical_name, IOS_FS_NAME_SIZE);
    io->write(" object_type=", io->write_context); io->write(object_type_name(info->object_type), io->write_context);
    io->write(" attributes=", io->write_context); write_u64(io, info->attributes);
    io->write(" size=", io->write_context); write_u64(io, info->size);
    io->write(" first_cluster=", io->write_context); write_u64(io, info->first_cluster);
    io->write(" primary_record_location=", io->write_context); write_u64(io, info->primary_record_location);
    io->write(" companion_record_location=", io->write_context); write_u64(io, info->companion_record_location);
    io->write("\n", io->write_context);
    return IOS_OK;
}

static ios_status hashinfo_command(
    ios_size count, const char *const *arguments, struct ios_cui_io *io
)
{
    struct ios_fs_diagnostic_reply reply;
    if (count != 2 || io == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    ios_status status = dispatch_query(
        io->command_context, IOS_FS_DIAGNOSTIC_QUERY_HASH, arguments[1], &reply
    );
    if (IOS_FAILED(status)) return status;
    const struct ios_fs_diagnostic_hash_info *info = &reply.value.hash;
    io->write("extension=", io->write_context); write_bytes(io, info->extension, info->extension_length);
    io->write(" canonical_extension_bytes=", io->write_context); write_bytes(io, info->extension, info->extension_length);
    io->write(" extension_length=", io->write_context); write_u64(io, info->extension_length);
    io->write(" hash_algorithm=", io->write_context);
    io->write(info->hash_algorithm_id == IOS_FS_HASH_FNV1A32 ? "FNV-1a-32" : "unsupported", io->write_context);
    io->write(" stored_hash=", io->write_context); write_bytes(io, info->stored_hash, IOS_FS_HASH_TEXT_SIZE);
    io->write(" recomputed_hash=", io->write_context); write_bytes(io, info->recomputed_hash, IOS_FS_HASH_TEXT_SIZE);
    io->write(" record_version=", io->write_context); write_u64(io, info->record_version);
    io->write(" committed=", io->write_context); io->write(yes_no(info->committed), io->write_context);
    io->write(" association_checksum_valid=", io->write_context); io->write(yes_no(info->association_checksum_valid), io->write_context);
    io->write(" crc_valid=", io->write_context); io->write(yes_no(info->crc_valid), io->write_context);
    io->write(" validation=", io->write_context);
    io->write(IOS_SUCCEEDED(info->validation_status) ? "valid" : "invalid", io->write_context);
    io->write(" validation_status=", io->write_context); write_i64(io, info->validation_status);
    io->write("\n", io->write_context);
    return IOS_OK;
}

static ios_status fatinfo_command(
    ios_size count, const char *const *arguments, struct ios_cui_io *io
)
{
    struct ios_fs_diagnostic_reply reply;
    if (count != 2 || io == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    ios_status status = dispatch_query(
        io->command_context, IOS_FS_DIAGNOSTIC_QUERY_FAT, arguments[1], &reply
    );
    if (IOS_FAILED(status)) return status;
    const struct ios_fs_diagnostic_fat_info *info = &reply.value.fat;
    io->write("cluster_count=", io->write_context); write_u64(io, info->cluster_count);
    io->write(" chain=", io->write_context);
    for (ios_size index = 0; index < info->cluster_count; ++index) {
        if (index != 0) io->write(",", io->write_context);
        write_u64(io, info->clusters[index]);
    }
    io->write(" end_of_chain=", io->write_context); io->write(yes_no(info->end_of_chain), io->write_context);
    io->write("\n", io->write_context);
    return IOS_OK;
}

static const struct ios_cui_command descriptors[] = {
    { "fileinfo", "show privileged internal file metadata", "fileinfo <path>", fileinfo_command },
    { "hashinfo", "show privileged extension-hash validation", "hashinfo <path>", hashinfo_command },
    { "fatinfo", "show a bounded validated cluster chain", "fatinfo <path>", fatinfo_command }
};

ios_status ios_cui_fs_set_diagnostic_service(
    struct ios_cui_fs_context *context,
    struct ios_fs_diagnostic_service *service,
    const struct ios_process *caller,
    ios_handle authority,
    ios_cui_diagnostic_object_resolver resolve_object,
    void *resolver_context
)
{
    if (context == NULL || service == NULL || caller == NULL
        || authority == IOS_INVALID_HANDLE || resolve_object == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    context->diagnostic = (struct ios_cui_diagnostic_binding){
        service, caller, authority, resolve_object, resolver_context
    };
    return IOS_OK;
}

ios_status ios_cui_register_diagnostic_commands(struct ios_cui_command_registry *registry)
{
    for (ios_size index = 0; index < IOS_ARRAY_COUNT(descriptors); ++index) {
        ios_status status = ios_cui_command_register(registry, &descriptors[index]);
        if (IOS_FAILED(status)) return status;
    }
    return IOS_OK;
}
