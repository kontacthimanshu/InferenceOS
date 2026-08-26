#include <inferenceos/test.h>

#include <inferenceos/cui_fs.h>

#include <string.h>

ios_u64 x86_64_interrupt_save_disable(void) { return 0; }
void x86_64_interrupt_restore(ios_u64 flags) { (void)flags; }

struct output_buffer { char bytes[8192]; ios_size length; };
struct sparse_disk {
    ios_u64 sector_count;
    ios_u8 sectors[3][IOS_FS_SECTOR_SIZE];
    ios_u64 flush_count;
};
struct fake_file_service {
    char path[64];
    char content[256];
    ios_size length;
    bool exists;
};
struct fake_power_transition {
    ios_size calls;
    enum ios_power_action action;
};
struct fake_directory_service {
    char current[IOS_VFS_PATH_CAPACITY];
    char last_created[IOS_VFS_PATH_CAPACITY];
    char last_removed[IOS_VFS_PATH_CAPACITY];
    ios_status remove_status;
};
struct diagnostic_fixture {
    struct ios_fs_primary_disk primary;
    struct ios_fs_companion_disk companion;
    ios_u32 fat[8];
};

static void capture(const char *text, void *opaque)
{
    struct output_buffer *output = opaque;
    ios_size length = strlen(text);
    IOS_TEST_ASSERT(output->length + length < sizeof(output->bytes));
    memcpy(output->bytes + output->length, text, length + 1);
    output->length += length;
}
static ios_status disk_read(
    void *opaque, ios_u64 first_sector, ios_size sector_count, void *buffer)
{
    struct sparse_disk *disk = opaque;
    ios_u8 *output = buffer;
    if (first_sector >= disk->sector_count || sector_count > disk->sector_count - first_sector) {
        return IOS_ERROR(IOS_E_OUT_OF_RANGE);
    }
    for (ios_size index = 0; index < sector_count; ++index) {
        if (first_sector + index < 3) {
            memcpy(output + index * IOS_FS_SECTOR_SIZE, disk->sectors[first_sector + index],
                   IOS_FS_SECTOR_SIZE);
        } else {
            memset(output + index * IOS_FS_SECTOR_SIZE, 0, IOS_FS_SECTOR_SIZE);
        }
    }
    return IOS_OK;
}
static ios_status disk_write(
    void *opaque, ios_u64 first_sector, ios_size sector_count, const void *buffer)
{
    struct sparse_disk *disk = opaque;
    const ios_u8 *input = buffer;
    if (first_sector >= disk->sector_count || sector_count > disk->sector_count - first_sector) {
        return IOS_ERROR(IOS_E_OUT_OF_RANGE);
    }
    for (ios_size index = 0; index < sector_count; ++index) {
        if (first_sector + index < 3) {
            memcpy(disk->sectors[first_sector + index], input + index * IOS_FS_SECTOR_SIZE,
                   IOS_FS_SECTOR_SIZE);
        }
    }
    return IOS_OK;
}
static ios_status disk_flush(void *opaque)
{
    ++((struct sparse_disk *)opaque)->flush_count;
    return IOS_OK;
}
static ios_status initialize_device(struct ios_block_device *device, struct sparse_disk *disk)
{
    const struct ios_block_device_operations operations = { disk_read, disk_write, disk_flush };
    return block_device_initialize(device, disk, &operations, IOS_FS_SECTOR_SIZE,
                                   disk->sector_count, IOS_BLOCK_DEVICE_READY);
}
static ios_status dispatch(
    const char *command, struct ios_cui_command_registry *registry, struct ios_cui_io *io)
{
    struct ios_cui_parsed_line line;
    IOS_TEST_ASSERT(ios_cui_parse_line(command, &line) == IOS_CUI_PARSE_OK);
    return ios_cui_command_dispatch(registry, &line, io);
}

static ios_status diagnostic_snapshot(
    void *opaque, enum ios_fs_diagnostic_query query, ios_u64 identity,
    struct ios_fs_diagnostic_source *source)
{
    struct diagnostic_fixture *fixture = opaque;
    if (query != IOS_FS_DIAGNOSTIC_QUERY_FILESYSTEM && identity != 77) {
        return IOS_ERROR(IOS_E_NOT_FOUND);
    }
    source->primary = fixture->primary;
    source->companion = fixture->companion;
    source->primary_record_location = 4096;
    source->companion_record_location = 4064;
    source->fat = fixture->fat;
    source->fat_entry_count = IOS_ARRAY_COUNT(fixture->fat);
    source->has_primary = true;
    source->has_companion = true;
    source->free_space_known = true;
    source->free_bytes = 12288;
    source->registry_health = IOS_FS_DIAGNOSTIC_REGISTRY_DISABLED;
    return IOS_OK;
}

static ios_status resolve_diagnostic_path(void *opaque, const char *path, ios_u64 *identity)
{
    (void)opaque;
    if (strcmp(path, "/REPORT.TXT") != 0) return IOS_ERROR(IOS_E_NOT_FOUND);
    *identity = 77;
    return IOS_OK;
}

