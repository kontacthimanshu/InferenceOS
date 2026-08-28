#include <inferenceos/test.h>

#include <inferenceos/extension_search.h>
#include <inferenceos/block.h>
#include <inferenceos/runtime.h>

#include <string.h>

struct search_source {
    const char *observed_extension;
    ios_size observed_length;
    bool malformed;
};

static ios_status provide_search_results(
    void *context,
    const char *extension,
    ios_size extension_length,
    struct ios_vfs_search_result *entries,
    ios_size capacity,
    ios_size *entry_count,
    bool *truncated
)
{
    struct search_source *source = context;
    source->observed_extension = extension;
    source->observed_length = extension_length;
    if (capacity < 2) return IOS_ERROR(IOS_E_NO_SPACE);
    entries[0].object_identity = 10;
    strcpy(entries[0].display_path, source->malformed ? "/REPORT.DOC" : "/REPORT");
    entries[0].display_path_length = strlen(entries[0].display_path);
    entries[1].object_identity = 11;
    strcpy(entries[1].display_path, "/WORK/ARCHIVE");
    entries[1].display_path_length = strlen(entries[1].display_path);
    *entry_count = 2;
    *truncated = false;
    return IOS_OK;
}

static void initialize_mount(
    struct ios_vfs_mount_registry *registry,
    struct ios_vfs_mount *mount,
    struct ios_vfs_object *root,
    struct ios_block_device *device,
    struct search_source *source
)
{
    vfs_mount_registry_initialize(registry);
    memset(mount, 0, sizeof(*mount));
    *root = (struct ios_vfs_object){
        IOS_VFS_ROOT_OBJECT_ID, IOS_VFS_OBJECT_DIRECTORY, 0
    };
    mount->driver_name = "test";
    mount->driver_context = source;
    mount->device = device;
    mount->root = root;
    mount->state = IOS_MOUNT_RW;
    mount->search_extension = provide_search_results;
    IOS_TEST_ASSERT_STATUS(vfs_mount_root(registry, mount, "/"), IOS_OK);
}

static struct ios_extension_search_request request_for(const char *extension)
{
    struct ios_extension_search_request request = {
        .size = sizeof(request),
        .version = IOS_EXTENSION_SEARCH_ABI_VERSION,
        .extension_length = (ios_u16)strlen(extension)
    };
    memcpy(request.extension, extension, request.extension_length);
    return request;
}

static bool contains_text(const void *bytes, ios_size length, const char *text)
{
    const ios_u8 *input = bytes;
    const ios_size text_length = strlen(text);
    if (text_length == 0 || text_length > length) return false;
    for (ios_size index = 0; index <= length - text_length; ++index) {
        if (memcmp(input + index, text, text_length) == 0) return true;
    }
    return false;
}

static bool all_zero(const void *bytes, ios_size length)
{
    const ios_u8 *input = bytes;
    for (ios_size index = 0; index < length; ++index) {
        if (input[index] != 0) return false;
    }
    return true;
}

