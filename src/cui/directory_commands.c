#include <inferenceos/cui_fs.h>
#include <inferenceos/vfs.h>

enum {
    IOS_CUI_DIRECTORY_ENTRY_CAPACITY = 64
};

static void write_u64(struct ios_cui_io *io, ios_u64 value)
{
    char buffer[21];
    ios_size cursor = sizeof(buffer) - 1U;
    buffer[cursor] = '\0';
    do {
        buffer[--cursor] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0);
    io->write(buffer + cursor, io->write_context);
}

static ios_status dir_command(
    ios_size count, const char *const *arguments, struct ios_cui_io *io
)
{
    struct ios_cui_fs_context *context = io == NULL ? NULL : io->command_context;
    struct ios_display_safe_entry entries[IOS_CUI_DIRECTORY_ENTRY_CAPACITY];
    ios_size entry_count = 0;
    const char *path;
    ios_status status;
    if ((count != 1 && count != 2) || context == NULL
        || context->directory_operations.enumerate == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    path = count == 1 ? "." : arguments[1];
    status = context->directory_operations.enumerate(
        context->directory_context, path, entries,
        IOS_CUI_DIRECTORY_ENTRY_CAPACITY, &entry_count
    );
    if (IOS_FAILED(status)) return status;
    if (entry_count > IOS_CUI_DIRECTORY_ENTRY_CAPACITY) return IOS_ERROR(IOS_E_PROTOCOL);
    status = ios_display_safe_entries_validate_final(entries, entry_count);
    if (IOS_FAILED(status)) return status;
    for (ios_size index = 0; index < entry_count; ++index) {
        const struct ios_display_safe_entry *entry = &entries[index];
        io->write(
            entry->object_kind == IOS_DISPLAY_SAFE_DIRECTORY ? "directory " : "file ",
            io->write_context
        );
        io->write(entry->display_name, io->write_context);
        if (entry->object_kind == IOS_DISPLAY_SAFE_REGULAR_FILE) {
            io->write(" size=", io->write_context);
            write_u64(io, entry->byte_size);
        }
        if ((entry->generic_attributes & IOS_DISPLAY_SAFE_ATTRIBUTE_READ_ONLY) != 0) {
            io->write(" read_only", io->write_context);
        }
        io->write("\n", io->write_context);
    }
    return IOS_OK;
}

static ios_status cd_command(
    ios_size count, const char *const *arguments, struct ios_cui_io *io
)
{
    struct ios_cui_fs_context *context = io == NULL ? NULL : io->command_context;
    if (count != 2 || arguments == NULL || context == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (context->directory_operations.change_current == NULL) {
        return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    }
    return context->directory_operations.change_current(
        context->directory_context, arguments[1]
    );
}

static ios_status pwd_command(
    ios_size count, const char *const *arguments, struct ios_cui_io *io
)
{
    struct ios_cui_fs_context *context = io == NULL ? NULL : io->command_context;
    char path[IOS_VFS_PATH_CAPACITY] = { 0 };
    ios_status status;
    (void)arguments;
    if (count != 1 || context == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    if (context->directory_operations.get_current == NULL) {
        return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    }
    status = context->directory_operations.get_current(
        context->directory_context, path, sizeof(path)
    );
    if (IOS_FAILED(status)) return status;
    path[sizeof(path) - 1U] = '\0';
    io->write(path, io->write_context);
    io->write("\n", io->write_context);
    return IOS_OK;
}

static ios_status mkdir_command(
    ios_size count, const char *const *arguments, struct ios_cui_io *io
)
{
    struct ios_cui_fs_context *context = io == NULL ? NULL : io->command_context;
    if (count != 2 || arguments == NULL || context == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (context->directory_operations.create == NULL) {
        return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    }
    return context->directory_operations.create(context->directory_context, arguments[1]);
}

static ios_status rmdir_command(
    ios_size count, const char *const *arguments, struct ios_cui_io *io
)
{
    struct ios_cui_fs_context *context = io == NULL ? NULL : io->command_context;
    if (count != 2 || arguments == NULL || context == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (context->directory_operations.remove == NULL) {
        return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    }
    return context->directory_operations.remove(context->directory_context, arguments[1]);
}

static const struct ios_cui_command directory_descriptors[] = {
    { "dir", "list display-safe directory entries", "dir [path]", dir_command },
    { "cd", "change the current directory", "cd <path>", cd_command },
    { "pwd", "print the current directory", "pwd", pwd_command },
    { "mkdir", "create a directory", "mkdir <path>", mkdir_command },
    { "rmdir", "remove an empty directory", "rmdir <path>", rmdir_command }
};

ios_status ios_cui_register_directory_commands(struct ios_cui_command_registry *registry)
{
    if (registry == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    for (ios_size index = 0; index < IOS_ARRAY_COUNT(directory_descriptors); ++index) {
        ios_status status = ios_cui_command_register(registry, &directory_descriptors[index]);
        if (IOS_FAILED(status)) return status;
    }
    return IOS_OK;
}
