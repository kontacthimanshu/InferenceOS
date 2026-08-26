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
static const char *device_state(enum ios_block_device_status status)
{
    switch (status) {
    case IOS_BLOCK_DEVICE_READY: return "ready";
    case IOS_BLOCK_DEVICE_READ_ONLY: return "read_only";
    case IOS_BLOCK_DEVICE_FAILED: return "failed";
    default: return "offline";
    }
}
static const char *mount_state(enum ios_mount_state state)
{
    switch (state) {
    case IOS_MOUNT_RW: return "read_write";
    case IOS_MOUNT_DIAGNOSTIC: return "diagnostic_read_only";
    default: return "rejected";
    }
}
static struct ios_cui_fs_context *get_context(struct ios_cui_io *io)
{
    return io == NULL ? NULL : io->command_context;
}
static ios_status parse_device(
    struct ios_cui_fs_context *context, const char *name, ios_size *index)
{
    ios_size value = 0;
    const char *cursor;
    if (context == NULL || name == NULL || index == NULL || strncmp(name, "disk", 4) != 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    cursor = name + 4;
    if (*cursor == '\0') return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    while (*cursor != '\0') {
        if (*cursor < '0' || *cursor > '9' || value > (SIZE_MAX - 9) / 10) {
            return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
        }
        value = value * 10 + (ios_size)(*cursor++ - '0');
    }
    if (value >= context->device_count) return IOS_ERROR(IOS_E_NOT_FOUND);
    *index = value;
    return IOS_OK;
}
static void write_device_line(
    struct ios_cui_io *io, ios_size index, const struct ios_block_device *device)
{
    io->write("disk", io->write_context); write_u64(io, index);
    io->write(" status=", io->write_context); io->write(device_state(device->status), io->write_context);
    io->write(" sector_size=", io->write_context); write_u64(io, device->logical_sector_size);
    io->write(" sectors=", io->write_context); write_u64(io, device->sector_count);
    io->write(" capacity_bytes=", io->write_context); write_u64(io, block_device_capacity_bytes(device));
    io->write("\n", io->write_context);
}

static ios_status devices_command(
    ios_size count, const char *const *arguments, struct ios_cui_io *io)
{
    struct ios_cui_fs_context *context = get_context(io);
    (void)arguments;
    if (count != 1 || context == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    for (ios_size index = 0; index < context->device_count; ++index) {
        write_device_line(io, index, context->devices[index]);
    }
    return IOS_OK;
}
static ios_status diskinfo_command(
    ios_size count, const char *const *arguments, struct ios_cui_io *io)
{
    struct ios_cui_fs_context *context = get_context(io);
    ios_size index;
    ios_status status;
    if (count != 2) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    status = parse_device(context, arguments[1], &index);
    if (IOS_FAILED(status)) return status;
    write_device_line(io, index, context->devices[index]);
    return IOS_OK;
}
static ios_status format_command(
    ios_size count, const char *const *arguments, struct ios_cui_io *io)
{
    static const ios_u8 label[IOS_FS_VOLUME_LABEL_SIZE] =
        { 'I', 'N', 'F', 'E', 'R', 'E', 'N', 'C', 'E', ' ', ' ' };
    struct ios_cui_fs_context *context = get_context(io);
    struct ios_fs_geometry geometry;
    ios_size index;
    ios_status status;
    if (count != 2 || context == NULL || context->mount_registry == NULL
        || context->mount_registry->root != NULL) return IOS_ERROR(IOS_E_INVALID_STATE);
    status = parse_device(context, arguments[1], &index);
    if (IOS_FAILED(status)) return status;
    if (context->next_volume_serial == UINT32_MAX) return IOS_ERROR(IOS_E_OVERFLOW);
    status = ios_fs_format(context->devices[index], context->next_volume_serial++, label, &geometry);
    if (IOS_FAILED(status)) return status;
    io->write("formatted disk", io->write_context); write_u64(io, index);
    io->write(" total_bytes=", io->write_context);
    write_u64(io, geometry.total_sectors * IOS_FS_SECTOR_SIZE);
    io->write("\n", io->write_context);
    return IOS_OK;
}
static ios_status mount_command(
    ios_size count, const char *const *arguments, struct ios_cui_io *io)
{
    struct ios_cui_fs_context *context = get_context(io);
    ios_size index;
    ios_status status;
    if (count != 3 || context == NULL || strcmp(arguments[2], "/") != 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    status = parse_device(context, arguments[1], &index);
    if (IOS_FAILED(status)) return status;
    status = ios_fs_mount_root(context->filesystem_mount, context->devices[index],
                               context->mount_registry);
    if (IOS_FAILED(status)) {
        const struct ios_fs_mount_report *report =
            ios_fs_mount_report_get(context->filesystem_mount);
        if (report != NULL && report->state == IOS_MOUNT_REJECTED) {
            io->write("mount rejected reason=", io->write_context);
            io->write(ios_fs_mount_reason_name(report->reason), io->write_context);
            io->write("\n", io->write_context);
        }
        return status;
    }
    if (context->mount_ready != NULL) {
        status = context->mount_ready(
            context->mount_ready_context, context->filesystem_mount
        );
        if (IOS_FAILED(status)) {
            (void)vfs_unmount_root(context->mount_registry);
            return status;
        }
    }
    io->write("mounted disk", io->write_context); write_u64(io, index);
    io->write(" at / state=", io->write_context);
    io->write(mount_state(context->filesystem_mount->vfs.state), io->write_context);
    if (context->filesystem_mount->vfs.state == IOS_MOUNT_DIAGNOSTIC) {
        const struct ios_fs_mount_report *report =
            ios_fs_mount_report_get(context->filesystem_mount);
        io->write(" reason=", io->write_context);
        io->write(ios_fs_mount_reason_name(report->reason), io->write_context);
        io->write(" trusted_superblock=", io->write_context);
        io->write(
            ios_fs_trusted_superblock_name(report->trusted_superblock), io->write_context);
    }
    io->write("\n", io->write_context);
    return IOS_OK;
}
static ios_status unmount_command(
    ios_size count, const char *const *arguments, struct ios_cui_io *io)
{
    struct ios_cui_fs_context *context = get_context(io);
    struct ios_vfs_mount *root;
    ios_status status;
    if (count != 2 || context == NULL || strcmp(arguments[1], "/") != 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    root = vfs_root_mount(context->mount_registry);
    if (root == NULL) return IOS_ERROR(IOS_E_NOT_FOUND);
    if (root->active_operations != 0 || root->root->reference_count != 0) {
        return IOS_ERROR(IOS_E_BUSY);
    }
    status = block_device_flush(root->device);
    if (IOS_FAILED(status)) return status;
    status = vfs_unmount_root(context->mount_registry);
    if (IOS_FAILED(status)) return status;
    io->write("unmounted /\n", io->write_context);
    return IOS_OK;
}
static ios_status fsinfo_command(
    ios_size count, const char *const *arguments, struct ios_cui_io *io)
{
    struct ios_cui_fs_context *context = get_context(io);
    struct ios_vfs_mount *root;
    struct ios_fs_mount *filesystem;
    ios_u64 free_bytes;
    (void)arguments;
    if (count != 1 || context == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    ios_status diagnostic_status = ios_cui_write_expanded_fsinfo(io);
    if (diagnostic_status != IOS_ERROR(IOS_E_NOT_SUPPORTED)) return diagnostic_status;
    root = vfs_root_mount(context->mount_registry);
    if (root == NULL || root->driver_context != context->filesystem_mount) {
        return IOS_ERROR(IOS_E_NOT_FOUND);
    }
    filesystem = root->driver_context;
    free_bytes = filesystem->geometry.cluster_count > 0
        ? ((ios_u64)filesystem->geometry.cluster_count - 1)
            * IOS_FS_SECTORS_PER_CLUSTER * IOS_FS_SECTOR_SIZE : 0;
    io->write("filesystem=InferenceOS-FS format_version=1 mount_state=", io->write_context);
    io->write(mount_state(root->state), io->write_context);
    if (root->state == IOS_MOUNT_DIAGNOSTIC) {
        io->write(" diagnostic_reason=", io->write_context);
        io->write(ios_fs_mount_reason_name(filesystem->report.reason), io->write_context);
        io->write(" trusted_superblock=", io->write_context);
        io->write(
            ios_fs_trusted_superblock_name(filesystem->report.trusted_superblock),
            io->write_context);
    }
    io->write(" total_bytes=", io->write_context);
    write_u64(io, filesystem->geometry.total_sectors * IOS_FS_SECTOR_SIZE);
    io->write(" usable_bytes=", io->write_context); write_u64(io, filesystem->geometry.usable_bytes);
    io->write(" free_bytes=", io->write_context); write_u64(io, free_bytes);
    io->write(" sector_size=512 cluster_size=4096 fat_sectors=", io->write_context);
    write_u64(io, filesystem->geometry.fat_sectors);
    io->write(" primary_record_size=32 companion_record_size=32 hash=FNV-1a-32 registry=disabled\n",
              io->write_context);
    return IOS_OK;
}
static ios_status sync_command(
    ios_size count, const char *const *arguments, struct ios_cui_io *io)
{
    struct ios_cui_fs_context *context = get_context(io);
    (void)arguments;
    if (count != 1 || context == NULL || context->sync == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    return context->sync(context->sync_context);
}
static ios_status reboot_command(
    ios_size count, const char *const *arguments, struct ios_cui_io *io)
{
    struct ios_cui_fs_context *context = get_context(io);
    (void)arguments;
    if (count != 1 || context == NULL || context->power == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    return ios_power_request(context->power, IOS_POWER_REBOOT);
}
static ios_status shutdown_command(
    ios_size count, const char *const *arguments, struct ios_cui_io *io)
{
    struct ios_cui_fs_context *context = get_context(io);
    (void)arguments;
    if (count != 1 || context == NULL || context->power == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    return ios_power_request(context->power, IOS_POWER_SHUTDOWN);
}

static const struct ios_cui_command descriptors[] = {
    { "devices", "list block devices", devices_command },
    { "diskinfo", "show block device information", diskinfo_command },
    { "format", "format an InferenceOS-FS device", format_command },
    { "mount", "mount an InferenceOS-FS device", mount_command },
    { "unmount", "flush and unmount a filesystem", unmount_command },
    { "fsinfo", "show mounted filesystem information", fsinfo_command },
    { "sync", "persist filesystem and device state", sync_command },
    { "reboot", "synchronize storage and restart", reboot_command },
    { "shutdown", "synchronize storage and halt", shutdown_command }
};

ios_status ios_cui_fs_context_initialize(
    struct ios_cui_fs_context *context, struct ios_vfs_mount_registry *mount_registry,
    struct ios_fs_mount *filesystem_mount)
{
    if (context == NULL || mount_registry == NULL || filesystem_mount == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    memset(context, 0, sizeof(*context));
    context->mount_registry = mount_registry;
    context->filesystem_mount = filesystem_mount;
    context->next_volume_serial = 1;
    return IOS_OK;
}
ios_status ios_cui_fs_add_device(
    struct ios_cui_fs_context *context, struct ios_block_device *device)
{
    if (context == NULL || device == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    if (context->device_count == IOS_CUI_MAX_BLOCK_DEVICES) return IOS_ERROR(IOS_E_NO_SPACE);
    context->devices[context->device_count++] = device;
    return IOS_OK;
}
ios_status ios_cui_fs_set_file_operations(
    struct ios_cui_fs_context *context, void *file_context,
    const struct ios_cui_file_operations *operations)
{
    if (context == NULL || operations == NULL || operations->create == NULL
        || operations->write == NULL || operations->append == NULL
        || operations->type == NULL || operations->rename == NULL
        || operations->remove == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    context->file_context = file_context;
    context->file_operations = *operations;
    return IOS_OK;
}
ios_status ios_cui_fs_set_directory_operations(
    struct ios_cui_fs_context *context, void *directory_context,
    const struct ios_cui_directory_operations *operations)
{
    if (context == NULL || operations == NULL || operations->enumerate == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    context->directory_context = directory_context;
    context->directory_operations = *operations;
    return IOS_OK;
}
ios_status ios_cui_fs_set_sync_operation(
    struct ios_cui_fs_context *context, void *sync_context,
    ios_status (*sync)(void *context))
{
    if (context == NULL || sync == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    context->sync_context = sync_context;
    context->sync = sync;
    return IOS_OK;
}
ios_status ios_cui_fs_set_power_controller(
    struct ios_cui_fs_context *context, struct ios_power_controller *power)
{
    if (context == NULL || power == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    context->power = power;
    return IOS_OK;
}
ios_status ios_cui_fs_set_mount_ready_operation(
    struct ios_cui_fs_context *context,
    void *mount_ready_context,
    ios_status (*mount_ready)(void *context, struct ios_fs_mount *mount))
{
    if (context == NULL || mount_ready_context == NULL || mount_ready == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    context->mount_ready_context = mount_ready_context;
    context->mount_ready = mount_ready;
    return IOS_OK;
}
ios_status ios_cui_register_fs_commands(struct ios_cui_command_registry *registry)
{
    for (ios_size index = 0; index < IOS_ARRAY_COUNT(descriptors); ++index) {
        ios_status status = ios_cui_command_register(registry, &descriptors[index]);
        if (IOS_FAILED(status)) return status;
    }
    ios_status status = ios_cui_register_file_commands(registry);
    if (IOS_FAILED(status)) return status;
    status = ios_cui_register_directory_commands(registry);
    if (IOS_FAILED(status)) return status;
    return ios_cui_register_diagnostic_commands(registry);
}
