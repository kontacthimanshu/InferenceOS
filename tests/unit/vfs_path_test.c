#include <inferenceos/test.h>

#include <inferenceos/vfs.h>

#include <string.h>

struct lookup_fixture {
    ios_size calls;
};

static bool component_is(const char *component, ios_size length, const char *expected)
{
    return strlen(expected) == length && memcmp(component, expected, length) == 0;
}

static ios_status lookup_child(
    void *context,
    ios_u64 directory_identity,
    const char *component,
    ios_size component_length,
    struct ios_vfs_object *object
)
{
    struct lookup_fixture *fixture = context;
    ++fixture->calls;
    if (directory_identity == 2 && component_is(component, component_length, "DOCS")) {
        *object = (struct ios_vfs_object){ 3, IOS_VFS_OBJECT_DIRECTORY, 0 };
        return IOS_OK;
    }
    if (directory_identity == 3 && component_is(component, component_length, "REPORT.TXT")) {
        *object = (struct ios_vfs_object){ 4, IOS_VFS_OBJECT_REGULAR_FILE, 0 };
        return IOS_OK;
    }
    if (directory_identity == 3 && component_is(component, component_length, "SUB")) {
        *object = (struct ios_vfs_object){ 5, IOS_VFS_OBJECT_DIRECTORY, 0 };
        return IOS_OK;
    }
    return IOS_ERROR(IOS_E_NOT_FOUND);
}

static void initialize_path_fixture(
    struct lookup_fixture *fixture,
    struct ios_vfs_object *root,
    struct ios_vfs_mount *mount,
    struct ios_vfs_path_context *context
)
{
    memset(fixture, 0, sizeof(*fixture));
    *root = (struct ios_vfs_object){ 2, IOS_VFS_OBJECT_DIRECTORY, 0 };
    memset(mount, 0, sizeof(*mount));
    mount->driver_name = "test";
    mount->driver_context = fixture;
    mount->root = root;
    mount->lookup = lookup_child;
    mount->state = IOS_MOUNT_RW;
    mount->lifecycle = IOS_VFS_MOUNT_ACTIVE;
    mount->generation = 7;
    mount->mounted = true;
    IOS_TEST_ASSERT_STATUS(vfs_path_context_initialize(context, mount), IOS_OK);
}

static void assert_normalizes(
    const char *current_directory, const char *path, const char *expected
)
{
    char normalized[IOS_VFS_PATH_CAPACITY];
    IOS_TEST_ASSERT_STATUS(vfs_path_normalize(
        current_directory, path, normalized, sizeof(normalized)), IOS_OK);
    IOS_TEST_ASSERT(strcmp(normalized, expected) == 0);
}

static void test_normalizes_absolute_and_relative_paths(void)
{
    assert_normalizes("/", "/", "/");
    assert_normalizes("/DOCS", "REPORT.TXT", "/DOCS/REPORT.TXT");
    assert_normalizes("/DOCS/2026", "./A/../REPORT.TXT", "/DOCS/2026/REPORT.TXT");
    assert_normalizes("/IGNORED", "//DOCS///REPORT.TXT/", "/DOCS/REPORT.TXT");
}

static void test_parent_traversal_cannot_escape_root(void)
{
    assert_normalizes("/", "..", "/");
    assert_normalizes("/DOCS", "../../../../REPORT.TXT", "/REPORT.TXT");
    assert_normalizes("/A/B", "/../../C", "/C");
}

static void test_accepts_contract_path_and_depth_boundaries(void)
{
    char path[IOS_VFS_PATH_CAPACITY];
    char normalized[IOS_VFS_PATH_CAPACITY];
    ios_size cursor = 0;
    path[cursor++] = '/';
    for (ios_size level = 0; level < IOS_VFS_MAX_DIRECTORY_LEVELS - 1; ++level) {
        if (level != 0) path[cursor++] = '/';
        path[cursor++] = (char)('A' + level);
    }
    path[cursor] = '\0';
    IOS_TEST_ASSERT_STATUS(vfs_path_normalize(
        "/", path, normalized, sizeof(normalized)), IOS_OK);
    IOS_TEST_ASSERT(strcmp(normalized, path) == 0);

    memset(path, 'A', IOS_VFS_PATH_MAX);
    path[0] = '/';
    path[IOS_VFS_PATH_MAX] = '\0';
    IOS_TEST_ASSERT_STATUS(vfs_path_normalize(
        "/", path, normalized, sizeof(normalized)), IOS_OK);
    IOS_TEST_ASSERT(strlen(normalized) == IOS_VFS_PATH_MAX);
}

static void test_rejects_overlong_deep_or_invalid_paths_without_partial_output(void)
{
    char overlong[IOS_VFS_PATH_CAPACITY + 1];
    char normalized[IOS_VFS_PATH_CAPACITY];
    memset(overlong, 'A', sizeof(overlong));
    overlong[0] = '/';
    overlong[sizeof(overlong) - 1] = '\0';
    normalized[0] = 'X';
    IOS_TEST_ASSERT_STATUS(vfs_path_normalize(
        "/", overlong, normalized, sizeof(normalized)), IOS_ERROR(IOS_E_OUT_OF_RANGE));
    IOS_TEST_ASSERT(normalized[0] == '\0');

    IOS_TEST_ASSERT_STATUS(vfs_path_normalize(
        "/", "/A/B/C/D/E/F/G/H/I/J/K/L/M/N/O/P", normalized, sizeof(normalized)),
        IOS_ERROR(IOS_E_OUT_OF_RANGE));
    IOS_TEST_ASSERT(normalized[0] == '\0');
    IOS_TEST_ASSERT_STATUS(vfs_path_normalize(
        "/", "/DOCS\\REPORT", normalized, sizeof(normalized)),
        IOS_ERROR(IOS_E_INVALID_ARGUMENT));
    IOS_TEST_ASSERT(normalized[0] == '\0');
    IOS_TEST_ASSERT_STATUS(vfs_path_normalize(
        "/", "/DOCS/REPORT", normalized, 5), IOS_ERROR(IOS_E_OUT_OF_RANGE));
    IOS_TEST_ASSERT(normalized[0] == '\0');
}