static void test_service_returns_only_bounded_display_safe_locations(void)
{
    struct ios_vfs_mount_registry registry;
    struct ios_vfs_mount mount;
    struct ios_vfs_object root;
    struct ios_block_device device = { 0 };
    struct search_source source = { 0 };
    struct ios_extension_search_service service;
    struct ios_process caller = { .process_id = 7, .application_identity = 9 };
    struct ios_extension_search_request request = request_for(".DoC");
    struct ios_extension_search_reply reply;

    initialize_mount(&registry, &mount, &root, &device, &source);
    IOS_TEST_ASSERT_STATUS(
        ios_extension_search_service_initialize(&service, &registry), IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(
        ios_extension_search_dispatch(&service, &caller, &request, &reply), IOS_OK
    );
    IOS_TEST_ASSERT(source.observed_length == 3);
    IOS_TEST_ASSERT(memcmp(source.observed_extension, "DOC", 3) == 0);
    IOS_TEST_ASSERT(reply.size == sizeof(reply));
    IOS_TEST_ASSERT(reply.version == IOS_EXTENSION_SEARCH_ABI_VERSION);
    IOS_TEST_ASSERT(reply.status == IOS_OK && reply.item_count == 2);
    IOS_TEST_ASSERT(strcmp(reply.entries[0].location, "/REPORT") == 0);
    IOS_TEST_ASSERT(strcmp(reply.entries[1].location, "/WORK/ARCHIVE") == 0);
    IOS_TEST_ASSERT(!contains_text(&reply, sizeof(reply), ".DOC"));
    IOS_TEST_ASSERT(!contains_text(&reply, sizeof(reply), "E771F04F"));
    IOS_TEST_ASSERT(reply.entries[2].object_identity == 0);
    IOS_TEST_ASSERT(reply.entries[2].location[0] == '\0');
}

static void test_service_rejects_invalid_requests_and_clears_reply(void)
{
    struct ios_vfs_mount_registry registry;
    struct ios_vfs_mount mount;
    struct ios_vfs_object root;
    struct ios_block_device device = { 0 };
    struct search_source source = { 0 };
    struct ios_extension_search_service service;
    struct ios_process caller = { .process_id = 7, .application_identity = 9 };
    struct ios_extension_search_request request = request_for("DOC");
    struct ios_extension_search_reply reply;

    initialize_mount(&registry, &mount, &root, &device, &source);
    IOS_TEST_ASSERT_STATUS(
        ios_extension_search_service_initialize(&service, &registry), IOS_OK
    );
    memset(&reply, 0xa5, sizeof(reply));
    request.flags = 1;
    IOS_TEST_ASSERT_STATUS(
        ios_extension_search_dispatch(&service, &caller, &request, &reply),
        IOS_ERROR(IOS_E_INVALID_ARGUMENT)
    );
    IOS_TEST_ASSERT(all_zero(&reply, sizeof(reply)));

    request = request_for("D.OC");
    memset(&reply, 0xa5, sizeof(reply));
    IOS_TEST_ASSERT_STATUS(
        ios_extension_search_dispatch(&service, &caller, &request, &reply),
        IOS_ERROR(IOS_E_INVALID_ARGUMENT)
    );
    IOS_TEST_ASSERT(all_zero(&reply, sizeof(reply)));

    request = request_for("DOC");
    request.extension[3] = 'X';
    memset(&reply, 0xa5, sizeof(reply));
    IOS_TEST_ASSERT_STATUS(
        ios_extension_search_dispatch(&service, &caller, &request, &reply),
        IOS_ERROR(IOS_E_INVALID_ARGUMENT)
    );
    IOS_TEST_ASSERT(all_zero(&reply, sizeof(reply)));
}

static void test_service_rejects_malformed_driver_results_without_leaking_them(void)
{
    struct ios_vfs_mount_registry registry;
    struct ios_vfs_mount mount;
    struct ios_vfs_object root;
    struct ios_block_device device = { 0 };
    struct search_source source = { .malformed = true };
    struct ios_extension_search_service service;
    struct ios_process caller = { .process_id = 7, .application_identity = 9 };
    struct ios_extension_search_request request = request_for("DOC");
    struct ios_extension_search_reply reply;

    initialize_mount(&registry, &mount, &root, &device, &source);
    IOS_TEST_ASSERT_STATUS(
        ios_extension_search_service_initialize(&service, &registry), IOS_OK
    );
    memset(&reply, 0xa5, sizeof(reply));
    IOS_TEST_ASSERT_STATUS(
        ios_extension_search_dispatch(&service, &caller, &request, &reply),
        IOS_ERROR(IOS_E_PROTOCOL)
    );
    IOS_TEST_ASSERT(all_zero(&reply, sizeof(reply)));
}

static void test_service_reports_unavailable_root_without_results(void)
{
    struct ios_vfs_mount_registry registry;
    struct ios_extension_search_service service;
    struct ios_process caller = { .process_id = 7, .application_identity = 9 };
    struct ios_extension_search_request request = request_for("DOC");
    struct ios_extension_search_reply reply;

    vfs_mount_registry_initialize(&registry);
    IOS_TEST_ASSERT_STATUS(
        ios_extension_search_service_initialize(&service, &registry), IOS_OK
    );
    memset(&reply, 0xa5, sizeof(reply));
    IOS_TEST_ASSERT_STATUS(
        ios_extension_search_dispatch(&service, &caller, &request, &reply),
        IOS_ERROR(IOS_E_NOT_FOUND)
    );
    IOS_TEST_ASSERT(all_zero(&reply, sizeof(reply)));
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_service_returns_only_bounded_display_safe_locations),
    IOS_TEST_CASE(test_service_rejects_invalid_requests_and_clears_reply),
    IOS_TEST_CASE(test_service_rejects_malformed_driver_results_without_leaking_them),
    IOS_TEST_CASE(test_service_reports_unavailable_root_without_results)
};
const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
