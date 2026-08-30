#include <inferenceos/test.h>

#include <inferenceos/file_command.h>

#include <string.h>

static struct ios_vfs_mount mount;
static ios_u64 selected_operations;
static ios_u32 selected_attributes;
static ios_u8 existing_content[512];
static ios_size existing_length;
static ios_u64 observed_identity;
static ios_u64 observed_parent;
static char observed_base[16];
static ios_size mutation_calls;
static ios_size read_calls;
static char captured_output[1024];
static ios_size captured_length;

ios_status ios_file_view_resolve_display_path(
    struct ios_file_view_service *service,
    const struct ios_vfs_path_context *path_context,
    const char *path,
    struct ios_file_view_resolved_object *resolved
)
{
    (void)service;
    (void)path_context;
    if (strcmp(path, "/") == 0) {
        *resolved = (struct ios_file_view_resolved_object){
            .mount = &mount,
            .object_identity = IOS_VFS_ROOT_OBJECT_ID,
            .parent_identity = IOS_VFS_ROOT_OBJECT_ID,
            .allowed_operations = IOS_VFS_FILE_ENUMERATE,
            .kind = IOS_VFS_OBJECT_DIRECTORY
        };
        return IOS_OK;
    }
    if (strcmp(path, "REPORT") != 0 && strcmp(path, "/REPORT") != 0) {
        return IOS_ERROR(IOS_E_NOT_FOUND);
    }
    *resolved = (struct ios_file_view_resolved_object){
        .mount = &mount,
        .object_identity = 77,
        .parent_identity = IOS_VFS_ROOT_OBJECT_ID,
        .internal_type_identity = UINT64_C(0x494d47),
        .byte_size = existing_length,
        .allowed_operations = selected_operations,
        .generic_attributes = selected_attributes,
        .kind = IOS_VFS_OBJECT_REGULAR_FILE
    };
    return IOS_OK;
}

ios_status vfs_read_object(
    struct ios_vfs_mount *target,
    ios_u64 identity,
    ios_u64 offset,
    void *buffer,
    ios_size capacity,
    ios_size *transferred,
    bool *complete
)
{
    ios_size remaining;
    IOS_TEST_ASSERT(target == &mount && identity == 77);
    IOS_TEST_ASSERT(offset <= existing_length);
    ++read_calls;
    remaining = existing_length - (ios_size)offset;
    if (remaining > capacity) remaining = capacity;
    memcpy(buffer, existing_content + offset, remaining);
    *transferred = remaining;
    *complete = offset + remaining == existing_length;
    return IOS_OK;
}