static void test_rejects_invalid_arguments(void)
{
    char normalized[IOS_VFS_PATH_CAPACITY];
    IOS_TEST_ASSERT_STATUS(vfs_path_normalize(
        "DOCS", "REPORT", normalized, sizeof(normalized)), IOS_ERROR(IOS_E_INVALID_ARGUMENT));
    IOS_TEST_ASSERT_STATUS(vfs_path_normalize(
        "/", "", normalized, sizeof(normalized)), IOS_ERROR(IOS_E_INVALID_ARGUMENT));
    IOS_TEST_ASSERT_STATUS(vfs_path_normalize(
        "/", "/", NULL, sizeof(normalized)), IOS_ERROR(IOS_E_INVALID_ARGUMENT));
}

static void test_resolves_absolute_and_relative_objects(void)
{
    struct lookup_fixture fixture;
    struct ios_vfs_object root;
    struct ios_vfs_mount mount;
    struct ios_vfs_path_context context;
    struct ios_vfs_object object;
    initialize_path_fixture(&fixture, &root, &mount, &context);
    IOS_TEST_ASSERT_STATUS(vfs_path_resolve(&context, "/DOCS/REPORT.TXT", &object), IOS_OK);
    IOS_TEST_ASSERT(object.identity == 4 && object.kind == IOS_VFS_OBJECT_REGULAR_FILE);
    IOS_TEST_ASSERT_STATUS(vfs_path_set_current(&context, "/DOCS"), IOS_OK);
    IOS_TEST_ASSERT_STATUS(vfs_path_resolve(&context, "./SUB", &object), IOS_OK);
    IOS_TEST_ASSERT(object.identity == 5 && object.kind == IOS_VFS_OBJECT_DIRECTORY);
    IOS_TEST_ASSERT_STATUS(vfs_path_resolve(&context, "../DOCS/REPORT.TXT", &object), IOS_OK);
    IOS_TEST_ASSERT(object.identity == 4);
    IOS_TEST_ASSERT(fixture.calls == 7);
    IOS_TEST_ASSERT(root.reference_count == 0 && mount.active_operations == 0);
}

static void test_current_directory_updates_atomically_and_clamps_at_root(void)
{
    struct lookup_fixture fixture;
    struct ios_vfs_object root;
    struct ios_vfs_mount mount;
    struct ios_vfs_path_context context;
    char path[IOS_VFS_PATH_CAPACITY];
    initialize_path_fixture(&fixture, &root, &mount, &context);
    IOS_TEST_ASSERT_STATUS(vfs_path_set_current(&context, "../../"), IOS_OK);
    IOS_TEST_ASSERT_STATUS(vfs_path_get_current(&context, path, sizeof(path)), IOS_OK);
    IOS_TEST_ASSERT(strcmp(path, "/") == 0 && context.current_directory_identity == 2);
    IOS_TEST_ASSERT_STATUS(vfs_path_set_current(&context, "/DOCS"), IOS_OK);
    IOS_TEST_ASSERT_STATUS(vfs_path_set_current(&context, "REPORT.TXT"),
                           IOS_ERROR(IOS_E_INVALID_ARGUMENT));
    IOS_TEST_ASSERT_STATUS(vfs_path_get_current(&context, path, sizeof(path)), IOS_OK);
    IOS_TEST_ASSERT(strcmp(path, "/DOCS") == 0 && context.current_directory_identity == 3);
    IOS_TEST_ASSERT_STATUS(vfs_path_set_current(&context, "MISSING"),
                           IOS_ERROR(IOS_E_NOT_FOUND));
    IOS_TEST_ASSERT(strcmp(context.current_directory, "/DOCS") == 0);
}

static void test_rejects_stale_context_and_invalid_driver_results(void)
{
    struct lookup_fixture fixture;
    struct ios_vfs_object root;
    struct ios_vfs_mount mount;
    struct ios_vfs_path_context context;
    struct ios_vfs_object object;
    char path[4];
    initialize_path_fixture(&fixture, &root, &mount, &context);
    ++mount.generation;
    IOS_TEST_ASSERT_STATUS(vfs_path_resolve(&context, "/", &object),
                           IOS_ERROR(IOS_E_INVALID_STATE));
    IOS_TEST_ASSERT(object.identity == 0);
    IOS_TEST_ASSERT_STATUS(vfs_path_get_current(&context, path, sizeof(path)),
                           IOS_ERROR(IOS_E_INVALID_STATE));
    IOS_TEST_ASSERT(*path == '\0');
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_normalizes_absolute_and_relative_paths),
    IOS_TEST_CASE(test_parent_traversal_cannot_escape_root),
    IOS_TEST_CASE(test_accepts_contract_path_and_depth_boundaries),
    IOS_TEST_CASE(test_rejects_overlong_deep_or_invalid_paths_without_partial_output),
    IOS_TEST_CASE(test_rejects_invalid_arguments),
    IOS_TEST_CASE(test_resolves_absolute_and_relative_objects),
    IOS_TEST_CASE(test_current_directory_updates_atomically_and_clamps_at_root),
    IOS_TEST_CASE(test_rejects_stale_context_and_invalid_driver_results)
};
const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