static ios_status fake_create(void *opaque, const char *path)
{
    struct fake_file_service *service = opaque;
    if (service->exists) return IOS_ERROR(IOS_E_ALREADY_EXISTS);
    strcpy(service->path, path);
    service->length = 0;
    service->content[0] = '\0';
    service->exists = true;
    return IOS_OK;
}
static ios_status fake_write(
    void *opaque, const char *path, const void *bytes, ios_size length)
{
    struct fake_file_service *service = opaque;
    if (!service->exists || strcmp(service->path, path) != 0) return IOS_ERROR(IOS_E_NOT_FOUND);
    if (length >= sizeof(service->content)) return IOS_ERROR(IOS_E_NO_SPACE);
    memcpy(service->content, bytes, length);
    service->content[length] = '\0';
    service->length = length;
    return IOS_OK;
}
static ios_status fake_append(
    void *opaque, const char *path, const void *bytes, ios_size length)
{
    struct fake_file_service *service = opaque;
    if (!service->exists || strcmp(service->path, path) != 0) return IOS_ERROR(IOS_E_NOT_FOUND);
    if (length >= sizeof(service->content) - service->length) return IOS_ERROR(IOS_E_NO_SPACE);
    memcpy(service->content + service->length, bytes, length);
    service->length += length;
    service->content[service->length] = '\0';
    return IOS_OK;
}
static ios_status fake_type(
    void *opaque, const char *path, ios_cui_write output, void *output_context)
{
    struct fake_file_service *service = opaque;
    if (!service->exists || strcmp(service->path, path) != 0) return IOS_ERROR(IOS_E_NOT_FOUND);
    output(service->content, output_context);
    return IOS_OK;
}
static ios_status fake_rename(void *opaque, const char *source, const char *destination)
{
    struct fake_file_service *service = opaque;
    if (!service->exists || strcmp(service->path, source) != 0) return IOS_ERROR(IOS_E_NOT_FOUND);
    strcpy(service->path, destination);
    return IOS_OK;
}
static ios_status fake_remove(void *opaque, const char *path)
{
    struct fake_file_service *service = opaque;
    if (!service->exists || strcmp(service->path, path) != 0) return IOS_ERROR(IOS_E_NOT_FOUND);
    service->exists = false;
    return IOS_OK;
}
static ios_status fake_sync(void *opaque)
{
    ios_status *result = opaque;
    return *result;
}
static ios_status fake_power_transition(void *opaque, enum ios_power_action action)
{
    struct fake_power_transition *transition = opaque;
    ++transition->calls;
    transition->action = action;
    return IOS_OK;
}

static ios_status make_safe_entry(
    struct ios_display_safe_entry *entry, const char *name, ios_u64 handle,
    ios_u64 type, ios_u64 size, enum ios_display_safe_object_kind kind
)
{
    const struct ios_display_safe_source_entry source = {
        .base_name = name,
        .object_handle = handle,
        .type_icon_capability = type,
        .byte_size = size,
        .allowed_operations = kind == IOS_DISPLAY_SAFE_DIRECTORY
            ? IOS_DISPLAY_SAFE_OPERATION_ENUMERATE : IOS_DISPLAY_SAFE_OPERATION_OPEN,
        .object_kind = kind
    };
    return ios_display_safe_entry_convert(&source, entry);
}

static ios_status fake_directory_enumerate(
    void *opaque, const char *path, struct ios_display_safe_entry *entries,
    ios_size capacity, ios_size *entry_count
)
{
    (void)opaque;
    if (strcmp(path, ".") != 0 && strcmp(path, "/") != 0) {
        return IOS_ERROR(IOS_E_NOT_FOUND);
    }
    if (capacity < 3) return IOS_ERROR(IOS_E_NO_SPACE);
    IOS_TEST_ASSERT_STATUS(
        make_safe_entry(
            &entries[0], "REPORT", 7, 100, 15, IOS_DISPLAY_SAFE_REGULAR_FILE), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        make_safe_entry(
            &entries[1], "REPORT", 9, 200, 21, IOS_DISPLAY_SAFE_REGULAR_FILE), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        make_safe_entry(&entries[2], "DOCS", 11, 0, 0, IOS_DISPLAY_SAFE_DIRECTORY), IOS_OK);
    *entry_count = 3;
    return IOS_OK;
}

static ios_status malformed_directory_enumerate(
    void *opaque, const char *path, struct ios_display_safe_entry *entries,
    ios_size capacity, ios_size *entry_count
)
{
    (void)opaque;
    (void)path;
    if (capacity == 0) return IOS_ERROR(IOS_E_NO_SPACE);
    memset(entries, 0, sizeof(*entries));
    *entry_count = 1;
    return IOS_OK;
}

