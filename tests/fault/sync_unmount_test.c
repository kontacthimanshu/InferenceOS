#include <inferenceos/test.h>

#include <inferenceos/fake_block.h>
#include <inferenceos/fs/sync.h>
#include <inferenceos/vfs.h>

#include <string.h>

enum { SYNC_UNMOUNT_CACHE_ENTRIES = 4 };

struct sync_unmount_fixture {
    struct ios_fault_injector faults;
    struct ios_fake_block backend;
    struct ios_block_device device;
    struct ios_block_cache cache;
    struct ios_block_cache_entry entries[SYNC_UNMOUNT_CACHE_ENTRIES];
    struct ios_fs_sync sync;
    struct ios_vfs_object root;
    struct ios_vfs_mount mount;
    struct ios_vfs_mount_registry registry;
};

static ios_status backend_read(void *context, ios_u64 sector, ios_size count, void *buffer)
{
    return fake_block_read(context, sector, count, buffer);
}

static ios_status backend_write(
    void *context, ios_u64 sector, ios_size count, const void *buffer)
{
    return fake_block_write(context, sector, count, buffer);
}

static ios_status backend_flush(void *context)
{
    return fake_block_flush(context);
}

static void fixture_initialize(struct sync_unmount_fixture *fixture)
{
    const struct ios_block_device_operations operations = {
        backend_read, backend_write, backend_flush
    };

    memset(fixture, 0, sizeof(*fixture));
    fault_injector_initialize(&fixture->faults);
    IOS_TEST_ASSERT_STATUS(
        fake_block_initialize(&fixture->backend, 128, 8, &fixture->faults), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        block_device_initialize(
            &fixture->device, &fixture->backend, &operations, IOS_BLOCK_SECTOR_SIZE,
            fixture->backend.sector_count, IOS_BLOCK_DEVICE_READY),
        IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        block_cache_initialize(
            &fixture->cache, &fixture->device, fixture->entries,
            SYNC_UNMOUNT_CACHE_ENTRIES),
        IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_fs_sync_initialize(&fixture->sync, &fixture->cache), IOS_OK);
    fixture->root.identity = IOS_VFS_ROOT_OBJECT_ID;
    fixture->root.kind = IOS_VFS_OBJECT_DIRECTORY;
    fixture->mount.driver_name = "InferenceOS-FS";
    fixture->mount.driver_context = &fixture->sync;
    fixture->mount.device = &fixture->device;
    fixture->mount.root = &fixture->root;
    fixture->mount.state = IOS_MOUNT_RW;
    vfs_mount_registry_initialize(&fixture->registry);
    IOS_TEST_ASSERT_STATUS(
        vfs_mount_root(&fixture->registry, &fixture->mount, "/"), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        vfs_mount_configure_unmount(
            &fixture->mount, &fixture->sync, ios_fs_sync_barrier_operation,
            ios_fs_sync_invalidate_operation),
        IOS_OK);
}

static void fixture_destroy(struct sync_unmount_fixture *fixture)
{
    fake_block_destroy(&fixture->backend);
}

static void assert_failed_sync_preserves_live_mount(
    struct sync_unmount_fixture *fixture, ios_status expected)
{
    IOS_TEST_ASSERT_STATUS(vfs_unmount_root(&fixture->registry), expected);
    IOS_TEST_ASSERT(ios_fs_sync_is_dirty(&fixture->sync));
    IOS_TEST_ASSERT_STATUS(ios_fs_sync_durable_result(&fixture->sync), expected);
    IOS_TEST_ASSERT(fixture->mount.mounted);
    IOS_TEST_ASSERT(fixture->mount.lifecycle == IOS_VFS_MOUNT_ACTIVE);
    IOS_TEST_ASSERT(vfs_root_mount(&fixture->registry) == &fixture->mount);
}

static void test_writeback_failure_blocks_durable_completion_and_unmount(void)
{
    struct sync_unmount_fixture fixture;
    ios_u8 bytes[IOS_BLOCK_SECTOR_SIZE] = { 0x57 };

    fixture_initialize(&fixture);
    IOS_TEST_ASSERT_STATUS(vfs_mount_begin_operation(&fixture.mount, true), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_sync_write_sector(&fixture.sync, 10, bytes, IOS_FS_DIRTY_CONTENT), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        fault_injector_fail_once(
            &fixture.faults, IOS_FAULT_BLOCK_WRITE, 1, IOS_ERROR(IOS_E_IO)),
        IOS_OK);
    IOS_TEST_ASSERT_STATUS(vfs_unmount_root(&fixture.registry), IOS_ERROR(IOS_E_BUSY));
    IOS_TEST_ASSERT_STATUS(
        vfs_mount_begin_operation(&fixture.mount, false), IOS_ERROR(IOS_E_BUSY));
    IOS_TEST_ASSERT_STATUS(vfs_mount_end_operation(&fixture.mount), IOS_OK);
    assert_failed_sync_preserves_live_mount(&fixture, IOS_ERROR(IOS_E_IO));
    IOS_TEST_ASSERT(fixture.entries[0].state == IOS_BLOCK_CACHE_ERROR);
    IOS_TEST_ASSERT_STATUS(vfs_unmount_root(&fixture.registry), IOS_OK);
    IOS_TEST_ASSERT(fixture.sync.cache == NULL);
    IOS_TEST_ASSERT(fixture.entries[0].state == IOS_BLOCK_CACHE_EMPTY);
    fixture_destroy(&fixture);
}

