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
    if (count != 2) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    if (context == NULL) return IOS_ERROR(IOS_E_INVALID_STATE);
    if (context->file_operations.create == NULL) return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    return context->file_operations.create(context->file_context, arguments[1]);
}

static ios_status write_command(
    ios_size count, const char *const *arguments, struct ios_cui_io *io
)
{
    struct ios_cui_fs_context *context = file_context(io);
    ios_status status;
    if (count != 3) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    if (context == NULL) return IOS_ERROR(IOS_E_INVALID_STATE);
    if (context->file_operations.write == NULL) return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    status = context->file_operations.write(
        context->file_context, arguments[1], arguments[2], strlen(arguments[2])
    );
    if (status != IOS_ERROR(IOS_E_NOT_FOUND)) return status;
    if (context->file_operations.create == NULL) return status;
    status = context->file_operations.create(context->file_context, arguments[1]);
    if (IOS_FAILED(status) && status != IOS_ERROR(IOS_E_ALREADY_EXISTS)) return status;
    return context->file_operations.write(
        context->file_context, arguments[1], arguments[2], strlen(arguments[2])
    );
}

static ios_status append_command(
    ios_size count, const char *const *arguments, struct ios_cui_io *io
)
{
    struct ios_cui_fs_context *context = file_context(io);
    if (count != 3) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    if (context == NULL) return IOS_ERROR(IOS_E_INVALID_STATE);
    if (context->file_operations.append == NULL) return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    return context->file_operations.append(
        context->file_context, arguments[1], arguments[2], strlen(arguments[2])
    );
}

static ios_status type_command(
    ios_size count, const char *const *arguments, struct ios_cui_io *io
)
{
    struct ios_cui_fs_context *context = file_context(io);
    if (count != 2) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    if (context == NULL) return IOS_ERROR(IOS_E_INVALID_STATE);
    if (context->file_operations.type == NULL) return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    return context->file_operations.type(
        context->file_context, arguments[1], io->write, io->write_context
    );
}

static ios_status cat_command(
    ios_size count, const char *const *arguments, struct ios_cui_io *io
)
{
    struct ios_cui_fs_context *context = file_context(io);
    if (count != 2) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    if (context == NULL || io == NULL || io->write == NULL) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    if (context->file_operations.cat == NULL) return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    return context->file_operations.cat(
        context->file_context, arguments[1], io->write, io->write_context
    );
}

static ios_status rename_command(
    ios_size count, const char *const *arguments, struct ios_cui_io *io
)
{
    struct ios_cui_fs_context *context = file_context(io);
    if (count != 3) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    if (context == NULL) return IOS_ERROR(IOS_E_INVALID_STATE);
    if (context->file_operations.rename == NULL) return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    return context->file_operations.rename(
        context->file_context, arguments[1], arguments[2]
    );
}

static ios_status delete_command(
    ios_size count, const char *const *arguments, struct ios_cui_io *io
)
{
    struct ios_cui_fs_context *context = file_context(io);
    if (count != 2) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    if (context == NULL) return IOS_ERROR(IOS_E_INVALID_STATE);
    if (context->file_operations.remove == NULL) return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    return context->file_operations.remove(context->file_context, arguments[1]);
}

static ios_status search_command(
    ios_size count, const char *const *arguments, struct ios_cui_io *io
)
{
    struct ios_cui_fs_context *context = file_context(io);
    struct ios_extension_search_request request = {
        .size = sizeof(request),
        .version = IOS_EXTENSION_SEARCH_ABI_VERSION
    };
    struct ios_extension_search_reply reply;
    ios_size extension_length;
    ios_status status;
    if (count != 2) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    if (context == NULL || io == NULL || io->write == NULL) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    if (context->extension_search.service == NULL
        || context->extension_search.caller == NULL) {
        return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    }
    extension_length = strlen(arguments[1]);
    if (extension_length == 0 || extension_length > sizeof(request.extension)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    request.extension_length = (ios_u16)extension_length;
    memcpy(request.extension, arguments[1], extension_length);
    status = ios_extension_search_dispatch(
        context->extension_search.service, context->extension_search.caller,
        &request, &reply
    );
    if (IOS_FAILED(status)) return status;
    if (reply.item_count == 0) {
        io->write("no matches\n", io->write_context);
    } else {
        for (ios_size index = 0; index < reply.item_count; ++index) {
            io->write(reply.entries[index].location, io->write_context);
            io->write("\n", io->write_context);
        }
    }
    if ((reply.flags & IOS_EXTENSION_SEARCH_REPLY_TRUNCATED) != 0) {
        io->write("results truncated\n", io->write_context);
    }
    return IOS_OK;
}

static const struct ios_cui_command file_descriptors[] = {
    { "create", "create an empty file", "create <path>", create_command },
    { "write", "create or initialize an empty file with text", "write <display-path> \"<text>\"", write_command },
    { "append", "append to an empty or validated text file", "append <display-path> \"<text>\"", append_command },
    { "type", "display file content", "type <path>", type_command },
    { "cat", "display validated text file content", "cat <display-path>", cat_command },
    { "rename", "rename a displayed file and preserve its type", "rename <display-source> <display-destination>", rename_command },
    { "delete", "delete a displayed file", "delete <display-path>", delete_command },
    { "search", "find files by extension", "search <extension>", search_command }
};

ios_status ios_cui_register_file_commands(struct ios_cui_command_registry *registry)
{
    for (ios_size index = 0; index < IOS_ARRAY_COUNT(file_descriptors); ++index) {
        ios_status status = ios_cui_command_register(registry, &file_descriptors[index]);
        if (IOS_FAILED(status)) return status;
    }
    return IOS_OK;
}
