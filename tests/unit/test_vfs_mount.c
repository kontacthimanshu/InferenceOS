#include "../support/test_assert.h"

#include <inferenceos/memory.h>
#include <inferenceos/vfs.h>

#define TEST_PATH_LITERAL(value)                                                \
    ((inferenceos_vfs_path) { (value), sizeof(value) - 1U })

/* The fake adapter owns these definitions. Generic callers see only the
 * incomplete declarations from vfs.h and can pass references, never inspect
 * filesystem-private locations or allocation metadata. */
struct inferenceos_vfs_mount { inferenceos_u32 private_cookie; };
struct inferenceos_vfs_node { inferenceos_u32 private_cookie; };

typedef struct fake_filesystem {
    inferenceos_vfs_mount mount;
    inferenceos_vfs_node node;
    inferenceos_vfs_mount_state requested_state;
    inferenceos_vfs_status mount_status;
    inferenceos_vfs_status unmount_status;
    inferenceos_vfs_status sync_status;
    inferenceos_size mount_calls;
    inferenceos_size unmount_calls;
    inferenceos_size sync_calls;
    inferenceos_size resolve_calls;
    inferenceos_size release_calls;
    inferenceos_size create_calls;
    inferenceos_size metadata_calls;
    inferenceos_vfs_mount *observed_mount;
    const inferenceos_vfs_node *observed_node;
} fake_filesystem;

static fake_filesystem fake;

static inferenceos_vfs_status fake_mount(
    void *context,
    inferenceos_vfs_mount **mount,
    inferenceos_vfs_mount_state *state
)
{
    fake_filesystem *filesystem = context;
    ++filesystem->mount_calls;
    *state = filesystem->requested_state;
    if (!inferenceos_vfs_status_is_success(filesystem->mount_status)) {
        *mount = NULL;
        return filesystem->mount_status;
    }
    *mount = &filesystem->mount;
    return INFERENCEOS_VFS_STATUS_OK;
}

static inferenceos_vfs_status fake_unmount(inferenceos_vfs_mount *mount)
{
    ++fake.unmount_calls;
    fake.observed_mount = mount;
    return fake.unmount_status;
}

static inferenceos_vfs_status fake_sync(inferenceos_vfs_mount *mount)
{
    ++fake.sync_calls;
    fake.observed_mount = mount;
    return fake.sync_status;
}

static inferenceos_vfs_status fake_resolve(
    inferenceos_vfs_mount *mount,
    inferenceos_vfs_node *current_directory,
    inferenceos_vfs_path path,
    inferenceos_vfs_node **node
)
{
    (void)path;
    ++fake.resolve_calls;
    fake.observed_mount = mount;
    fake.observed_node = current_directory;
    *node = &fake.node;
    return INFERENCEOS_VFS_STATUS_OK;
}

static inferenceos_vfs_status fake_release_node(inferenceos_vfs_node *node)
{
    ++fake.release_calls;
    fake.observed_node = node;
    return INFERENCEOS_VFS_STATUS_OK;
}

static inferenceos_vfs_status fake_create(
    inferenceos_vfs_mount *mount,
    inferenceos_vfs_node *current_directory,
    inferenceos_vfs_path path
)
{
    (void)path;
    ++fake.create_calls;
    fake.observed_mount = mount;
    fake.observed_node = current_directory;
    return INFERENCEOS_VFS_STATUS_OK;
}

static inferenceos_vfs_status fake_metadata_query(
    const inferenceos_vfs_node *node,
    inferenceos_vfs_metadata *metadata
)
{
    ++fake.metadata_calls;
    fake.observed_node = node;
    metadata->type = INFERENCEOS_VFS_NODE_DIRECTORY;
    metadata->attributes = 0U;
    metadata->size = 0U;
    return INFERENCEOS_VFS_STATUS_OK;
}