static ios_status fake_directory_change_current(void *opaque, const char *path)
{
    struct fake_directory_service *service = opaque;
    if (strcmp(path, "/DOCS") != 0) return IOS_ERROR(IOS_E_NOT_FOUND);
    strcpy(service->current, path);
    return IOS_OK;
}

static ios_status fake_directory_get_current(void *opaque, char *path, ios_size capacity)
{
    struct fake_directory_service *service = opaque;
    ios_size length = strlen(service->current);
    if (capacity <= length) return IOS_ERROR(IOS_E_NO_SPACE);
    memcpy(path, service->current, length + 1U);
    return IOS_OK;
}

static ios_status fake_directory_create(void *opaque, const char *path)
{
    struct fake_directory_service *service = opaque;
    strcpy(service->last_created, path);
    return IOS_OK;
}

static ios_status fake_directory_remove(void *opaque, const char *path)
{
    struct fake_directory_service *service = opaque;
    strcpy(service->last_removed, path);
    return service->remove_status;
}

static void test_storage_commands_format_mount_report_unmount_and_remount(void)
{
    struct sparse_disk disk = {
        .sector_count = IOS_FS_MINIMUM_VOLUME_BYTES / IOS_FS_SECTOR_SIZE
    };
    struct ios_block_device device;
    struct ios_vfs_mount_registry mounts;
    struct ios_fs_mount filesystem;
    struct ios_cui_fs_context context;
    struct ios_cui_command_registry commands;
    struct output_buffer output = { 0 };
    struct ios_cui_io io = { capture, &output, &context, NULL, NULL };
    IOS_TEST_ASSERT_STATUS(initialize_device(&device, &disk), IOS_OK);
    vfs_mount_registry_initialize(&mounts);
    IOS_TEST_ASSERT_STATUS(ios_cui_fs_context_initialize(&context, &mounts, &filesystem), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_cui_fs_add_device(&context, &device), IOS_OK);
    ios_cui_command_registry_initialize(&commands);
    IOS_TEST_ASSERT_STATUS(ios_cui_register_fs_commands(&commands), IOS_OK);
    IOS_TEST_ASSERT_STATUS(dispatch("devices", &commands, &io), IOS_OK);
    IOS_TEST_ASSERT_STATUS(dispatch("diskinfo disk0", &commands, &io), IOS_OK);
    IOS_TEST_ASSERT(strstr(output.bytes, "disk0 status=ready sector_size=512") != NULL);
    IOS_TEST_ASSERT(strstr(output.bytes, "capacity_bytes=50000000000") != NULL);
    IOS_TEST_ASSERT_STATUS(dispatch("format disk0", &commands, &io), IOS_OK);
    IOS_TEST_ASSERT_STATUS(dispatch("mount disk0 /", &commands, &io), IOS_OK);
    IOS_TEST_ASSERT_STATUS(dispatch("fsinfo", &commands, &io), IOS_OK);
    IOS_TEST_ASSERT(strstr(output.bytes, "filesystem=InferenceOS-FS format_version=1") != NULL);
    IOS_TEST_ASSERT(strstr(output.bytes, "mount_state=read_write total_bytes=50000000000") != NULL);
    IOS_TEST_ASSERT(strstr(output.bytes, "sector_size=512 cluster_size=4096 fat_sectors=95271") != NULL);
    IOS_TEST_ASSERT(strstr(output.bytes, "primary_record_size=32 companion_record_size=32") != NULL);
    IOS_TEST_ASSERT_STATUS(dispatch("unmount /", &commands, &io), IOS_OK);
    IOS_TEST_ASSERT_STATUS(dispatch("fsinfo", &commands, &io), IOS_ERROR(IOS_E_NOT_FOUND));
    IOS_TEST_ASSERT_STATUS(dispatch("mount disk0 /", &commands, &io), IOS_OK);
    IOS_TEST_ASSERT_STATUS(dispatch("fsinfo", &commands, &io), IOS_OK);
    IOS_TEST_ASSERT(disk.flush_count == 2 && mounts.root == &filesystem.vfs);
}

static void test_commands_reject_bad_devices_paths_and_unsafe_state(void)
{
    struct sparse_disk disk = { .sector_count = 1000 };
    struct ios_block_device device;
    struct ios_vfs_mount_registry mounts;
    struct ios_fs_mount filesystem;
    struct ios_cui_fs_context context;
    struct ios_cui_command_registry commands;
    struct output_buffer output = { 0 };
    struct ios_cui_io io = { capture, &output, &context, NULL, NULL };
    IOS_TEST_ASSERT_STATUS(initialize_device(&device, &disk), IOS_OK);
    vfs_mount_registry_initialize(&mounts);
    IOS_TEST_ASSERT_STATUS(ios_cui_fs_context_initialize(&context, &mounts, &filesystem), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_cui_fs_add_device(&context, &device), IOS_OK);
    ios_cui_command_registry_initialize(&commands);
    IOS_TEST_ASSERT_STATUS(ios_cui_register_fs_commands(&commands), IOS_OK);
    IOS_TEST_ASSERT_STATUS(dispatch("diskinfo disk9", &commands, &io), IOS_ERROR(IOS_E_NOT_FOUND));
    IOS_TEST_ASSERT_STATUS(dispatch("format disk0", &commands, &io), IOS_ERROR(IOS_E_NO_SPACE));
    IOS_TEST_ASSERT_STATUS(dispatch("mount disk0 /tmp", &commands, &io),
                           IOS_ERROR(IOS_E_INVALID_ARGUMENT));
    IOS_TEST_ASSERT_STATUS(dispatch("unmount /", &commands, &io), IOS_ERROR(IOS_E_NOT_FOUND));
}

