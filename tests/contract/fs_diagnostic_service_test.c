#include <inferenceos/test.h>

#include <inferenceos/arch/interrupts.h>
#include <inferenceos/fs_diagnostic.h>

#include <string.h>

enum { TEST_FAT_ENTRIES = 16 };

struct provider_context {
    struct ios_fs_primary_disk primary;
    struct ios_fs_companion_disk companion;
    ios_u32 fat[TEST_FAT_ENTRIES];
    bool corrupt_chain;
    ios_u32 calls;
};

ios_u64 x86_64_interrupt_save_disable(void) { return 0; }
void x86_64_interrupt_restore(ios_u64 flags) { (void)flags; }

static struct ios_fs_primary report_txt(void)
{
    const struct ios_fs_primary primary = {
        { 'R', 'E', 'P', 'O', 'R', 'T', ' ', ' ', 'T', 'X', 'T' },
        IOS_FS_ATTRIBUTE_REGULAR, 2, 4096
    };
    return primary;
}

static ios_status provide_snapshot(
    void *context,
    enum ios_fs_diagnostic_query query,
    ios_u64 object_identity,
    struct ios_fs_diagnostic_source *source
)
{
    struct provider_context *provider = context;
    ++provider->calls;
    if (query != IOS_FS_DIAGNOSTIC_QUERY_FILESYSTEM && object_identity != 42) {
        return IOS_ERROR(IOS_E_NOT_FOUND);
    }
    source->primary = provider->primary;
    source->companion = provider->companion;
    source->primary_record_location = 96;
    source->companion_record_location = 64;
    source->fat = provider->fat;
    source->fat_entry_count = IOS_ARRAY_COUNT(provider->fat);
    source->has_primary = true;
    source->has_companion = true;
    source->free_space_known = true;
    source->free_bytes = 8192;
    source->registry_health = IOS_FS_DIAGNOSTIC_REGISTRY_DISABLED;
    source->registry_active_type_count = 0;
    if (provider->corrupt_chain) provider->fat[2] = 2;
    return IOS_OK;
}

static void initialize_fixture(
    struct ios_process *process,
    struct ios_fs_mount *mount,
    struct provider_context *provider,
    struct ios_fs_diagnostic_service *service,
    struct ios_fs_diagnostic_authority *authority,
    ios_handle *handle
)
{
    static struct ios_vfs_object root = { 2, IOS_VFS_OBJECT_DIRECTORY, 0 };
    const struct ios_fs_primary primary = report_txt();
    memset(process, 0, sizeof(*process));
    memset(mount, 0, sizeof(*mount));
    memset(provider, 0, sizeof(*provider));
    process->process_id = 7;
    process->application_identity = UINT64_C(0x435549);
    process->state = IOS_PROCESS_RUNNABLE;
    IOS_TEST_ASSERT_STATUS(handle_table_initialize(&process->handles, 7), IOS_OK);
    *authority = (struct ios_fs_diagnostic_authority){
        process->process_id, process->application_identity, IOS_FS_DIAGNOSTIC_SCOPE_ALL
    };
    IOS_TEST_ASSERT_STATUS(handle_table_insert(
        &process->handles, authority, IOS_OBJECT_DIAGNOSTIC_CAPABILITY,
        IOS_RIGHT_DIAGNOSTIC, NULL, NULL, handle
    ), IOS_OK);
    mount->vfs.root = &root;
    mount->vfs.state = IOS_MOUNT_RW;
    mount->vfs.lifecycle = IOS_VFS_MOUNT_ACTIVE;
    mount->vfs.mounted = true;
    mount->report.bounds_trusted = true;
    mount->geometry.total_sectors = 1000;
    mount->geometry.usable_bytes = 32768;
    mount->geometry.data_start_sector = 32;
    mount->geometry.fat_sectors = 1;
    mount->geometry.cluster_count = TEST_FAT_ENTRIES - 2;
    IOS_TEST_ASSERT_STATUS(ios_fs_primary_encode(&primary, &provider->primary), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_companion_encode(primary.name, true, &provider->companion), IOS_OK
    );
    provider->fat[0] = IOS_FS_FAT_END_OF_CHAIN;
    provider->fat[1] = IOS_FS_FAT_END_OF_CHAIN;
    provider->fat[2] = IOS_FS_FAT_END_OF_CHAIN;
    IOS_TEST_ASSERT_STATUS(ios_fs_diagnostic_service_initialize(
        service, mount, provide_snapshot, provider
    ), IOS_OK);
}

static struct ios_fs_diagnostic_request request_for(
    enum ios_fs_diagnostic_query query, ios_handle authority
)
{
    const struct ios_fs_diagnostic_request request = {
        sizeof(struct ios_fs_diagnostic_request), IOS_FS_DIAGNOSTIC_ABI_VERSION,
        query, authority, query == IOS_FS_DIAGNOSTIC_QUERY_FILESYSTEM ? 0 : 42,
        IOS_FS_DIAGNOSTIC_CHAIN_CAPACITY, 0
    };
    return request;
}

