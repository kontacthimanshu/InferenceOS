#include <inferenceos/test.h>

#include <inferenceos/block.h>
#include <inferenceos/file_view.h>

#include <string.h>

enum {
    TEXT_TYPE = UINT64_C(0x545854),
    IMAGE_TYPE = UINT64_C(0x504e47),
    COLLIDING_PREFILTER = UINT64_C(0x12345678)
};

struct fake_directory {
    struct ios_vfs_directory_entry entries[4];
    ios_size calls;
};

static struct ios_vfs_directory_entry make_entry(
    const char *name,
    ios_u64 identity,
    ios_u64 type_identity,
    enum ios_vfs_object_kind kind
)
{
    struct ios_vfs_directory_entry entry = {
        .object_identity = identity,
        .internal_type_identity = type_identity,
        .type_prefilter = kind == IOS_VFS_OBJECT_REGULAR_FILE ? COLLIDING_PREFILTER : 0,
        .byte_size = kind == IOS_VFS_OBJECT_REGULAR_FILE ? 64 : 0,
        .allowed_operations = kind == IOS_VFS_OBJECT_DIRECTORY
            ? IOS_VFS_FILE_ENUMERATE : IOS_VFS_FILE_OPEN,
        .kind = kind
    };
    memcpy(entry.display_base_name, name, strlen(name) + 1U);
    return entry;
}

static ios_status enumerate_directory(
    void *context,
    ios_u64 directory_identity,
    ios_u64 continuation,
    struct ios_vfs_directory_entry *entries,
    ios_size capacity,
    ios_size *entry_count,
    ios_u64 *next_continuation
)
{
    struct fake_directory *directory = context;
    ios_size source = (ios_size)continuation;
    ++directory->calls;
    if (directory_identity != IOS_VFS_ROOT_OBJECT_ID || source >= 4) {
        return IOS_ERROR(IOS_E_NOT_FOUND);
    }
    *entry_count = 0;
    while (source < 4 && *entry_count < capacity) {
        entries[(*entry_count)++] = directory->entries[source++];
    }
    *next_continuation = source < 4 ? source : 0;
    return IOS_OK;
}

static void initialize_stack(
    struct fake_directory *directory,
    struct ios_block_device *device,
    struct ios_vfs_object *root,
    struct ios_vfs_mount *mount,
    struct ios_vfs_mount_registry *registry,
    struct ios_type_catalog *catalog,
    struct ios_file_view_service *service,
    ios_type_icon_capability *text_capability,
    ios_type_icon_capability *image_capability
)
{
    *directory = (struct fake_directory){ .entries = {
        make_entry("REPORT", 7, TEXT_TYPE, IOS_VFS_OBJECT_REGULAR_FILE),
        make_entry("CHART", 9, IMAGE_TYPE, IOS_VFS_OBJECT_REGULAR_FILE),
        make_entry("NOTES", 11, TEXT_TYPE, IOS_VFS_OBJECT_REGULAR_FILE),
        make_entry("DOCS", 13, 0, IOS_VFS_OBJECT_DIRECTORY)
    } };
    *device = (struct ios_block_device){ 0 };
    *root = (struct ios_vfs_object){ IOS_VFS_ROOT_OBJECT_ID, IOS_VFS_OBJECT_DIRECTORY, 0 };
    *mount = (struct ios_vfs_mount){
        .driver_name = "fake-vfs",
        .driver_context = directory,
        .device = device,
        .root = root,
        .enumerate = enumerate_directory,
        .state = IOS_MOUNT_RW
    };
    vfs_mount_registry_initialize(registry);
    IOS_TEST_ASSERT_STATUS(vfs_mount_root(registry, mount, "/"), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_type_catalog_initialize(catalog, 0x7777), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_type_catalog_register(catalog, TEXT_TYPE, IOS_ICON_TEXT, text_capability), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_type_catalog_register(catalog, IMAGE_TYPE, IOS_ICON_IMAGE, image_capability), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_file_view_service_initialize(service, registry, catalog), IOS_OK);
}

static ios_status dispatch(
    struct ios_file_view_service *service,
    enum ios_shell_operation operation,
    ios_type_icon_capability type_capability,
    ios_u64 continuation,
    ios_u16 maximum_items,
    struct ios_shell_file_view_reply *reply
)
{
    const struct ios_shell_file_view_request request = {
        .size = sizeof(request),
        .version = IOS_SHELL_PROTOCOL_VERSION,
        .directory_handle = IOS_VFS_ROOT_OBJECT_ID,
        .type_icon_capability = type_capability,
        .continuation = continuation,
        .maximum_items = maximum_items
    };
    *reply = (struct ios_shell_file_view_reply){
        .size = sizeof(*reply), .version = IOS_SHELL_PROTOCOL_VERSION
    };
    return ios_file_view_dispatch(service, 41, 43, operation, &request, reply);
}