static void test_mount_commands_report_diagnostic_and_rejected_states(void)
{
    struct sparse_disk disk = {
        .sector_count = IOS_FS_MINIMUM_VOLUME_BYTES / IOS_FS_SECTOR_SIZE
    };
    struct ios_block_device device;
    struct ios_vfs_mount_registry mounts;
    struct ios_fs_mount filesystem;
    struct ios_cui_fs_context context;
    struct ios_cui_command_registry commands;
    struct output_buffer output = { 0 };
    struct ios_cui_io io = { capture, &output, &context, NULL, NULL };
    IOS_TEST_ASSERT_STATUS(initialize_device(&device, &disk), IOS_OK);
    vfs_mount_registry_initialize(&mounts);
    IOS_TEST_ASSERT_STATUS(
        ios_cui_fs_context_initialize(&context, &mounts, &filesystem), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_cui_fs_add_device(&context, &device), IOS_OK);
    ios_cui_command_registry_initialize(&commands);
    IOS_TEST_ASSERT_STATUS(ios_cui_register_fs_commands(&commands), IOS_OK);
    IOS_TEST_ASSERT_STATUS(dispatch("format disk0", &commands, &io), IOS_OK);
    disk.sectors[1][0x100] ^= 0x80;
    IOS_TEST_ASSERT_STATUS(dispatch("mount disk0 /", &commands, &io), IOS_OK);
    IOS_TEST_ASSERT(strstr(
        output.bytes,
        "state=diagnostic_read_only reason=backup_invalid trusted_superblock=primary") != NULL);
    IOS_TEST_ASSERT_STATUS(dispatch("fsinfo", &commands, &io), IOS_OK);
    IOS_TEST_ASSERT(strstr(
        output.bytes,
        "diagnostic_reason=backup_invalid trusted_superblock=primary") != NULL);
    IOS_TEST_ASSERT_STATUS(dispatch("unmount /", &commands, &io), IOS_OK);
    disk.sectors[0][0x100] ^= 0x80;
    IOS_TEST_ASSERT_STATUS(
        dispatch("mount disk0 /", &commands, &io), IOS_ERROR(IOS_E_CORRUPT));
    IOS_TEST_ASSERT(strstr(output.bytes, "mount rejected reason=superblocks_invalid\n") != NULL);
    IOS_TEST_ASSERT(vfs_root_mount(&mounts) == NULL);
}