static void test_authorized_queries_return_correlated_bounded_dtos(void)
{
    struct ios_process process;
    struct ios_fs_mount mount;
    struct provider_context provider;
    struct ios_fs_diagnostic_service service;
    struct ios_fs_diagnostic_authority authority;
    struct ios_fs_diagnostic_reply reply;
    ios_handle handle;
    initialize_fixture(&process, &mount, &provider, &service, &authority, &handle);

    struct ios_fs_diagnostic_request request = request_for(
        IOS_FS_DIAGNOSTIC_QUERY_FILESYSTEM, handle
    );
    IOS_TEST_ASSERT_STATUS(
        ios_fs_diagnostic_dispatch(&service, &process, &request, &reply), IOS_OK
    );
    IOS_TEST_ASSERT(memcmp(reply.value.filesystem.identity, "INFOSFS1", 8) == 0);
    IOS_TEST_ASSERT(reply.value.filesystem.volume_capacity_bytes == 512000);
    IOS_TEST_ASSERT(reply.value.filesystem.free_bytes == 8192);

    request = request_for(IOS_FS_DIAGNOSTIC_QUERY_FILE, handle);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_diagnostic_dispatch(&service, &process, &request, &reply), IOS_OK
    );
    IOS_TEST_ASSERT(reply.value.file.first_cluster == 2);
    IOS_TEST_ASSERT(reply.value.file.primary_record_location == 96);
    IOS_TEST_ASSERT(reply.value.file.companion_record_location == 64);

    request = request_for(IOS_FS_DIAGNOSTIC_QUERY_HASH, handle);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_diagnostic_dispatch(&service, &process, &request, &reply), IOS_OK
    );
    IOS_TEST_ASSERT(reply.value.hash.extension_length == 3);
    IOS_TEST_ASSERT(memcmp(reply.value.hash.extension, "TXT", 3) == 0);
    IOS_TEST_ASSERT(reply.value.hash.crc_valid);
    IOS_TEST_ASSERT(reply.value.hash.association_checksum_valid);
    IOS_TEST_ASSERT(reply.value.hash.validation_status == IOS_OK);
    IOS_TEST_ASSERT(memcmp(
        reply.value.hash.stored_hash, reply.value.hash.recomputed_hash,
        IOS_FS_HASH_TEXT_SIZE
    ) == 0);

    request = request_for(IOS_FS_DIAGNOSTIC_QUERY_FAT, handle);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_diagnostic_dispatch(&service, &process, &request, &reply), IOS_OK
    );
    IOS_TEST_ASSERT(reply.value.fat.cluster_count == 1);
    IOS_TEST_ASSERT(reply.value.fat.clusters[0] == 2);
    IOS_TEST_ASSERT(reply.value.fat.end_of_chain);
    IOS_TEST_ASSERT(provider.calls == 4);
    IOS_TEST_ASSERT(mount.vfs.active_operations == 0);
}

static void test_scope_and_chain_failures_return_no_diagnostic_payload(void)
{
    struct ios_process process;
    struct ios_fs_mount mount;
    struct provider_context provider;
    struct ios_fs_diagnostic_service service;
    struct ios_fs_diagnostic_authority authority;
    struct ios_fs_diagnostic_reply reply;
    struct ios_fs_diagnostic_reply zero;
    ios_handle handle;
    initialize_fixture(&process, &mount, &provider, &service, &authority, &handle);
    memset(&zero, 0, sizeof(zero));
    authority.scope = IOS_FS_DIAGNOSTIC_SCOPE_FILESYSTEM;
    struct ios_fs_diagnostic_request request = request_for(IOS_FS_DIAGNOSTIC_QUERY_HASH, handle);
    memset(&reply, 0xa5, sizeof(reply));
    IOS_TEST_ASSERT_STATUS(
        ios_fs_diagnostic_dispatch(&service, &process, &request, &reply),
        IOS_ERROR(IOS_E_ACCESS_DENIED)
    );
    IOS_TEST_ASSERT(provider.calls == 0);

    authority.scope = IOS_FS_DIAGNOSTIC_SCOPE_ALL;
    provider.corrupt_chain = true;
    request = request_for(IOS_FS_DIAGNOSTIC_QUERY_FAT, handle);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_diagnostic_dispatch(&service, &process, &request, &reply),
        IOS_ERROR(IOS_E_CORRUPT)
    );
    IOS_TEST_ASSERT(memcmp(&reply, &zero, sizeof(reply)) == 0);
    IOS_TEST_ASSERT(mount.vfs.active_operations == 0);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_authorized_queries_return_correlated_bounded_dtos),
    IOS_TEST_CASE(test_scope_and_chain_failures_return_no_diagnostic_payload)
};
const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