static void test_directory_view_returns_bounded_display_safe_entries(void)
{
    struct fake_directory directory;
    struct ios_block_device device;
    struct ios_vfs_object root;
    struct ios_vfs_mount mount;
    struct ios_vfs_mount_registry registry;
    struct ios_type_catalog catalog;
    struct ios_file_view_service service;
    struct ios_shell_file_view_reply reply;
    ios_type_icon_capability text_capability;
    ios_type_icon_capability image_capability;
    initialize_stack(
        &directory, &device, &root, &mount, &registry, &catalog, &service,
        &text_capability, &image_capability
    );
    IOS_TEST_ASSERT_STATUS(
        dispatch(&service, IOS_SHELL_DIRECTORY_VIEW, 0, 0, 4, &reply), IOS_OK);
    IOS_TEST_ASSERT(reply.item_count == 4 && reply.continuation == 0);
    IOS_TEST_ASSERT(strcmp(reply.entries[0].display_name, "REPORT") == 0);
    IOS_TEST_ASSERT(reply.entries[0].type_icon_capability == text_capability);
    IOS_TEST_ASSERT(reply.entries[1].type_icon_capability == image_capability);
    IOS_TEST_ASSERT(reply.entries[3].object_kind == IOS_DISPLAY_SAFE_DIRECTORY);
    for (ios_size index = 0; index < reply.item_count; ++index) {
        IOS_TEST_ASSERT(strchr(reply.entries[index].display_name, '.') == NULL);
        IOS_TEST_ASSERT(reply.entries[index].version == IOS_DISPLAY_SAFE_ENTRY_VERSION);
    }
}

static void test_type_and_search_verify_exact_identity_after_colliding_prefilter(void)
{
    struct fake_directory directory;
    struct ios_block_device device;
    struct ios_vfs_object root;
    struct ios_vfs_mount mount;
    struct ios_vfs_mount_registry registry;
    struct ios_type_catalog catalog;
    struct ios_file_view_service service;
    struct ios_shell_file_view_reply reply;
    ios_type_icon_capability text_capability;
    ios_type_icon_capability image_capability;
    initialize_stack(
        &directory, &device, &root, &mount, &registry, &catalog, &service,
        &text_capability, &image_capability
    );
    IOS_TEST_ASSERT_STATUS(
        dispatch(&service, IOS_SHELL_TYPE_VIEW, text_capability, 0, 4, &reply), IOS_OK);
    IOS_TEST_ASSERT(reply.item_count == 2);
    IOS_TEST_ASSERT(strcmp(reply.entries[0].display_name, "REPORT") == 0);
    IOS_TEST_ASSERT(strcmp(reply.entries[1].display_name, "NOTES") == 0);
    IOS_TEST_ASSERT_STATUS(
        dispatch(&service, IOS_SHELL_SEARCH, image_capability, 0, 4, &reply), IOS_OK);
    IOS_TEST_ASSERT(reply.item_count == 1);
    IOS_TEST_ASSERT(strcmp(reply.entries[0].display_name, "CHART") == 0);
}

static void test_pagination_and_forged_type_capability_are_bounded(void)
{
    struct fake_directory directory;
    struct ios_block_device device;
    struct ios_vfs_object root;
    struct ios_vfs_mount mount;
    struct ios_vfs_mount_registry registry;
    struct ios_type_catalog catalog;
    struct ios_file_view_service service;
    struct ios_shell_file_view_reply reply;
    ios_type_icon_capability text_capability;
    ios_type_icon_capability image_capability;
    initialize_stack(
        &directory, &device, &root, &mount, &registry, &catalog, &service,
        &text_capability, &image_capability
    );
    IOS_TEST_ASSERT_STATUS(
        dispatch(&service, IOS_SHELL_TYPE_VIEW, text_capability, 0, 2, &reply), IOS_OK);
    IOS_TEST_ASSERT(reply.item_count == 1 && reply.continuation == 2);
    IOS_TEST_ASSERT_STATUS(
        dispatch(
            &service, IOS_SHELL_TYPE_VIEW, text_capability,
            reply.continuation, 2, &reply
        ), IOS_OK);
    IOS_TEST_ASSERT(reply.item_count == 1 && reply.continuation == 0);
    const ios_size calls_before_forgery = directory.calls;
    IOS_TEST_ASSERT_STATUS(
        dispatch(&service, IOS_SHELL_SEARCH, UINT64_C(0xdeadbeef), 0, 4, &reply),
        IOS_ERROR(IOS_E_BAD_HANDLE));
    IOS_TEST_ASSERT(directory.calls == calls_before_forgery);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_directory_view_returns_bounded_display_safe_entries),
    IOS_TEST_CASE(test_type_and_search_verify_exact_identity_after_colliding_prefilter),
    IOS_TEST_CASE(test_pagination_and_forged_type_capability_are_bounded)
};

const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