static void test_file_commands_share_provider_and_preserve_quoted_content(void)
{
    static const struct ios_cui_file_operations operations = {
        fake_create, fake_write, fake_append, fake_type, fake_rename, fake_remove
    };
    struct ios_vfs_mount_registry mounts;
    struct ios_fs_mount filesystem;
    struct ios_cui_fs_context context;
    struct ios_cui_command_registry commands;
    struct fake_file_service service = { 0 };
    struct output_buffer output = { 0 };
    struct ios_cui_io io = { capture, &output, &context, NULL, NULL };
    vfs_mount_registry_initialize(&mounts);
    IOS_TEST_ASSERT_STATUS(ios_cui_fs_context_initialize(&context, &mounts, &filesystem), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_cui_fs_set_file_operations(&context, &service, &operations), IOS_OK
    );
    ios_cui_command_registry_initialize(&commands);
    IOS_TEST_ASSERT_STATUS(ios_cui_register_fs_commands(&commands), IOS_OK);
    IOS_TEST_ASSERT_STATUS(dispatch("create /REPORT.TXT", &commands, &io), IOS_OK);
    IOS_TEST_ASSERT_STATUS(dispatch("write /REPORT.TXT \"persistent data\"", &commands, &io), IOS_OK);
    IOS_TEST_ASSERT_STATUS(dispatch("append /REPORT.TXT \"!\"", &commands, &io), IOS_OK);
    IOS_TEST_ASSERT_STATUS(dispatch("type /REPORT.TXT", &commands, &io), IOS_OK);
    IOS_TEST_ASSERT(strcmp(output.bytes, "persistent data!") == 0);
    IOS_TEST_ASSERT_STATUS(
        dispatch("rename /REPORT.TXT /SUMMARY.LOG", &commands, &io), IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(dispatch("type /REPORT.TXT", &commands, &io), IOS_ERROR(IOS_E_NOT_FOUND));
    IOS_TEST_ASSERT_STATUS(dispatch("delete /SUMMARY.LOG", &commands, &io), IOS_OK);
    IOS_TEST_ASSERT(!service.exists);
}

static void test_file_commands_validate_arity_and_provider_errors(void)
{
    struct ios_vfs_mount_registry mounts;
    struct ios_fs_mount filesystem;
    struct ios_cui_fs_context context;
    struct ios_cui_command_registry commands;
    struct output_buffer output = { 0 };
    struct ios_cui_io io = { capture, &output, &context, NULL, NULL };
    vfs_mount_registry_initialize(&mounts);
    IOS_TEST_ASSERT_STATUS(ios_cui_fs_context_initialize(&context, &mounts, &filesystem), IOS_OK);
    ios_cui_command_registry_initialize(&commands);
    IOS_TEST_ASSERT_STATUS(ios_cui_register_fs_commands(&commands), IOS_OK);
    IOS_TEST_ASSERT_STATUS(dispatch("create", &commands, &io), IOS_ERROR(IOS_E_INVALID_ARGUMENT));
    IOS_TEST_ASSERT_STATUS(
        dispatch("write /REPORT.TXT", &commands, &io), IOS_ERROR(IOS_E_INVALID_ARGUMENT)
    );
    IOS_TEST_ASSERT_STATUS(
        dispatch("delete /REPORT.TXT extra", &commands, &io), IOS_ERROR(IOS_E_INVALID_ARGUMENT)
    );
}

static void test_sync_command_propagates_durable_provider_result(void)
{
    struct ios_vfs_mount_registry mounts;
    struct ios_fs_mount filesystem;
    struct ios_cui_fs_context context;
    struct ios_cui_command_registry commands;
    struct output_buffer output = { 0 };
    struct ios_cui_io io = { capture, &output, &context, NULL, NULL };
    ios_status sync_result = IOS_ERROR(IOS_E_IO);
    vfs_mount_registry_initialize(&mounts);
    IOS_TEST_ASSERT_STATUS(ios_cui_fs_context_initialize(&context, &mounts, &filesystem), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_cui_fs_set_sync_operation(&context, &sync_result, fake_sync), IOS_OK);
    ios_cui_command_registry_initialize(&commands);
    IOS_TEST_ASSERT_STATUS(ios_cui_register_fs_commands(&commands), IOS_OK);
    IOS_TEST_ASSERT_STATUS(dispatch("sync", &commands, &io), IOS_ERROR(IOS_E_IO));
    sync_result = IOS_OK;
    IOS_TEST_ASSERT_STATUS(dispatch("sync", &commands, &io), IOS_OK);
    IOS_TEST_ASSERT_STATUS(dispatch("sync now", &commands, &io), IOS_ERROR(IOS_E_INVALID_ARGUMENT));
}

static void test_power_commands_refuse_failed_sync_before_transition(void)
{
    struct ios_vfs_mount_registry mounts;
    struct ios_fs_mount filesystem;
    struct ios_cui_fs_context context;
    struct ios_cui_command_registry commands;
    struct ios_power_controller power;
    struct fake_power_transition transition = { 0 };
    struct output_buffer output = { 0 };
    struct ios_cui_io io = { capture, &output, &context, NULL, NULL };
    ios_status sync_result = IOS_ERROR(IOS_E_IO);

    vfs_mount_registry_initialize(&mounts);
    IOS_TEST_ASSERT_STATUS(
        ios_cui_fs_context_initialize(&context, &mounts, &filesystem), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_power_initialize(&power, &transition, fake_power_transition), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_power_set_filesystem_sync(&power, &sync_result, fake_sync), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_cui_fs_set_power_controller(&context, &power), IOS_OK);
    ios_cui_command_registry_initialize(&commands);
    IOS_TEST_ASSERT_STATUS(ios_cui_register_fs_commands(&commands), IOS_OK);

    IOS_TEST_ASSERT_STATUS(dispatch("reboot", &commands, &io), IOS_ERROR(IOS_E_IO));
    IOS_TEST_ASSERT(transition.calls == 0 && power.state == IOS_POWER_READY);
    sync_result = IOS_OK;
    IOS_TEST_ASSERT_STATUS(dispatch("shutdown", &commands, &io), IOS_OK);
    IOS_TEST_ASSERT(transition.calls == 1 && transition.action == IOS_POWER_SHUTDOWN);
    IOS_TEST_ASSERT_STATUS(
        dispatch("shutdown now", &commands, &io), IOS_ERROR(IOS_E_INVALID_ARGUMENT));
}

static void test_dir_uses_display_safe_entries_and_hides_internal_metadata(void)
{
    static const struct ios_cui_directory_operations operations = {
        .enumerate = fake_directory_enumerate
    };
    struct ios_vfs_mount_registry mounts;
    struct ios_fs_mount filesystem;
    struct ios_cui_fs_context context;
    struct ios_cui_command_registry commands;
    struct output_buffer output = { 0 };
    struct ios_cui_io io = { capture, &output, &context, NULL, NULL };
    vfs_mount_registry_initialize(&mounts);
    IOS_TEST_ASSERT_STATUS(ios_cui_fs_context_initialize(&context, &mounts, &filesystem), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_cui_fs_set_directory_operations(&context, NULL, &operations), IOS_OK);
    ios_cui_command_registry_initialize(&commands);
    IOS_TEST_ASSERT_STATUS(ios_cui_register_fs_commands(&commands), IOS_OK);
    IOS_TEST_ASSERT_STATUS(dispatch("dir", &commands, &io), IOS_OK);
    IOS_TEST_ASSERT(strcmp(
        output.bytes,
        "file REPORT size=15\nfile REPORT (2) size=21\ndirectory DOCS\n") == 0);
    IOS_TEST_ASSERT(strstr(output.bytes, ".TXT") == NULL);
    IOS_TEST_ASSERT(strstr(output.bytes, "extension") == NULL);
    IOS_TEST_ASSERT(strstr(output.bytes, "hash") == NULL);
    IOS_TEST_ASSERT_STATUS(dispatch("dir /missing", &commands, &io), IOS_ERROR(IOS_E_NOT_FOUND));
}

static void test_dir_rejects_malformed_batch_before_writing_output(void)
{
    static const struct ios_cui_directory_operations operations = {
        .enumerate = malformed_directory_enumerate
    };
    struct ios_vfs_mount_registry mounts;
    struct ios_fs_mount filesystem;
    struct ios_cui_fs_context context;
    struct ios_cui_command_registry commands;
    struct output_buffer output = { 0 };
    struct ios_cui_io io = { capture, &output, &context, NULL, NULL };
    vfs_mount_registry_initialize(&mounts);
    IOS_TEST_ASSERT_STATUS(ios_cui_fs_context_initialize(&context, &mounts, &filesystem), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_cui_fs_set_directory_operations(&context, NULL, &operations), IOS_OK);
    ios_cui_command_registry_initialize(&commands);
    IOS_TEST_ASSERT_STATUS(ios_cui_register_fs_commands(&commands), IOS_OK);
    IOS_TEST_ASSERT_STATUS(dispatch("dir", &commands, &io), IOS_ERROR(IOS_E_INVALID_ARGUMENT));
    IOS_TEST_ASSERT(output.length == 0);
}

static void test_directory_navigation_and_mutation_commands_use_shared_provider(void)
{
    static const struct ios_cui_directory_operations operations = {
        .enumerate = fake_directory_enumerate,
        .change_current = fake_directory_change_current,
        .get_current = fake_directory_get_current,
        .create = fake_directory_create,
        .remove = fake_directory_remove
    };
    struct ios_vfs_mount_registry mounts;
    struct ios_fs_mount filesystem;
    struct ios_cui_fs_context context;
    struct ios_cui_command_registry commands;
    struct fake_directory_service service = { .current = "/", .remove_status = IOS_OK };
    struct output_buffer output = { 0 };
    struct ios_cui_io io = { capture, &output, &context, NULL, NULL };
    vfs_mount_registry_initialize(&mounts);
    IOS_TEST_ASSERT_STATUS(ios_cui_fs_context_initialize(&context, &mounts, &filesystem), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_cui_fs_set_directory_operations(&context, &service, &operations), IOS_OK);
    ios_cui_command_registry_initialize(&commands);
    IOS_TEST_ASSERT_STATUS(ios_cui_register_fs_commands(&commands), IOS_OK);

    IOS_TEST_ASSERT_STATUS(dispatch("pwd", &commands, &io), IOS_OK);
    IOS_TEST_ASSERT(strcmp(output.bytes, "/\n") == 0);
    IOS_TEST_ASSERT_STATUS(dispatch("mkdir PROJECTS", &commands, &io), IOS_OK);
    IOS_TEST_ASSERT(strcmp(service.last_created, "PROJECTS") == 0);
    IOS_TEST_ASSERT_STATUS(dispatch("cd /DOCS", &commands, &io), IOS_OK);
    IOS_TEST_ASSERT_STATUS(dispatch("pwd", &commands, &io), IOS_OK);
    IOS_TEST_ASSERT(strcmp(output.bytes, "/\n/DOCS\n") == 0);
    IOS_TEST_ASSERT_STATUS(dispatch("rmdir ../OLD", &commands, &io), IOS_OK);
    IOS_TEST_ASSERT(strcmp(service.last_removed, "../OLD") == 0);

    service.remove_status = IOS_ERROR(IOS_E_NOT_EMPTY);
    IOS_TEST_ASSERT_STATUS(
        dispatch("rmdir /DOCS", &commands, &io), IOS_ERROR(IOS_E_NOT_EMPTY));
    IOS_TEST_ASSERT_STATUS(dispatch("cd /missing", &commands, &io), IOS_ERROR(IOS_E_NOT_FOUND));
}

static void test_directory_commands_validate_arity_and_optional_operations(void)
{
    static const struct ios_cui_directory_operations operations = {
        .enumerate = fake_directory_enumerate
    };
    struct ios_vfs_mount_registry mounts;
    struct ios_fs_mount filesystem;
    struct ios_cui_fs_context context;
    struct ios_cui_command_registry commands;
    struct output_buffer output = { 0 };
    struct ios_cui_io io = { capture, &output, &context, NULL, NULL };
    vfs_mount_registry_initialize(&mounts);
    IOS_TEST_ASSERT_STATUS(ios_cui_fs_context_initialize(&context, &mounts, &filesystem), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_cui_fs_set_directory_operations(&context, NULL, &operations), IOS_OK);
    ios_cui_command_registry_initialize(&commands);
    IOS_TEST_ASSERT_STATUS(ios_cui_register_fs_commands(&commands), IOS_OK);

    IOS_TEST_ASSERT_STATUS(dispatch("cd", &commands, &io), IOS_ERROR(IOS_E_INVALID_ARGUMENT));
    IOS_TEST_ASSERT_STATUS(dispatch("pwd extra", &commands, &io), IOS_ERROR(IOS_E_INVALID_ARGUMENT));
    IOS_TEST_ASSERT_STATUS(dispatch("mkdir", &commands, &io), IOS_ERROR(IOS_E_INVALID_ARGUMENT));
    IOS_TEST_ASSERT_STATUS(dispatch("rmdir one two", &commands, &io), IOS_ERROR(IOS_E_INVALID_ARGUMENT));
    IOS_TEST_ASSERT_STATUS(dispatch("cd /", &commands, &io), IOS_ERROR(IOS_E_NOT_SUPPORTED));
    IOS_TEST_ASSERT_STATUS(dispatch("pwd", &commands, &io), IOS_ERROR(IOS_E_NOT_SUPPORTED));
    IOS_TEST_ASSERT_STATUS(dispatch("mkdir A", &commands, &io), IOS_ERROR(IOS_E_NOT_SUPPORTED));
    IOS_TEST_ASSERT_STATUS(dispatch("rmdir A", &commands, &io), IOS_ERROR(IOS_E_NOT_SUPPORTED));
    IOS_TEST_ASSERT(output.length == 0);
}

static void test_privileged_diagnostic_commands_render_authoritative_dtos(void)
{
    static struct ios_vfs_object root = { 2, IOS_VFS_OBJECT_DIRECTORY, 0 };
    const struct ios_fs_primary primary = {
        { 'R', 'E', 'P', 'O', 'R', 'T', ' ', ' ', 'T', 'X', 'T' },
        IOS_FS_ATTRIBUTE_REGULAR, 2, 4096
    };
    struct ios_vfs_mount_registry mounts;
    struct ios_fs_mount filesystem;
    struct ios_cui_fs_context context;
    struct ios_cui_command_registry commands;
    struct ios_process process;
    struct ios_fs_diagnostic_authority authority;
    struct ios_fs_diagnostic_service diagnostic;
    struct diagnostic_fixture fixture;
    struct output_buffer output = { 0 };
    struct ios_cui_io io = { capture, &output, &context, NULL, NULL };
    ios_handle authority_handle;
    memset(&filesystem, 0, sizeof(filesystem));
    memset(&process, 0, sizeof(process));
    memset(&fixture, 0, sizeof(fixture));
    vfs_mount_registry_initialize(&mounts);
    IOS_TEST_ASSERT_STATUS(ios_cui_fs_context_initialize(&context, &mounts, &filesystem), IOS_OK);
    filesystem.vfs.root = &root;
    filesystem.vfs.state = IOS_MOUNT_RW;
    filesystem.vfs.lifecycle = IOS_VFS_MOUNT_ACTIVE;
    filesystem.vfs.mounted = true;
    filesystem.report.bounds_trusted = true;
    filesystem.geometry.total_sectors = 1000;
    filesystem.geometry.usable_bytes = 24576;
    filesystem.geometry.data_start_sector = 16;
    filesystem.geometry.fat_sectors = 1;
    filesystem.geometry.cluster_count = 6;
    process.process_id = 9;
    process.application_identity = UINT64_C(0x435549);
    process.state = IOS_PROCESS_RUNNABLE;
    IOS_TEST_ASSERT_STATUS(handle_table_initialize(&process.handles, process.process_id), IOS_OK);
    authority = (struct ios_fs_diagnostic_authority){
        process.process_id, process.application_identity, IOS_FS_DIAGNOSTIC_SCOPE_ALL
    };
    IOS_TEST_ASSERT_STATUS(handle_table_insert(
        &process.handles, &authority, IOS_OBJECT_DIAGNOSTIC_CAPABILITY,
        IOS_RIGHT_DIAGNOSTIC, NULL, NULL, &authority_handle), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_fs_primary_encode(&primary, &fixture.primary), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_fs_companion_encode(primary.name, true, &fixture.companion), IOS_OK);
    fixture.fat[0] = IOS_FS_FAT_END_OF_CHAIN;
    fixture.fat[1] = IOS_FS_FAT_END_OF_CHAIN;
    fixture.fat[2] = IOS_FS_FAT_END_OF_CHAIN;
    IOS_TEST_ASSERT_STATUS(ios_fs_diagnostic_service_initialize(
        &diagnostic, &filesystem, diagnostic_snapshot, &fixture), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_cui_fs_set_diagnostic_service(
        &context, &diagnostic, &process, authority_handle,
        resolve_diagnostic_path, NULL), IOS_OK);
    ios_cui_command_registry_initialize(&commands);
    IOS_TEST_ASSERT_STATUS(ios_cui_register_fs_commands(&commands), IOS_OK);

    IOS_TEST_ASSERT_STATUS(dispatch("fsinfo", &commands, &io), IOS_OK);
    IOS_TEST_ASSERT(strstr(output.bytes, "filesystem=INFOSFS1 format_version=1") != NULL);
    IOS_TEST_ASSERT(strstr(output.bytes, "free_state=known free_bytes=12288") != NULL);
    IOS_TEST_ASSERT(strstr(output.bytes, "registry=disabled registry_active_types=0") != NULL);
    IOS_TEST_ASSERT_STATUS(dispatch("fileinfo /REPORT.TXT", &commands, &io), IOS_OK);
    IOS_TEST_ASSERT(strstr(output.bytes, "canonical_name=REPORT  TXT object_type=regular_file") != NULL);
    IOS_TEST_ASSERT(strstr(output.bytes, "primary_record_location=4096 companion_record_location=4064") != NULL);
    IOS_TEST_ASSERT_STATUS(dispatch("hashinfo /REPORT.TXT", &commands, &io), IOS_OK);
    IOS_TEST_ASSERT(strstr(output.bytes, "extension=TXT canonical_extension_bytes=TXT extension_length=3") != NULL);
    IOS_TEST_ASSERT(strstr(output.bytes, "committed=yes association_checksum_valid=yes crc_valid=yes") != NULL);
    IOS_TEST_ASSERT_STATUS(dispatch("fatinfo /REPORT.TXT", &commands, &io), IOS_OK);
    IOS_TEST_ASSERT(strstr(output.bytes, "cluster_count=1 chain=2 end_of_chain=yes") != NULL);
}

static void test_internal_diagnostics_require_an_explicit_binding(void)
{
    struct ios_vfs_mount_registry mounts;
    struct ios_fs_mount filesystem;
    struct ios_cui_fs_context context;
    struct ios_cui_command_registry commands;
    struct output_buffer output = { 0 };
    struct ios_cui_io io = { capture, &output, &context, NULL, NULL };
    vfs_mount_registry_initialize(&mounts);
    IOS_TEST_ASSERT_STATUS(ios_cui_fs_context_initialize(&context, &mounts, &filesystem), IOS_OK);
    ios_cui_command_registry_initialize(&commands);
    IOS_TEST_ASSERT_STATUS(ios_cui_register_fs_commands(&commands), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        dispatch("fileinfo /REPORT.TXT", &commands, &io), IOS_ERROR(IOS_E_NOT_SUPPORTED));
    IOS_TEST_ASSERT_STATUS(
        dispatch("hashinfo /REPORT.TXT", &commands, &io), IOS_ERROR(IOS_E_NOT_SUPPORTED));
    IOS_TEST_ASSERT_STATUS(
        dispatch("fatinfo /REPORT.TXT", &commands, &io), IOS_ERROR(IOS_E_NOT_SUPPORTED));
    IOS_TEST_ASSERT(output.length == 0);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_storage_commands_format_mount_report_unmount_and_remount),
    IOS_TEST_CASE(test_commands_reject_bad_devices_paths_and_unsafe_state),
    IOS_TEST_CASE(test_mount_commands_report_diagnostic_and_rejected_states),
    IOS_TEST_CASE(test_file_commands_share_provider_and_preserve_quoted_content),
    IOS_TEST_CASE(test_file_commands_validate_arity_and_provider_errors),
    IOS_TEST_CASE(test_sync_command_propagates_durable_provider_result),
    IOS_TEST_CASE(test_power_commands_refuse_failed_sync_before_transition),
    IOS_TEST_CASE(test_dir_uses_display_safe_entries_and_hides_internal_metadata),
    IOS_TEST_CASE(test_dir_rejects_malformed_batch_before_writing_output),
    IOS_TEST_CASE(test_directory_navigation_and_mutation_commands_use_shared_provider),
    IOS_TEST_CASE(test_directory_commands_validate_arity_and_optional_operations),
    IOS_TEST_CASE(test_privileged_diagnostic_commands_render_authoritative_dtos),
    IOS_TEST_CASE(test_internal_diagnostics_require_an_explicit_binding)
};
const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
