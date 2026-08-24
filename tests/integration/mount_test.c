#include <inferenceos/test.h>

#include <inferenceos/block.h>
#include <inferenceos/fake_block.h>
#include <inferenceos/fs/mount.h>

#include <string.h>

static ios_status backend_read(
    void *context, ios_u64 first_sector, ios_size sector_count, void *buffer)
{
    return fake_block_read(context, first_sector, sector_count, buffer);
}
static ios_status backend_write(
    void *context, ios_u64 first_sector, ios_size sector_count, const void *buffer)
{
    return fake_block_write(context, first_sector, sector_count, buffer);
}
static ios_status backend_flush(void *context) { return fake_block_flush(context); }
static void write_u32(ios_u8 *bytes, ios_u32 value)
{
    for (ios_size index = 0; index < 4; ++index) bytes[index] = (ios_u8)(value >> (index * 8));
}

static ios_status prepare_volume(
    struct ios_fake_block *backend, struct ios_block_device *device,
    struct ios_fault_injector *faults)
{
    static const ios_u8 label[IOS_FS_VOLUME_LABEL_SIZE] =
        { 'I', 'N', 'F', 'E', 'R', 'E', 'N', 'C', 'E', ' ', ' ' };
    const struct ios_block_device_operations operations = {
        backend_read, backend_write, backend_flush
    };
    struct ios_fs_geometry geometry;
    struct ios_fs_superblock values;
    struct ios_fs_superblock_disk disk;
    ios_u8 fat[IOS_FS_SECTOR_SIZE] = { 0 };
    ios_status status = fake_block_initialize(
        backend, IOS_FS_MINIMUM_VOLUME_BYTES / IOS_FS_SECTOR_SIZE, 8, faults);
    if (IOS_FAILED(status)) return status;
    status = block_device_initialize(device, backend, &operations, IOS_FS_SECTOR_SIZE,
                                     backend->sector_count, IOS_BLOCK_DEVICE_READY);
    if (IOS_FAILED(status)) return status;
    status = ios_fs_calculate_geometry(device->logical_sector_size, device->sector_count, &geometry);
    if (IOS_FAILED(status)) return status;
    values = (struct ios_fs_superblock){
        geometry.total_sectors, geometry.fat_sectors, geometry.registry_start_sector,
        UINT32_C(0x1234abcd), 0, { 0 }
    };
    memcpy(values.volume_label, label, sizeof(values.volume_label));
    status = ios_fs_superblock_encode(&values, &disk);
    if (IOS_FAILED(status)) return status;
    status = fake_block_write(backend, 0, 1, &disk);
    if (IOS_FAILED(status)) return status;
    status = fake_block_write(backend, 1, 1, &disk);
    if (IOS_FAILED(status)) return status;
    write_u32(fat, IOS_FS_FAT_END_OF_CHAIN);
    write_u32(fat + 4, IOS_FS_FAT_END_OF_CHAIN);
    write_u32(fat + 8, IOS_FS_FAT_END_OF_CHAIN);
    return fake_block_write(backend, 2, 1, fat);
}

static void test_valid_volume_mounts_as_shared_writable_root(void)
{
    struct ios_fault_injector faults;
    struct ios_fake_block backend;
    struct ios_block_device device;
    struct ios_fs_mount filesystem;
    struct ios_vfs_mount_registry registry;
    fault_injector_initialize(&faults);
    IOS_TEST_ASSERT_STATUS(prepare_volume(&backend, &device, &faults), IOS_OK);
    vfs_mount_registry_initialize(&registry);
    IOS_TEST_ASSERT_STATUS(ios_fs_mount_root(&filesystem, &device, &registry), IOS_OK);
    IOS_TEST_ASSERT(vfs_root_mount(&registry) == &filesystem.vfs);
    IOS_TEST_ASSERT(filesystem.vfs.state == IOS_MOUNT_RW && filesystem.vfs.mounted);
    IOS_TEST_ASSERT(filesystem.report.reason == IOS_FS_MOUNT_REASON_NONE);
    IOS_TEST_ASSERT(filesystem.report.trusted_superblock == IOS_FS_TRUSTED_SUPERBLOCK_BOTH);
    IOS_TEST_ASSERT(filesystem.root.identity == IOS_FS_ROOT_CLUSTER
                    && filesystem.root.kind == IOS_VFS_OBJECT_DIRECTORY);
    IOS_TEST_ASSERT_STATUS(vfs_mount_begin_operation(&filesystem.vfs, true), IOS_OK);
    IOS_TEST_ASSERT_STATUS(vfs_unmount_root(&registry), IOS_ERROR(IOS_E_BUSY));
    IOS_TEST_ASSERT_STATUS(vfs_mount_end_operation(&filesystem.vfs), IOS_OK);
    IOS_TEST_ASSERT_STATUS(vfs_unmount_root(&registry), IOS_OK);
    IOS_TEST_ASSERT(vfs_root_mount(&registry) == NULL && !filesystem.vfs.mounted);
    fake_block_destroy(&backend);
}