static const inferenceos_vfs_operations fake_operations = {
    .mount = fake_mount,
    .unmount = fake_unmount,
    .sync = fake_sync,
    .resolve = fake_resolve,
    .release_node = fake_release_node,
    .create = fake_create,
    .metadata_query = fake_metadata_query
};

static const inferenceos_vfs_filesystem fake_adapter = {
    .name = "fakefs",
    .operations = &fake_operations,
    .filesystem_context = &fake
};

static void setup_fake(inferenceos_vfs_mount_state state)
{
    (void)memset(&fake, 0, sizeof(fake));
    fake.mount.private_cookie = UINT32_C(0x4D4F554E);
    fake.node.private_cookie = UINT32_C(0x4E4F4445);
    fake.requested_state = state;
    fake.mount_status = INFERENCEOS_VFS_STATUS_OK;
    fake.unmount_status = INFERENCEOS_VFS_STATUS_OK;
    fake.sync_status = INFERENCEOS_VFS_STATUS_OK;
}

static void mount_fake(inferenceos_vfs_mount_state expected)
{
    inferenceos_vfs_mount_state state = INFERENCEOS_VFS_MOUNT_REJECTED;

    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_STATUS_OK,
        inferenceos_vfs_mount_root(&fake_adapter, &state));
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(expected, state);
}

static void test_single_root_mount_lifecycle_and_state(void)
{
    inferenceos_vfs_mount_state state;

    setup_fake(INFERENCEOS_VFS_MOUNT_CLEAN_WRITABLE);
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_STATUS_NOT_MOUNTED,
        inferenceos_vfs_root_state(&state));
    mount_fake(INFERENCEOS_VFS_MOUNT_CLEAN_WRITABLE);
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_STATUS_OK,
        inferenceos_vfs_root_state(&state));
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(
        INFERENCEOS_VFS_MOUNT_CLEAN_WRITABLE, state);
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_STATUS_BUSY,
        inferenceos_vfs_mount_root(&fake_adapter, &state));
    INFERENCEOS_TEST_ASSERT_SIZE_EQUAL(1U, fake.mount_calls);
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_STATUS_OK,
        inferenceos_vfs_unmount_root());
    INFERENCEOS_TEST_ASSERT_POINTER_EQUAL(&fake.mount, fake.observed_mount);
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_STATUS_NOT_MOUNTED,
        inferenceos_vfs_root_state(&state));
}

static void test_rejected_mount_is_never_installed(void)
{
    inferenceos_vfs_mount_state state = INFERENCEOS_VFS_MOUNT_UNMOUNTED;

    setup_fake(INFERENCEOS_VFS_MOUNT_REJECTED);
    fake.mount_status = INFERENCEOS_VFS_STATUS_CORRUPT;
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_STATUS_CORRUPT,
        inferenceos_vfs_mount_root(&fake_adapter, &state));
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_MOUNT_REJECTED, state);
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_STATUS_NOT_MOUNTED,
        inferenceos_vfs_root_state(&state));
    INFERENCEOS_TEST_ASSERT_SIZE_EQUAL(0U, fake.unmount_calls);
}

static void test_read_only_mount_blocks_mutation_at_vfs_boundary(void)
{
    setup_fake(INFERENCEOS_VFS_MOUNT_DIAGNOSTIC_READ_ONLY);
    mount_fake(INFERENCEOS_VFS_MOUNT_DIAGNOSTIC_READ_ONLY);
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_STATUS_READ_ONLY,
        inferenceos_vfs_create(NULL, TEST_PATH_LITERAL("/NO.TXT")));
    INFERENCEOS_TEST_ASSERT_SIZE_EQUAL(0U, fake.create_calls);
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_STATUS_OK,
        inferenceos_vfs_unmount_root());
}

