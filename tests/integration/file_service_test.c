#include <inferenceos/test.h>

#include <inferenceos/block.h>
#include <inferenceos/fake_block.h>
#include <inferenceos/fs/file_service.h>
#include <inferenceos/runtime.h>

enum {
    TEST_SECTORS = 1024,
    TEST_FAT_ENTRIES = 128,
    TEST_CACHE_ENTRIES = 64
};

struct fixture {
    struct ios_fake_block backing;
    struct ios_block_device device;
    struct ios_block_cache cache;
    struct ios_block_cache_entry cache_entries[TEST_CACHE_ENTRIES];
    struct ios_fs_sync sync;
    struct ios_fs_mount mount;
    struct ios_fs_file_service service;
    struct ios_vfs_mount_registry mounts;
    struct ios_vfs_path_context path;
    ios_u32 fat[TEST_FAT_ENTRIES];
};

static ios_status device_read(
    void *context, ios_u64 sector, ios_size count, void *buffer
)
{
    return fake_block_read(context, sector, count, buffer);
}

static ios_status device_write(
    void *context, ios_u64 sector, ios_size count, const void *buffer
)
{
    return fake_block_write(context, sector, count, buffer);
}

static ios_status device_flush(void *context)
{
    return fake_block_flush(context);
}

static void write_u32(ios_u8 *bytes, ios_u32 value)
{
    for (ios_size index = 0; index < 4; ++index) {
        bytes[index] = (ios_u8)(value >> (index * 8));
    }
}

