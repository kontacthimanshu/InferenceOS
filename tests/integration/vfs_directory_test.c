#include <inferenceos/test.h>

#include <inferenceos/vfs.h>

#include <string.h>

enum { NODE_CAPACITY = 8, NODE_NAME_CAPACITY = 16 };

struct test_node {
    ios_u64 identity;
    ios_u64 parent;
    enum ios_vfs_object_kind kind;
    char name[NODE_NAME_CAPACITY];
    bool present;
};

struct directory_fixture {
    struct test_node nodes[NODE_CAPACITY];
    ios_u64 next_identity;
};

static bool name_is(
    const struct test_node *node, const char *component, ios_size component_length
)
{
    return node->present && strlen(node->name) == component_length
        && memcmp(node->name, component, component_length) == 0;
}

static struct test_node *find_child(
    struct directory_fixture *fixture,
    ios_u64 parent,
    const char *component,
    ios_size component_length
)
{
    for (ios_size index = 0; index < IOS_ARRAY_COUNT(fixture->nodes); ++index) {
        if (fixture->nodes[index].parent == parent
            && name_is(&fixture->nodes[index], component, component_length)) {
            return &fixture->nodes[index];
        }
    }
    return NULL;
}

static ios_status lookup_node(
    void *context, ios_u64 parent, const char *component,
    ios_size component_length, struct ios_vfs_object *object
)
{
    struct test_node *node = find_child(context, parent, component, component_length);
    if (node == NULL) return IOS_ERROR(IOS_E_NOT_FOUND);
    *object = (struct ios_vfs_object){ node->identity, node->kind, 0 };
    return IOS_OK;
}

static ios_status create_directory(
    void *context, ios_u64 parent, const char *component,
    ios_size component_length, struct ios_vfs_object *object
)
{
    struct directory_fixture *fixture = context;
    if (find_child(fixture, parent, component, component_length) != NULL) {
        return IOS_ERROR(IOS_E_ALREADY_EXISTS);
    }
    if (component_length == 0 || component_length >= NODE_NAME_CAPACITY) {
        return IOS_ERROR(IOS_E_OUT_OF_RANGE);
    }
    for (ios_size index = 0; index < IOS_ARRAY_COUNT(fixture->nodes); ++index) {
        struct test_node *node = &fixture->nodes[index];
        if (!node->present) {
            memset(node, 0, sizeof(*node));
            node->identity = fixture->next_identity++;
            node->parent = parent;
            node->kind = IOS_VFS_OBJECT_DIRECTORY;
            memcpy(node->name, component, component_length);
            node->present = true;
            *object = (struct ios_vfs_object){ node->identity, node->kind, 0 };
            return IOS_OK;
        }
    }
    return IOS_ERROR(IOS_E_NO_SPACE);
}

static ios_status remove_directory(
    void *context, ios_u64 parent, const char *component, ios_size component_length
)
{
    struct directory_fixture *fixture = context;
    struct test_node *node = find_child(fixture, parent, component, component_length);
    if (node == NULL) return IOS_ERROR(IOS_E_NOT_FOUND);
    if (node->kind != IOS_VFS_OBJECT_DIRECTORY) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    for (ios_size index = 0; index < IOS_ARRAY_COUNT(fixture->nodes); ++index) {
        if (fixture->nodes[index].present
            && fixture->nodes[index].parent == node->identity) {
            return IOS_ERROR(IOS_E_NOT_EMPTY);
        }
    }
    node->present = false;
    return IOS_OK;
}

static ios_status rename_node(
    void *context,
    ios_u64 source_parent,
    const char *source_component,
    ios_size source_length,
    ios_u64 destination_parent,
    const char *destination_component,
    ios_size destination_length
)
{
    struct directory_fixture *fixture = context;
    struct test_node *node = find_child(
        fixture, source_parent, source_component, source_length
    );
    if (node == NULL) return IOS_ERROR(IOS_E_NOT_FOUND);
    if (find_child(fixture, destination_parent, destination_component,
                   destination_length) != NULL) return IOS_ERROR(IOS_E_ALREADY_EXISTS);
    if (destination_length == 0 || destination_length >= NODE_NAME_CAPACITY) {
        return IOS_ERROR(IOS_E_OUT_OF_RANGE);
    }
    memset(node->name, 0, sizeof(node->name));
    memcpy(node->name, destination_component, destination_length);
    node->parent = destination_parent;
    return IOS_OK;
}

static ios_status enumerate_nodes(
    void *context,
    ios_u64 parent,
    ios_u64 continuation,
    struct ios_vfs_directory_entry *entries,
    ios_size capacity,
    ios_size *entry_count,
    ios_u64 *next_continuation
)
{
    struct directory_fixture *fixture = context;
    ios_size count = 0;
    for (ios_size index = (ios_size)continuation;
         index < IOS_ARRAY_COUNT(fixture->nodes); ++index) {
        const struct test_node *node = &fixture->nodes[index];
        if (!node->present || node->parent != parent) continue;
        if (count == capacity) {
            *entry_count = count;
            *next_continuation = index;
            return IOS_OK;
        }
        memset(&entries[count], 0, sizeof(entries[count]));
        memcpy(entries[count].display_base_name, node->name, strlen(node->name));
        entries[count].object_identity = node->identity;
        entries[count].kind = node->kind;
        ++count;
    }
    *entry_count = count;
    *next_continuation = 0;
    return IOS_OK;
}