static void test_command_boundary_delegates_only_through_vfs(void)
{
    inferenceos_vfs_node *node = NULL;

    setup_fake(INFERENCEOS_VFS_MOUNT_CLEAN_WRITABLE);
    mount_fake(INFERENCEOS_VFS_MOUNT_CLEAN_WRITABLE);
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_STATUS_OK,
        inferenceos_vfs_resolve(NULL, TEST_PATH_LITERAL("/"), &node));
    INFERENCEOS_TEST_ASSERT_POINTER_EQUAL(&fake.node, node);
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_STATUS_OK,
        inferenceos_vfs_create(node, TEST_PATH_LITERAL("TEST.TXT")));
    INFERENCEOS_TEST_ASSERT_SIZE_EQUAL(1U, fake.resolve_calls);
    INFERENCEOS_TEST_ASSERT_SIZE_EQUAL(1U, fake.create_calls);
    INFERENCEOS_TEST_ASSERT_POINTER_EQUAL(&fake.mount, fake.observed_mount);
    INFERENCEOS_TEST_ASSERT_POINTER_EQUAL(node, fake.observed_node);
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_STATUS_OK,
        inferenceos_vfs_release_node(node));
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_STATUS_OK,
        inferenceos_vfs_unmount_root());
}

static void test_opaque_node_is_rejected_after_unmount(void)
{
    inferenceos_vfs_node *node = NULL;
    inferenceos_vfs_metadata metadata;

    setup_fake(INFERENCEOS_VFS_MOUNT_CLEAN_WRITABLE);
    mount_fake(INFERENCEOS_VFS_MOUNT_CLEAN_WRITABLE);
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_STATUS_OK,
        inferenceos_vfs_resolve(NULL, TEST_PATH_LITERAL("/"), &node));
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_STATUS_OK,
        inferenceos_vfs_unmount_root());
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_STATUS_NOT_MOUNTED,
        inferenceos_vfs_metadata_query(node, &metadata));
    INFERENCEOS_TEST_ASSERT_SIZE_EQUAL(0U, fake.metadata_calls);
}

static void test_sync_and_unmount_failures_preserve_active_state(void)
{
    inferenceos_vfs_mount_state state;

    setup_fake(INFERENCEOS_VFS_MOUNT_CLEAN_WRITABLE);
    mount_fake(INFERENCEOS_VFS_MOUNT_CLEAN_WRITABLE);
    fake.sync_status = INFERENCEOS_VFS_STATUS_TIMEOUT;
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_STATUS_TIMEOUT,
        inferenceos_vfs_sync_root());
    fake.unmount_status = INFERENCEOS_VFS_STATUS_IO_ERROR;
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_STATUS_IO_ERROR,
        inferenceos_vfs_unmount_root());
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_STATUS_OK,
        inferenceos_vfs_root_state(&state));
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(
        INFERENCEOS_VFS_MOUNT_CLEAN_WRITABLE, state);
    fake.unmount_status = INFERENCEOS_VFS_STATUS_OK;
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_STATUS_OK,
        inferenceos_vfs_unmount_root());
}

static const inferenceos_test_case vfs_mount_cases[] = {
    INFERENCEOS_TEST_CASE(test_single_root_mount_lifecycle_and_state),
    INFERENCEOS_TEST_CASE(test_rejected_mount_is_never_installed),
    INFERENCEOS_TEST_CASE(test_read_only_mount_blocks_mutation_at_vfs_boundary),
    INFERENCEOS_TEST_CASE(test_command_boundary_delegates_only_through_vfs),
    INFERENCEOS_TEST_CASE(test_opaque_node_is_rejected_after_unmount),
    INFERENCEOS_TEST_CASE(test_sync_and_unmount_failures_preserve_active_state)
};

const inferenceos_test_suite *inferenceos_test_suite_definition(void)
{
    static const inferenceos_test_suite suite = {
        .name = "vfs-mount",
        .cases = vfs_mount_cases,
        .case_count = INFERENCEOS_ARRAY_COUNT(vfs_mount_cases)
    };
    return &suite;
}