static void test_flush_failure_blocks_durable_completion_and_unmount(void)
{
    struct sync_unmount_fixture fixture;
    ios_u8 bytes[IOS_BLOCK_SECTOR_SIZE] = { 0x46 };

    fixture_initialize(&fixture);
    IOS_TEST_ASSERT_STATUS(vfs_mount_begin_operation(&fixture.mount, true), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_sync_write_sector(
            &fixture.sync, 11, bytes,
            (enum ios_fs_dirty_component)(IOS_FS_DIRTY_PRIMARY | IOS_FS_DIRTY_COMPANION)),
        IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        fault_injector_fail_once(
            &fixture.faults, IOS_FAULT_BLOCK_FLUSH, 1, IOS_ERROR(IOS_E_IO)),
        IOS_OK);
    IOS_TEST_ASSERT_STATUS(vfs_unmount_root(&fixture.registry), IOS_ERROR(IOS_E_BUSY));
    IOS_TEST_ASSERT_STATUS(vfs_mount_end_operation(&fixture.mount), IOS_OK);
    assert_failed_sync_preserves_live_mount(&fixture, IOS_ERROR(IOS_E_IO));
    IOS_TEST_ASSERT(fixture.backend.write_operations == 1);
    IOS_TEST_ASSERT(fixture.backend.durable_generation == 0);
    IOS_TEST_ASSERT_STATUS(vfs_unmount_root(&fixture.registry), IOS_OK);
    IOS_TEST_ASSERT(fixture.backend.durable_generation == 1);
    IOS_TEST_ASSERT(fixture.sync.cache == NULL);
    fixture_destroy(&fixture);
}

static void test_immediate_write_refusal_is_not_reported_as_durable(void)
{
    struct sync_unmount_fixture fixture;
    ios_u8 bytes[IOS_BLOCK_SECTOR_SIZE] = { 0x52 };

    fixture_initialize(&fixture);
    fixture.device.status = IOS_BLOCK_DEVICE_READ_ONLY;
    IOS_TEST_ASSERT_STATUS(
        ios_fs_sync_write_sector(&fixture.sync, 12, bytes, IOS_FS_DIRTY_ALLOCATION),
        IOS_ERROR(IOS_E_READ_ONLY));
    IOS_TEST_ASSERT(!ios_fs_sync_is_dirty(&fixture.sync));
    IOS_TEST_ASSERT_STATUS(
        ios_fs_sync_durable_result(&fixture.sync), IOS_ERROR(IOS_E_READ_ONLY));
    IOS_TEST_ASSERT(fixture.backend.write_operations == 0);
    fixture.device.status = IOS_BLOCK_DEVICE_READY;
    IOS_TEST_ASSERT_STATUS(vfs_unmount_root(&fixture.registry), IOS_OK);
    IOS_TEST_ASSERT(fixture.sync.cache == NULL);
    fixture_destroy(&fixture);
}

static void test_busy_unmount_preserves_registry_until_all_operations_finish(void)
{
    struct sync_unmount_fixture fixture;

    fixture_initialize(&fixture);
    IOS_TEST_ASSERT_STATUS(vfs_mount_begin_operation(&fixture.mount, false), IOS_OK);
    IOS_TEST_ASSERT_STATUS(vfs_mount_begin_operation(&fixture.mount, true), IOS_OK);
    IOS_TEST_ASSERT_STATUS(vfs_unmount_root(&fixture.registry), IOS_ERROR(IOS_E_BUSY));
    IOS_TEST_ASSERT(fixture.mount.active_operations == 2);
    IOS_TEST_ASSERT(fixture.mount.lifecycle == IOS_VFS_MOUNT_DRAINING);
    IOS_TEST_ASSERT(fixture.root.reference_count == 2);
    IOS_TEST_ASSERT(fixture.registry.count == 1);
    IOS_TEST_ASSERT_STATUS(
        vfs_mount_begin_operation(&fixture.mount, false), IOS_ERROR(IOS_E_BUSY));
    IOS_TEST_ASSERT_STATUS(vfs_mount_end_operation(&fixture.mount), IOS_OK);
    IOS_TEST_ASSERT_STATUS(vfs_unmount_root(&fixture.registry), IOS_ERROR(IOS_E_BUSY));
    IOS_TEST_ASSERT_STATUS(vfs_mount_end_operation(&fixture.mount), IOS_OK);
    IOS_TEST_ASSERT_STATUS(vfs_unmount_root(&fixture.registry), IOS_OK);
    IOS_TEST_ASSERT(!fixture.mount.mounted && vfs_root_mount(&fixture.registry) == NULL);
    IOS_TEST_ASSERT(fixture.mount.lifecycle == IOS_VFS_MOUNT_DETACHED);
    IOS_TEST_ASSERT(fixture.sync.cache == NULL);
    IOS_TEST_ASSERT_STATUS(
        vfs_mount_begin_operation(&fixture.mount, false), IOS_ERROR(IOS_E_INVALID_STATE));
    fixture_destroy(&fixture);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_writeback_failure_blocks_durable_completion_and_unmount),
    IOS_TEST_CASE(test_flush_failure_blocks_durable_completion_and_unmount),
    IOS_TEST_CASE(test_immediate_write_refusal_is_not_reported_as_durable),
    IOS_TEST_CASE(test_busy_unmount_preserves_registry_until_all_operations_finish)
};

const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