static void initialize_fixture(
    struct directory_fixture *fixture,
    struct ios_vfs_object *root,
    struct ios_vfs_mount *mount,
    struct ios_vfs_path_context *first,
    struct ios_vfs_path_context *second
)
{
    memset(fixture, 0, sizeof(*fixture));
    fixture->next_identity = 3;
    *root = (struct ios_vfs_object){ 2, IOS_VFS_OBJECT_DIRECTORY, 0 };
    memset(mount, 0, sizeof(*mount));
    mount->driver_name = "test";
    mount->driver_context = fixture;
    mount->root = root;
    mount->lookup = lookup_node;
    mount->create_directory = create_directory;
    mount->remove_directory = remove_directory;
    mount->rename = rename_node;
    mount->enumerate = enumerate_nodes;
    mount->state = IOS_MOUNT_RW;
    mount->lifecycle = IOS_VFS_MOUNT_ACTIVE;
    mount->generation = 1;
    mount->mounted = true;
    IOS_TEST_ASSERT_STATUS(vfs_path_context_initialize(first, mount), IOS_OK);
    IOS_TEST_ASSERT_STATUS(vfs_path_context_initialize(second, mount), IOS_OK);
}

static void test_create_and_list_are_coherent_across_contexts(void)
{
    struct directory_fixture fixture;
    struct ios_vfs_object root, created, resolved;
    struct ios_vfs_mount mount;
    struct ios_vfs_path_context cui, gui;
    struct ios_vfs_directory_entry entry;
    ios_size count;
    ios_u64 next;
    initialize_fixture(&fixture, &root, &mount, &cui, &gui);
    IOS_TEST_ASSERT_STATUS(vfs_create_directory(&cui, "/DOCS", &created), IOS_OK);
    IOS_TEST_ASSERT(created.kind == IOS_VFS_OBJECT_DIRECTORY);
    IOS_TEST_ASSERT_STATUS(vfs_path_resolve(&gui, "/DOCS", &resolved), IOS_OK);
    IOS_TEST_ASSERT(resolved.identity == created.identity);
    IOS_TEST_ASSERT_STATUS(vfs_list_directory(
        &gui, "/", 0, &entry, 1, &count, &next), IOS_OK);
    IOS_TEST_ASSERT(count == 1 && next == 0);
    IOS_TEST_ASSERT(strcmp(entry.display_base_name, "DOCS") == 0);
}

static void test_nonempty_remove_and_rename_preserve_shared_namespace(void)
{
    struct directory_fixture fixture;
    struct ios_vfs_object root, object;
    struct ios_vfs_mount mount;
    struct ios_vfs_path_context cui, gui;
    initialize_fixture(&fixture, &root, &mount, &cui, &gui);
    IOS_TEST_ASSERT_STATUS(vfs_create_directory(&cui, "/DOCS", &object), IOS_OK);
    IOS_TEST_ASSERT_STATUS(vfs_create_directory(&gui, "/DOCS/SUB", &object), IOS_OK);
    IOS_TEST_ASSERT_STATUS(vfs_remove_directory(&cui, "/DOCS"),
                           IOS_ERROR(IOS_E_NOT_EMPTY));
    IOS_TEST_ASSERT_STATUS(vfs_rename(&gui, "/DOCS/SUB", "/DOCS/ARCHIVE"), IOS_OK);
    IOS_TEST_ASSERT_STATUS(vfs_path_resolve(&cui, "/DOCS/ARCHIVE", &object), IOS_OK);
    IOS_TEST_ASSERT_STATUS(vfs_path_resolve(&cui, "/DOCS/SUB", &object),
                           IOS_ERROR(IOS_E_NOT_FOUND));
    IOS_TEST_ASSERT_STATUS(vfs_remove_directory(&cui, "/DOCS/ARCHIVE"), IOS_OK);
    IOS_TEST_ASSERT_STATUS(vfs_remove_directory(&gui, "/DOCS"), IOS_OK);
    IOS_TEST_ASSERT_STATUS(vfs_path_resolve(&cui, "/DOCS", &object),
                           IOS_ERROR(IOS_E_NOT_FOUND));
}

static void test_rejects_root_mutation_descendant_rename_and_read_only_mount(void)
{
    struct directory_fixture fixture;
    struct ios_vfs_object root, object;
    struct ios_vfs_mount mount;
    struct ios_vfs_path_context cui, gui;
    initialize_fixture(&fixture, &root, &mount, &cui, &gui);
    IOS_TEST_ASSERT_STATUS(vfs_remove_directory(&cui, "/"),
                           IOS_ERROR(IOS_E_INVALID_ARGUMENT));
    IOS_TEST_ASSERT_STATUS(vfs_create_directory(&cui, "/DOCS", &object), IOS_OK);
    IOS_TEST_ASSERT_STATUS(vfs_rename(&cui, "/DOCS", "/DOCS/CHILD"),
                           IOS_ERROR(IOS_E_INVALID_ARGUMENT));
    mount.state = IOS_MOUNT_DIAGNOSTIC;
    IOS_TEST_ASSERT_STATUS(vfs_create_directory(&cui, "/READONLY", &object),
                           IOS_ERROR(IOS_E_READ_ONLY));
    IOS_TEST_ASSERT_STATUS(vfs_remove_directory(&cui, "/DOCS"),
                           IOS_ERROR(IOS_E_READ_ONLY));
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_create_and_list_are_coherent_across_contexts),
    IOS_TEST_CASE(test_nonempty_remove_and_rename_preserve_shared_namespace),
    IOS_TEST_CASE(test_rejects_root_mutation_descendant_rename_and_read_only_mount)
};
const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