static void initialize_fixture(struct fixture *fixture)
{
    static const struct ios_block_device_operations operations = {
        device_read, device_write, device_flush
    };
    ios_u8 fat_sector[IOS_FS_SECTOR_SIZE] = { 0 };

    memset(fixture, 0, sizeof(*fixture));
    IOS_TEST_ASSERT_STATUS(
        fake_block_initialize(&fixture->backing, TEST_SECTORS, 256, NULL), IOS_OK
    );
    write_u32(fat_sector, IOS_FS_FAT_END_OF_CHAIN);
    write_u32(fat_sector + 4, IOS_FS_FAT_END_OF_CHAIN);
    write_u32(fat_sector + 8, IOS_FS_FAT_END_OF_CHAIN);
    IOS_TEST_ASSERT_STATUS(fake_block_write(&fixture->backing, 2, 1, fat_sector), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        block_device_initialize(
            &fixture->device, &fixture->backing, &operations,
            IOS_FS_SECTOR_SIZE, TEST_SECTORS, IOS_BLOCK_DEVICE_READY
        ), IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(
        block_cache_initialize(
            &fixture->cache, &fixture->device,
            fixture->cache_entries, IOS_ARRAY_COUNT(fixture->cache_entries)
        ), IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(ios_fs_sync_initialize(&fixture->sync, &fixture->cache), IOS_OK);
    fixture->mount.geometry = (struct ios_fs_geometry){
        .total_sectors = TEST_SECTORS,
        .fat_sectors = 1,
        .registry_start_sector = 3,
        .data_start_sector = 11,
        .cluster_count = TEST_FAT_ENTRIES - 2,
        .usable_bytes = (TEST_FAT_ENTRIES - 2) * 4096U
    };
    fixture->mount.vfs.state = IOS_MOUNT_RW;
    fixture->mount.vfs.driver_name = "InferenceOS-FS";
    fixture->mount.vfs.driver_context = &fixture->mount;
    fixture->mount.vfs.device = &fixture->device;
    fixture->mount.vfs.root = &fixture->mount.root;
    fixture->mount.root.identity = IOS_VFS_ROOT_OBJECT_ID;
    fixture->mount.root.kind = IOS_VFS_OBJECT_DIRECTORY;
    vfs_mount_registry_initialize(&fixture->mounts);
    IOS_TEST_ASSERT_STATUS(
        vfs_mount_root(&fixture->mounts, &fixture->mount.vfs, "/"), IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(
        ios_fs_file_service_initialize(
            &fixture->service, &fixture->mount, &fixture->sync,
            fixture->fat, sizeof(fixture->fat)
        ), IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(
        vfs_path_context_initialize(&fixture->path, &fixture->mount.vfs), IOS_OK
    );
}

static void destroy_fixture(struct fixture *fixture)
{
    fake_block_destroy(&fixture->backing);
}

static void test_file_lifecycle_is_durable_and_preserves_companion_metadata(void)
{
    struct fixture fixture;
    ios_u8 bytes[32] = { 0 };
    ios_size transferred;
    bool complete;
    struct ios_fs_file_service reopened;
    ios_u32 reopened_fat[TEST_FAT_ENTRIES];
    struct ios_block_cache reopened_cache;
    struct ios_block_cache_entry reopened_entries[TEST_CACHE_ENTRIES];
    struct ios_fs_sync reopened_sync;

    initialize_fixture(&fixture);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_file_service_create(&fixture.service, "REPORT.TXT"), IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(
        ios_fs_file_service_create(&fixture.service, "report.txt"),
        IOS_ERROR(IOS_E_ALREADY_EXISTS)
    );
    IOS_TEST_ASSERT_STATUS(
        ios_fs_file_service_replace(&fixture.service, "REPORT.TXT", "hello", 5), IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(
        ios_fs_file_service_append(&fixture.service, "REPORT.TXT", " world", 6), IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(
        ios_fs_file_service_read(
            &fixture.service, "REPORT.TXT", 0, bytes, sizeof(bytes),
            &transferred, &complete
        ), IOS_OK
    );
    IOS_TEST_ASSERT(complete && transferred == 11 && memcmp(bytes, "hello world", 11) == 0);

    IOS_TEST_ASSERT_STATUS(
        block_cache_initialize(
            &reopened_cache, &fixture.device,
            reopened_entries, IOS_ARRAY_COUNT(reopened_entries)
        ), IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(ios_fs_sync_initialize(&reopened_sync, &reopened_cache), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_file_service_initialize(
            &reopened, &fixture.mount, &reopened_sync,
            reopened_fat, sizeof(reopened_fat)
        ), IOS_OK
    );
    memset(bytes, 0, sizeof(bytes));
    IOS_TEST_ASSERT_STATUS(
        ios_fs_file_service_read(
            &reopened, "REPORT.TXT", 0, bytes, sizeof(bytes),
            &transferred, &complete
        ), IOS_OK
    );
    IOS_TEST_ASSERT(complete && transferred == 11 && memcmp(bytes, "hello world", 11) == 0);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_file_service_rename(&reopened, "REPORT.TXT", "SUMMARY.LOG"), IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(
        ios_fs_file_service_read(
            &reopened, "REPORT.TXT", 0, bytes, sizeof(bytes),
            &transferred, &complete
        ), IOS_ERROR(IOS_E_NOT_FOUND)
    );
    IOS_TEST_ASSERT_STATUS(
        ios_fs_file_service_remove(&reopened, "SUMMARY.LOG"), IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(
        ios_fs_file_service_read(
            &reopened, "SUMMARY.LOG", 0, bytes, sizeof(bytes),
            &transferred, &complete
        ), IOS_ERROR(IOS_E_NOT_FOUND)
    );
    destroy_fixture(&fixture);
}

static void test_file_service_rejects_bad_names_and_read_only_mutation(void)
{
    struct fixture fixture;
    initialize_fixture(&fixture);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_file_service_create(&fixture.service, "TOO-LONG-NAME.TXT"),
        IOS_ERROR(IOS_E_INVALID_ARGUMENT)
    );
    fixture.mount.vfs.state = IOS_MOUNT_DIAGNOSTIC;
    IOS_TEST_ASSERT_STATUS(
        ios_fs_file_service_create(&fixture.service, "LOCKED.TXT"),
        IOS_ERROR(IOS_E_READ_ONLY)
    );
    destroy_fixture(&fixture);
}

static void test_directory_vfs_and_nested_file_commands_use_real_disk_records(void)
{
    struct fixture fixture;
    struct ios_vfs_object object;
    struct ios_vfs_directory_entry entries[4];
    struct ios_fs_primary_disk primary;
    struct ios_fs_companion_disk companion;
    ios_u8 bytes[16] = { 0 };
    ios_u64 primary_location;
    ios_u64 companion_location;
    ios_u64 continuation;
    ios_size entry_count;
    ios_size transferred;
    bool has_companion;
    bool complete;

    initialize_fixture(&fixture);
    IOS_TEST_ASSERT_STATUS(
        vfs_create_directory(&fixture.path, "/DOCS", &object), IOS_OK
    );
    IOS_TEST_ASSERT(object.kind == IOS_VFS_OBJECT_DIRECTORY && object.identity > 2);
    IOS_TEST_ASSERT_STATUS(vfs_path_set_current(&fixture.path, "/DOCS"), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_file_service_create(&fixture.service, "/DOCS/NOTE.TXT"), IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(
        ios_fs_file_service_replace(
            &fixture.service, "/DOCS/NOTE.TXT", "nested", 6
        ), IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(
        vfs_list_directory(
            &fixture.path, ".", 0, entries, IOS_ARRAY_COUNT(entries),
            &entry_count, &continuation
        ), IOS_OK
    );
    IOS_TEST_ASSERT(entry_count == 1 && continuation == 0);
    IOS_TEST_ASSERT(strcmp(entries[0].display_base_name, "NOTE") == 0);
    IOS_TEST_ASSERT(entries[0].kind == IOS_VFS_OBJECT_REGULAR_FILE);
    IOS_TEST_ASSERT(entries[0].internal_type_identity == UINT64_C(0x545854));
    IOS_TEST_ASSERT(entries[0].type_prefilter == UINT64_C(0xe771f04f));
    IOS_TEST_ASSERT_STATUS(
        ios_fs_file_service_get_record(
            &fixture.service, entries[0].object_identity,
            &primary, &companion, &primary_location, &companion_location,
            &has_companion
        ), IOS_OK
    );
    IOS_TEST_ASSERT(has_companion && primary_location == companion_location + 32U);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_file_service_rename(
            &fixture.service, "/DOCS/NOTE.TXT", "/RENAMED.TXT"
        ), IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(
        ios_fs_file_service_read(
            &fixture.service, "/RENAMED.TXT", 0, bytes, sizeof(bytes),
            &transferred, &complete
        ), IOS_OK
    );
    IOS_TEST_ASSERT(complete && transferred == 6 && memcmp(bytes, "nested", 6) == 0);
    IOS_TEST_ASSERT_STATUS(vfs_remove_directory(&fixture.path, "/DOCS"), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_file_service_remove(&fixture.service, "/RENAMED.TXT"), IOS_OK
    );
    destroy_fixture(&fixture);
}

static void test_extension_search_returns_nested_extension_hidden_paths(void)
{
    struct fixture fixture;
    struct ios_vfs_object object;
    struct ios_vfs_search_result results[8];
    ios_size count;
    bool truncated;

    initialize_fixture(&fixture);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_file_service_create(&fixture.service, "/ROOT.DOC"), IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(vfs_create_directory(&fixture.path, "/WORK", &object), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_file_service_create(&fixture.service, "/WORK/REPORT.DOC"), IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(
        vfs_create_directory(&fixture.path, "/WORK/OLD", &object), IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(
        ios_fs_file_service_create(&fixture.service, "/WORK/OLD/ARCHIVE.DOC"), IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(
        ios_fs_file_service_create(&fixture.service, "/WORK/NOTES.TXT"), IOS_OK
    );

    IOS_TEST_ASSERT_STATUS(vfs_search_extension(
        &fixture.mount.vfs, ".DoC", 4, results, IOS_ARRAY_COUNT(results),
        &count, &truncated
    ), IOS_OK);
    IOS_TEST_ASSERT(!truncated && count == 3);
    IOS_TEST_ASSERT(strcmp(results[0].display_path, "/ROOT") == 0);
    IOS_TEST_ASSERT(strcmp(results[1].display_path, "/WORK/REPORT") == 0);
    IOS_TEST_ASSERT(strcmp(results[2].display_path, "/WORK/OLD/ARCHIVE") == 0);
    for (ios_size index = 0; index < count; ++index) {
        IOS_TEST_ASSERT(strchr(results[index].display_path, '.') == NULL);
    }

    IOS_TEST_ASSERT_STATUS(vfs_search_extension(
        &fixture.mount.vfs, "ZIP", 3, results, IOS_ARRAY_COUNT(results),
        &count, &truncated
    ), IOS_OK);
    IOS_TEST_ASSERT(!truncated && count == 0);
    destroy_fixture(&fixture);
}

static void test_extension_search_reports_capacity_truncation(void)
{
    struct fixture fixture;
    struct ios_vfs_search_result results[2];
    ios_size count;
    bool truncated;

    initialize_fixture(&fixture);
    IOS_TEST_ASSERT_STATUS(ios_fs_file_service_create(&fixture.service, "/ONE.DOC"), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_fs_file_service_create(&fixture.service, "/TWO.DOC"), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_fs_file_service_create(&fixture.service, "/THREE.DOC"), IOS_OK);
    IOS_TEST_ASSERT_STATUS(vfs_search_extension(
        &fixture.mount.vfs, "doc", 3, results, IOS_ARRAY_COUNT(results),
        &count, &truncated
    ), IOS_OK);
    IOS_TEST_ASSERT(truncated && count == IOS_ARRAY_COUNT(results));
    IOS_TEST_ASSERT(strcmp(results[0].display_path, "/ONE") == 0);
    IOS_TEST_ASSERT(strcmp(results[1].display_path, "/TWO") == 0);
    destroy_fixture(&fixture);
}

static void test_extension_matcher_rejects_prefilter_collision_and_damage(void)
{
    static const ios_u8 doc[IOS_FS_EXTENSION_SIZE] = { 'D', 'O', 'C' };
    struct ios_fs_primary primary = {
        { 'N', 'O', 'T', 'E', 'S', ' ', ' ', ' ', 'T', 'X', 'T' },
        IOS_FS_ATTRIBUTE_REGULAR, 0, 0
    };
    struct ios_fs_primary_disk primary_disk;
    struct ios_fs_companion_disk companion_disk;
    bool matches;

    IOS_TEST_ASSERT_STATUS(ios_fs_primary_encode(&primary, &primary_disk), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_companion_encode(primary.name, true, &companion_disk), IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(ios_fs_record_pair_matches_extension(
        &primary_disk, &companion_disk, doc, sizeof(doc),
        companion_disk.extension_hash_text, &matches
    ), IOS_OK);
    IOS_TEST_ASSERT(!matches);

    companion_disk.crc32[0] ^= 1U;
    IOS_TEST_ASSERT_STATUS(ios_fs_record_pair_matches_extension(
        &primary_disk, &companion_disk, doc, sizeof(doc),
        companion_disk.extension_hash_text, &matches
    ), IOS_ERROR(IOS_E_CORRUPT));
}

static void test_extension_search_rejects_excessive_directory_depth(void)
{
    struct fixture fixture;
    struct ios_vfs_object object;
    struct ios_vfs_search_result result;
    ios_u64 parent = IOS_VFS_ROOT_OBJECT_ID;
    ios_size count;
    bool truncated;

    initialize_fixture(&fixture);
    for (ios_size depth = 0; depth <= IOS_VFS_MAX_DIRECTORY_LEVELS; ++depth) {
        IOS_TEST_ASSERT_STATUS(
            ios_fs_file_service_create_directory(
                &fixture.service, parent, "A", 1, &object
            ), IOS_OK
        );
        parent = object.identity;
    }
    IOS_TEST_ASSERT_STATUS(vfs_search_extension(
        &fixture.mount.vfs, "DOC", 3, &result, 1, &count, &truncated
    ), IOS_ERROR(IOS_E_CORRUPT));
    IOS_TEST_ASSERT(count == 0 && !truncated);
    destroy_fixture(&fixture);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_file_lifecycle_is_durable_and_preserves_companion_metadata),
    IOS_TEST_CASE(test_file_service_rejects_bad_names_and_read_only_mutation),
    IOS_TEST_CASE(test_directory_vfs_and_nested_file_commands_use_real_disk_records),
    IOS_TEST_CASE(test_extension_search_returns_nested_extension_hidden_paths),
    IOS_TEST_CASE(test_extension_search_reports_capacity_truncation),
    IOS_TEST_CASE(test_extension_matcher_rejects_prefilter_collision_and_damage),
    IOS_TEST_CASE(test_extension_search_rejects_excessive_directory_depth)
};

const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