static void test_one_invalid_superblock_mounts_diagnostic_read_only(void)
{
    struct ios_fault_injector faults;
    struct ios_fake_block backend;
    struct ios_block_device device;
    struct ios_fs_mount filesystem;
    struct ios_vfs_mount_registry registry;
    fault_injector_initialize(&faults);
    IOS_TEST_ASSERT_STATUS(prepare_volume(&backend, &device, &faults), IOS_OK);
    IOS_TEST_ASSERT_STATUS(fake_block_corrupt(&backend, 1, 0x100, 0x80), IOS_OK);
    vfs_mount_registry_initialize(&registry);
    IOS_TEST_ASSERT_STATUS(ios_fs_mount_root(&filesystem, &device, &registry), IOS_OK);
    IOS_TEST_ASSERT(filesystem.vfs.state == IOS_MOUNT_DIAGNOSTIC);
    IOS_TEST_ASSERT(filesystem.report.reason == IOS_FS_MOUNT_REASON_BACKUP_INVALID);
    IOS_TEST_ASSERT(filesystem.report.trusted_superblock == IOS_FS_TRUSTED_SUPERBLOCK_PRIMARY);
    IOS_TEST_ASSERT(filesystem.report.bounds_trusted && filesystem.report.read_only);
    IOS_TEST_ASSERT_STATUS(ios_fs_diagnostic_begin_read(&filesystem), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_fs_diagnostic_end_read(&filesystem), IOS_OK);
    IOS_TEST_ASSERT_STATUS(vfs_mount_begin_operation(&filesystem.vfs, true), IOS_ERROR(IOS_E_READ_ONLY));
    fake_block_destroy(&backend);
}

static void test_invalid_root_fat_or_untrusted_bounds_are_rejected(void)
{
    struct ios_fault_injector faults;
    struct ios_fake_block backend;
    struct ios_block_device device;
    struct ios_fs_mount filesystem;
    struct ios_vfs_mount_registry registry;
    fault_injector_initialize(&faults);
    IOS_TEST_ASSERT_STATUS(prepare_volume(&backend, &device, &faults), IOS_OK);
    IOS_TEST_ASSERT_STATUS(fake_block_corrupt(&backend, 2, 8, 0xff), IOS_OK);
    vfs_mount_registry_initialize(&registry);
    IOS_TEST_ASSERT_STATUS(ios_fs_mount_probe(&filesystem, &device), IOS_OK);
    IOS_TEST_ASSERT(filesystem.vfs.state == IOS_MOUNT_REJECTED);
    IOS_TEST_ASSERT(filesystem.report.reason == IOS_FS_MOUNT_REASON_ROOT_CHAIN_UNSAFE);
    IOS_TEST_ASSERT(filesystem.report.bounds_trusted && !filesystem.report.read_only);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_diagnostic_begin_read(&filesystem), IOS_ERROR(IOS_E_INVALID_STATE));
    IOS_TEST_ASSERT_STATUS(ios_fs_mount_root(&filesystem, &device, &registry), IOS_ERROR(IOS_E_CORRUPT));
    IOS_TEST_ASSERT(vfs_root_mount(&registry) == NULL);
    fake_block_destroy(&backend);
}

static void test_registry_enforces_one_root_namespace(void)
{
    struct ios_fault_injector faults;
    struct ios_fake_block backend;
    struct ios_block_device device;
    struct ios_fs_mount first;
    struct ios_fs_mount second;
    struct ios_vfs_mount_registry registry;
    fault_injector_initialize(&faults);
    IOS_TEST_ASSERT_STATUS(prepare_volume(&backend, &device, &faults), IOS_OK);
    vfs_mount_registry_initialize(&registry);
    IOS_TEST_ASSERT_STATUS(ios_fs_mount_root(&first, &device, &registry), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_fs_mount_root(&second, &device, &registry),
                           IOS_ERROR(IOS_E_ALREADY_EXISTS));
    IOS_TEST_ASSERT(registry.count == 1 && registry.root == &first.vfs);
    fake_block_destroy(&backend);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_valid_volume_mounts_as_shared_writable_root),
    IOS_TEST_CASE(test_one_invalid_superblock_mounts_diagnostic_read_only),
    IOS_TEST_CASE(test_invalid_root_fat_or_untrusted_bounds_are_rejected),
    IOS_TEST_CASE(test_registry_enforces_one_root_namespace)
};
const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
