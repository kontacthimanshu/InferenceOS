#include <inferenceos/cui_fs.h>

#include <inferenceos/runtime.h>

static struct ios_cui_fs_context *file_context(struct ios_cui_io *io)
{
    return io == NULL ? NULL : io->command_context;
}

static ios_status create_command(
    ios_size count, const char *const *arguments, struct ios_cui_io *io
)
{
    struct ios_cui_fs_context *context = file_context(io);
    if (count != 2 || context == NULL || context->file_operations.create == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    return context->file_operations.create(context->file_context, arguments[1]);
}

static ios_status write_command(
    ios_size count, const char *const *arguments, struct ios_cui_io *io
)
{
    struct ios_cui_fs_context *context = file_context(io);
    if (count != 3 || context == NULL || context->file_operations.write == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    return context->file_operations.write(
        context->file_context, arguments[1], arguments[2], strlen(arguments[2])
    );
}

static ios_status append_command(
    ios_size count, const char *const *arguments, struct ios_cui_io *io
)
{
    struct ios_cui_fs_context *context = file_context(io);
    if (count != 3 || context == NULL || context->file_operations.append == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    return context->file_operations.append(
        context->file_context, arguments[1], arguments[2], strlen(arguments[2])
    );
}

static ios_status type_command(
    ios_size count, const char *const *arguments, struct ios_cui_io *io
)
{
    struct ios_cui_fs_context *context = file_context(io);
    if (count != 2 || context == NULL || context->file_operations.type == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    return context->file_operations.type(
        context->file_context, arguments[1], io->write, io->write_context
    );
}

static ios_status rename_command(
    ios_size count, const char *const *arguments, struct ios_cui_io *io
)
{
    struct ios_cui_fs_context *context = file_context(io);
    if (count != 3 || context == NULL || context->file_operations.rename == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    return context->file_operations.rename(
        context->file_context, arguments[1], arguments[2]
    );
}

static ios_status delete_command(
    ios_size count, const char *const *arguments, struct ios_cui_io *io
)
{
    struct ios_cui_fs_context *context = file_context(io);
    if (count != 2 || context == NULL || context->file_operations.remove == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    return context->file_operations.remove(context->file_context, arguments[1]);
}

static const struct ios_cui_command file_descriptors[] = {
    { "create", "create an empty file", create_command },
    { "write", "replace file content", write_command },
    { "append", "append file content", append_command },
    { "type", "display file content", type_command },
    { "rename", "rename a file", rename_command },
    { "delete", "delete a file", delete_command }
};

ios_status ios_cui_register_file_commands(struct ios_cui_command_registry *registry)
{
    for (ios_size index = 0; index < IOS_ARRAY_COUNT(file_descriptors); ++index) {
        ios_status status = ios_cui_command_register(registry, &file_descriptors[index]);
        if (IOS_FAILED(status)) return status;
    }
    return IOS_OK;
}