ios_status vfs_path_normalize(
    const char *current_directory,
    const char *path,
    char *normalized,
    ios_size normalized_capacity
)
{
    ios_size length;
    if (strcmp(current_directory, "/") != 0 || path == NULL || normalized == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    length = strlen(path);
    if (length + (path[0] == '/' ? 1U : 2U) > normalized_capacity) {
        return IOS_ERROR(IOS_E_OUT_OF_RANGE);
    }
    if (path[0] == '/') {
        memcpy(normalized, path, length + 1U);
    } else {
        normalized[0] = '/';
        memcpy(normalized + 1, path, length + 1U);
    }
    return IOS_OK;
}

ios_status vfs_replace_object(
    struct ios_vfs_mount *target,
    ios_u64 identity,
    const void *bytes,
    ios_size length
)
{
    (void)bytes;
    (void)length;
    IOS_TEST_ASSERT(target == &mount);
    observed_identity = identity;
    ++mutation_calls;
    return IOS_OK;
}

ios_status vfs_append_object(
    struct ios_vfs_mount *target,
    ios_u64 identity,
    const void *bytes,
    ios_size length
)
{
    return vfs_replace_object(target, identity, bytes, length);
}

ios_status vfs_remove_object(struct ios_vfs_mount *target, ios_u64 identity)
{
    return vfs_replace_object(target, identity, NULL, 0);
}

ios_status vfs_rename_object(
    struct ios_vfs_mount *target,
    ios_u64 identity,
    ios_u64 parent_identity,
    const char *base,
    ios_size base_length
)
{
    IOS_TEST_ASSERT(target == &mount && base_length < sizeof(observed_base));
    observed_identity = identity;
    observed_parent = parent_identity;
    memcpy(observed_base, base, base_length);
    observed_base[base_length] = '\0';
    ++mutation_calls;
    return IOS_OK;
}

static void initialize_service(
    struct ios_file_command_service *commands,
    struct ios_file_view_service *view,
    struct ios_vfs_path_context *path
)
{
    memset(&mount, 0, sizeof(mount));
    mount.state = IOS_MOUNT_RW;
    memset(view, 0, sizeof(*view));
    memset(path, 0, sizeof(*path));
    path->current_directory[0] = '/';
    path->current_directory[1] = '\0';
    selected_operations = IOS_VFS_FILE_WRITE | IOS_VFS_FILE_RENAME
        | IOS_VFS_FILE_DELETE | IOS_VFS_FILE_READ;
    selected_attributes = 0;
    memset(existing_content, 0, sizeof(existing_content));
    existing_length = 0;
    observed_identity = 0;
    observed_parent = 0;
    observed_base[0] = '\0';
    mutation_calls = 0;
    read_calls = 0;
    captured_output[0] = '\0';
    captured_length = 0;
    IOS_TEST_ASSERT_STATUS(
        ios_file_command_service_initialize(commands, view, path), IOS_OK
    );
}

static void capture_output(const char *text, void *context)
{
    const ios_size length = strlen(text);
    (void)context;
    IOS_TEST_ASSERT(captured_length + length < sizeof(captured_output));
    memcpy(captured_output + captured_length, text, length + 1U);
    captured_length += length;
}

static void set_existing_content(const void *bytes, ios_size length)
{
    IOS_TEST_ASSERT(length <= sizeof(existing_content));
    memcpy(existing_content, bytes, length);
    existing_length = length;
}

static void test_write_initializes_empty_file_without_using_hidden_type(void)
{
    struct ios_file_command_service commands;
    struct ios_file_view_service view;
    struct ios_vfs_path_context path;
    initialize_service(&commands, &view, &path);
    IOS_TEST_ASSERT_STATUS(
        ios_file_command_write(&commands, "REPORT", "hello", 5), IOS_OK
    );
    IOS_TEST_ASSERT(observed_identity == 77 && mutation_calls == 1);
    IOS_TEST_ASSERT(read_calls == 0);
}

static void test_append_accepts_empty_or_supported_ascii_text(void)
{
    static const char text[] = "first line\nsecond\tline\r\n";
    struct ios_file_command_service commands;
    struct ios_file_view_service view;
    struct ios_vfs_path_context path;
    initialize_service(&commands, &view, &path);
    IOS_TEST_ASSERT_STATUS(
        ios_file_command_append(&commands, "/REPORT", "!", 1), IOS_OK
    );
    IOS_TEST_ASSERT(observed_identity == 77 && mutation_calls == 1 && read_calls == 0);
    set_existing_content(text, sizeof(text) - 1U);
    IOS_TEST_ASSERT_STATUS(
        ios_file_command_append(&commands, "/REPORT", "more", 4), IOS_OK
    );
    IOS_TEST_ASSERT(observed_identity == 77 && mutation_calls == 2 && read_calls == 1);
}

static void test_write_rejects_nonempty_files_before_mutation(void)
{
    struct ios_file_command_service commands;
    struct ios_file_view_service view;
    struct ios_vfs_path_context path;
    initialize_service(&commands, &view, &path);
    set_existing_content("already text", 12);
    IOS_TEST_ASSERT_STATUS(
        ios_file_command_write(&commands, "REPORT", "bad", 3),
        IOS_ERROR(IOS_E_UNEXPECTED_FORMAT)
    );
    IOS_TEST_ASSERT(mutation_calls == 0 && read_calls == 0);
}

static void test_binary_content_and_invalid_input_fail_before_mutation(void)
{
    static const ios_u8 nul_input[] = { 'a', 0, 'b' };
    struct ios_file_command_service commands;
    struct ios_file_view_service view;
    struct ios_vfs_path_context path;
    initialize_service(&commands, &view, &path);
    memset(existing_content, 'a', 300);
    existing_content[299] = 0;
    existing_length = 300;
    IOS_TEST_ASSERT_STATUS(
        ios_file_command_append(&commands, "REPORT", "bad", 3),
        IOS_ERROR(IOS_E_UNEXPECTED_FORMAT)
    );
    IOS_TEST_ASSERT(mutation_calls == 0 && read_calls == 2);
    existing_length = 0;
    IOS_TEST_ASSERT_STATUS(
        ios_file_command_write(&commands, "REPORT", nul_input, sizeof(nul_input)),
        IOS_ERROR(IOS_E_UNEXPECTED_FORMAT)
    );
    IOS_TEST_ASSERT(mutation_calls == 0);
}

static void test_read_only_writes_fail_before_content_checks(void)
{
    struct ios_file_command_service commands;
    struct ios_file_view_service view;
    struct ios_vfs_path_context path;
    initialize_service(&commands, &view, &path);
    selected_operations &= ~IOS_VFS_FILE_WRITE;
    selected_attributes = IOS_VFS_ATTRIBUTE_READ_ONLY;
    IOS_TEST_ASSERT_STATUS(
        ios_file_command_append(&commands, "REPORT", "bad", 3),
        IOS_ERROR(IOS_E_READ_ONLY)
    );
    IOS_TEST_ASSERT(mutation_calls == 0 && read_calls == 0);
}

static void test_cat_reads_display_selected_supported_text_without_mutation(void)
{
    static const char text[] = "first line\nsecond\tline\r\n";
    struct ios_file_command_service commands;
    struct ios_file_view_service view;
    struct ios_vfs_path_context path;
    initialize_service(&commands, &view, &path);
    set_existing_content(text, sizeof(text) - 1U);
    IOS_TEST_ASSERT_STATUS(
        ios_file_command_cat(&commands, "REPORT", capture_output, NULL), IOS_OK
    );
    IOS_TEST_ASSERT(strcmp(captured_output, text) == 0);
    IOS_TEST_ASSERT(mutation_calls == 0 && read_calls == 2);
}

static void test_cat_rejects_binary_content_before_producing_output(void)
{
    struct ios_file_command_service commands;
    struct ios_file_view_service view;
    struct ios_vfs_path_context path;
    initialize_service(&commands, &view, &path);
    memset(existing_content, 'a', 300);
    existing_content[299] = 0;
    existing_length = 300;
    IOS_TEST_ASSERT_STATUS(
        ios_file_command_cat(&commands, "/REPORT", capture_output, NULL),
        IOS_ERROR(IOS_E_UNEXPECTED_FORMAT)
    );
    IOS_TEST_ASSERT(captured_length == 0 && mutation_calls == 0 && read_calls == 2);
}

static void test_cat_requires_read_permission(void)
{
    struct ios_file_command_service commands;
    struct ios_file_view_service view;
    struct ios_vfs_path_context path;
    initialize_service(&commands, &view, &path);
    selected_operations &= ~IOS_VFS_FILE_READ;
    IOS_TEST_ASSERT_STATUS(
        ios_file_command_cat(&commands, "REPORT", capture_output, NULL),
        IOS_ERROR(IOS_E_ACCESS_DENIED)
    );
    IOS_TEST_ASSERT(captured_length == 0 && mutation_calls == 0 && read_calls == 0);
}

static void test_rename_preserves_hidden_type_below_the_visible_contract(void)
{
    struct ios_file_command_service commands;
    struct ios_file_view_service view;
    struct ios_vfs_path_context path;
    initialize_service(&commands, &view, &path);
    IOS_TEST_ASSERT_STATUS(
        ios_file_command_rename(&commands, "REPORT", "SUMMARY"), IOS_OK
    );
    IOS_TEST_ASSERT(observed_identity == 77);
    IOS_TEST_ASSERT(observed_parent == IOS_VFS_ROOT_OBJECT_ID);
    IOS_TEST_ASSERT(strcmp(observed_base, "SUMMARY") == 0);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_write_initializes_empty_file_without_using_hidden_type),
    IOS_TEST_CASE(test_append_accepts_empty_or_supported_ascii_text),
    IOS_TEST_CASE(test_write_rejects_nonempty_files_before_mutation),
    IOS_TEST_CASE(test_binary_content_and_invalid_input_fail_before_mutation),
    IOS_TEST_CASE(test_read_only_writes_fail_before_content_checks),
    IOS_TEST_CASE(test_cat_reads_display_selected_supported_text_without_mutation),
    IOS_TEST_CASE(test_cat_rejects_binary_content_before_producing_output),
    IOS_TEST_CASE(test_cat_requires_read_permission),
    IOS_TEST_CASE(test_rename_preserves_hidden_type_below_the_visible_contract)
};

const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
