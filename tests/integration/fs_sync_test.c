#include <inferenceos/test.h>

#include <inferenceos/fake_block.h>
#include <inferenceos/fs/sync.h>

#include <string.h>

enum { SYNC_CACHE_ENTRIES = 8 };

static ios_status backend_read(void *context, ios_u64 sector, ios_size count, void *buffer)
{
    return fake_block_read(context, sector, count, buffer);
}
static ios_status backend_write(
    void *context, ios_u64 sector, ios_size count, const void *buffer)
{
    return fake_block_write(context, sector, count, buffer);
}
static ios_status backend_flush(void *context) { return fake_block_flush(context); }

static void initialize_sync(
    struct ios_fault_injector *faults, struct ios_fake_block *backend,
    struct ios_block_device *device, struct ios_block_cache *cache,
    struct ios_block_cache_entry entries[SYNC_CACHE_ENTRIES], struct ios_fs_sync *sync)
{
    const struct ios_block_device_operations operations = {
        backend_read, backend_write, backend_flush
    };
    fault_injector_initialize(faults);
    IOS_TEST_ASSERT_STATUS(fake_block_initialize(backend, 128, 16, faults), IOS_OK);
    IOS_TEST_ASSERT_STATUS(block_device_initialize(
        device, backend, &operations, IOS_BLOCK_SECTOR_SIZE, backend->sector_count,
        IOS_BLOCK_DEVICE_READY), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        block_cache_initialize(cache, device, entries, SYNC_CACHE_ENTRIES), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_fs_sync_initialize(sync, cache), IOS_OK);
}

static void test_sync_tracks_components_and_advances_only_after_durable_success(void)
{
    struct ios_fault_injector faults;
    struct ios_fake_block backend;
    struct ios_block_device device;
    struct ios_block_cache cache;
    struct ios_block_cache_entry entries[SYNC_CACHE_ENTRIES];
    struct ios_fs_sync sync;
    ios_u8 content[IOS_BLOCK_SECTOR_SIZE];
    ios_u8 metadata[IOS_BLOCK_SECTOR_SIZE];
    memset(content, 0x43, sizeof(content));
    memset(metadata, 0x4d, sizeof(metadata));
    initialize_sync(&faults, &backend, &device, &cache, entries, &sync);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_sync_write_sector(&sync, 10, content, IOS_FS_DIRTY_CONTENT), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_fs_sync_write_sector(
        &sync, 11, metadata,
        (enum ios_fs_dirty_component)(IOS_FS_DIRTY_PRIMARY | IOS_FS_DIRTY_COMPANION)), IOS_OK);
    IOS_TEST_ASSERT(ios_fs_sync_is_dirty(&sync));
    IOS_TEST_ASSERT_STATUS(ios_fs_sync_durable_result(&sync), IOS_ERROR(IOS_E_WOULD_BLOCK));
    IOS_TEST_ASSERT_STATUS(ios_fs_sync_all(&sync), IOS_OK);
    IOS_TEST_ASSERT(!ios_fs_sync_is_dirty(&sync));
    IOS_TEST_ASSERT_STATUS(ios_fs_sync_durable_result(&sync), IOS_OK);
    IOS_TEST_ASSERT(sync.durable_generation == 1 && sync.generation == 2);
    IOS_TEST_ASSERT(backend.write_operations == 2 && backend.flush_operations == 1);
    IOS_TEST_ASSERT(backend.durable_generation == 1);
    fake_block_destroy(&backend);
}

static void test_write_and_flush_failures_remain_dirty_and_propagate_until_retry(void)
{
    struct ios_fault_injector faults;
    struct ios_fake_block backend;
    struct ios_block_device device;
    struct ios_block_cache cache;
    struct ios_block_cache_entry entries[SYNC_CACHE_ENTRIES];
    struct ios_fs_sync sync;
    ios_u8 bytes[IOS_BLOCK_SECTOR_SIZE] = { 0xa5 };
    initialize_sync(&faults, &backend, &device, &cache, entries, &sync);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_sync_write_sector(&sync, 20, bytes, IOS_FS_DIRTY_ALLOCATION), IOS_OK);
    IOS_TEST_ASSERT_STATUS(fault_injector_fail_once(
        &faults, IOS_FAULT_BLOCK_WRITE, 1, IOS_ERROR(IOS_E_IO)), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_fs_sync_barrier(&sync), IOS_ERROR(IOS_E_IO));
    IOS_TEST_ASSERT(ios_fs_sync_is_dirty(&sync) && sync.generation == 1);
    IOS_TEST_ASSERT_STATUS(ios_fs_sync_durable_result(&sync), IOS_ERROR(IOS_E_IO));
    IOS_TEST_ASSERT_STATUS(fault_injector_fail_once(
        &faults, IOS_FAULT_BLOCK_FLUSH, 1, IOS_ERROR(IOS_E_IO)), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_fs_sync_barrier(&sync), IOS_ERROR(IOS_E_IO));
    IOS_TEST_ASSERT(ios_fs_sync_is_dirty(&sync) && sync.generation == 1);
    IOS_TEST_ASSERT_STATUS(ios_fs_sync_barrier(&sync), IOS_OK);
    IOS_TEST_ASSERT(!ios_fs_sync_is_dirty(&sync) && sync.durable_generation == 1);
    fake_block_destroy(&backend);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_sync_tracks_components_and_advances_only_after_durable_success),
    IOS_TEST_CASE(test_write_and_flush_failures_remain_dirty_and_propagate_until_retry)
};
const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
