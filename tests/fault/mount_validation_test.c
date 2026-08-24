#include <inferenceos/test.h>

#include <inferenceos/block.h>
#include <inferenceos/fake_block.h>
#include <inferenceos/fs/fat.h>
#include <inferenceos/fs/mount.h>

#include <string.h>

struct mount_fixture {
    struct ios_fault_injector faults;
    struct ios_fake_block backend;
    struct ios_block_device device;
};

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

static ios_status backend_flush(void *context)
{
    return fake_block_flush(context);
}

static void write_u32(ios_u8 *bytes, ios_u32 value)
{
    for (ios_size index = 0; index < 4; ++index) {
        bytes[index] = (ios_u8)(value >> (index * 8));
    }
}

static ios_status fixture_initialize(struct mount_fixture *fixture)
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
    ios_status status;

    memset(fixture, 0, sizeof(*fixture));
    fault_injector_initialize(&fixture->faults);
    status = fake_block_initialize(
        &fixture->backend, IOS_FS_MINIMUM_VOLUME_BYTES / IOS_FS_SECTOR_SIZE, 8,
        &fixture->faults);
    if (IOS_FAILED(status)) return status;
    status = block_device_initialize(
        &fixture->device, &fixture->backend, &operations, IOS_FS_SECTOR_SIZE,
        fixture->backend.sector_count, IOS_BLOCK_DEVICE_READY);
    if (IOS_FAILED(status)) return status;
    status = ios_fs_calculate_geometry(
        fixture->device.logical_sector_size, fixture->device.sector_count, &geometry);
    if (IOS_FAILED(status)) return status;
    values = (struct ios_fs_superblock){
        geometry.total_sectors, geometry.fat_sectors, geometry.registry_start_sector,
        UINT32_C(0x1234abcd), 0, { 0 }
    };
    memcpy(values.volume_label, label, sizeof(values.volume_label));
    status = ios_fs_superblock_encode(&values, &disk);
    if (IOS_FAILED(status)) return status;
    status = fake_block_write(&fixture->backend, 0, 1, &disk);
    if (IOS_FAILED(status)) return status;
    status = fake_block_write(&fixture->backend, 1, 1, &disk);
    if (IOS_FAILED(status)) return status;
    write_u32(fat, IOS_FS_FAT_END_OF_CHAIN);
    write_u32(fat + 4, IOS_FS_FAT_END_OF_CHAIN);
    write_u32(fat + IOS_FS_ROOT_CLUSTER * 4, IOS_FS_FAT_END_OF_CHAIN);
    return fake_block_write(&fixture->backend, IOS_FS_SUPERBLOCK_SECTORS, 1, fat);
}

static enum ios_mount_state probe_state(struct mount_fixture *fixture)
{
    struct ios_fs_mount mount;
    IOS_TEST_ASSERT_STATUS(ios_fs_mount_probe(&mount, &fixture->device), IOS_OK);
    return mount.vfs.state;
}

static void test_each_trusted_superblock_field_requires_a_valid_peer(void)
{
    static const ios_size trusted_offsets[] = {
        offsetof(struct ios_fs_superblock_disk, magic),
        offsetof(struct ios_fs_superblock_disk, format_version),
        offsetof(struct ios_fs_superblock_disk, header_size),
        offsetof(struct ios_fs_superblock_disk, bytes_per_sector),
        offsetof(struct ios_fs_superblock_disk, sectors_per_cluster),
        offsetof(struct ios_fs_superblock_disk, fat_count),
        offsetof(struct ios_fs_superblock_disk, reserved_superblock_sectors),
        offsetof(struct ios_fs_superblock_disk, primary_record_size),
        offsetof(struct ios_fs_superblock_disk, companion_record_size),
        offsetof(struct ios_fs_superblock_disk, flags),
        offsetof(struct ios_fs_superblock_disk, total_sectors),
        offsetof(struct ios_fs_superblock_disk, sectors_per_fat),
        offsetof(struct ios_fs_superblock_disk, root_cluster),
        offsetof(struct ios_fs_superblock_disk, registry_start_sector),
        offsetof(struct ios_fs_superblock_disk, registry_sector_count),
        offsetof(struct ios_fs_superblock_disk, volume_label),
        offsetof(struct ios_fs_superblock_disk, hash_algorithm_id),
        offsetof(struct ios_fs_superblock_disk, companion_record_version),
        offsetof(struct ios_fs_superblock_disk, registry_record_version),
        offsetof(struct ios_fs_superblock_disk, crc32),
        offsetof(struct ios_fs_superblock_disk, reserved_header),
        offsetof(struct ios_fs_superblock_disk, reserved_header)
            + sizeof(((struct ios_fs_superblock_disk *)0)->reserved_header) - 1,
        offsetof(struct ios_fs_superblock_disk, reserved),
        offsetof(struct ios_fs_superblock_disk, reserved)
            + sizeof(((struct ios_fs_superblock_disk *)0)->reserved) - 1,
        offsetof(struct ios_fs_superblock_disk, trailer_signature)
    };

    for (ios_size index = 0; index < IOS_ARRAY_COUNT(trusted_offsets); ++index) {
        struct mount_fixture fixture;
        IOS_TEST_ASSERT_STATUS(fixture_initialize(&fixture), IOS_OK);
        IOS_TEST_ASSERT_STATUS(
            fake_block_corrupt(&fixture.backend, 0, trusted_offsets[index], 1), IOS_OK);
        IOS_TEST_ASSERT(probe_state(&fixture) == IOS_MOUNT_DIAGNOSTIC);
        IOS_TEST_ASSERT_STATUS(
            fake_block_corrupt(&fixture.backend, 1, trusted_offsets[index], 1), IOS_OK);
        IOS_TEST_ASSERT(probe_state(&fixture) == IOS_MOUNT_REJECTED);
        fake_block_destroy(&fixture.backend);
    }
}

static void test_valid_but_disagreeing_superblocks_are_read_only(void)
{
    struct mount_fixture fixture;
    struct ios_fs_superblock_disk backup;
    struct ios_fs_superblock values;

    IOS_TEST_ASSERT_STATUS(fixture_initialize(&fixture), IOS_OK);
    IOS_TEST_ASSERT_STATUS(fake_block_read(&fixture.backend, 1, 1, &backup), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_fs_superblock_decode(&backup, &values), IOS_OK);
    ++values.volume_serial;
    IOS_TEST_ASSERT_STATUS(ios_fs_superblock_encode(&values, &backup), IOS_OK);
    IOS_TEST_ASSERT_STATUS(fake_block_write(&fixture.backend, 1, 1, &backup), IOS_OK);
    IOS_TEST_ASSERT(probe_state(&fixture) == IOS_MOUNT_DIAGNOSTIC);
    fake_block_destroy(&fixture.backend);
}

static void test_untrustworthy_root_fat_values_are_rejected(void)
{
    static const ios_u32 corrupt_values[] = {
        0, 1, IOS_FS_ROOT_CLUSTER, IOS_FS_MAX_DATA_CLUSTER,
        UINT32_C(0x0ffffff0), UINT32_C(0x0ffffff7)
    };

    for (ios_size index = 0; index < IOS_ARRAY_COUNT(corrupt_values); ++index) {
        struct mount_fixture fixture;
        ios_u8 fat[IOS_FS_SECTOR_SIZE];
        IOS_TEST_ASSERT_STATUS(fixture_initialize(&fixture), IOS_OK);
        IOS_TEST_ASSERT_STATUS(
            fake_block_read(&fixture.backend, IOS_FS_SUPERBLOCK_SECTORS, 1, fat), IOS_OK);
        write_u32(fat + IOS_FS_ROOT_CLUSTER * 4, corrupt_values[index]);
        IOS_TEST_ASSERT_STATUS(
            fake_block_write(&fixture.backend, IOS_FS_SUPERBLOCK_SECTORS, 1, fat), IOS_OK);
        IOS_TEST_ASSERT(probe_state(&fixture) == IOS_MOUNT_REJECTED);
        fake_block_destroy(&fixture.backend);
    }
}

static void test_corrupt_reserved_fat_entries_are_rejected(void)
{
    for (ios_size reserved = 0; reserved < IOS_FS_ROOT_CLUSTER; ++reserved) {
        struct mount_fixture fixture;
        ios_u8 fat[IOS_FS_SECTOR_SIZE];
        IOS_TEST_ASSERT_STATUS(fixture_initialize(&fixture), IOS_OK);
        IOS_TEST_ASSERT_STATUS(
            fake_block_read(&fixture.backend, IOS_FS_SUPERBLOCK_SECTORS, 1, fat), IOS_OK);
        write_u32(fat + reserved * 4, IOS_FS_FAT_FREE);
        IOS_TEST_ASSERT_STATUS(
            fake_block_write(&fixture.backend, IOS_FS_SUPERBLOCK_SECTORS, 1, fat), IOS_OK);
        IOS_TEST_ASSERT(probe_state(&fixture) == IOS_MOUNT_REJECTED);
        fake_block_destroy(&fixture.backend);
    }
}

static void test_root_chain_traversal_accepts_growth_and_rejects_loops(void)
{
    struct mount_fixture fixture;
    ios_u8 fat[IOS_FS_SECTOR_SIZE];

    IOS_TEST_ASSERT_STATUS(fixture_initialize(&fixture), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        fake_block_read(&fixture.backend, IOS_FS_SUPERBLOCK_SECTORS, 1, fat), IOS_OK);
    write_u32(fat + IOS_FS_ROOT_CLUSTER * 4, 3);
    write_u32(fat + 3 * 4, 4);
    write_u32(fat + 4 * 4, IOS_FS_FAT_END_OF_CHAIN);
    IOS_TEST_ASSERT_STATUS(
        fake_block_write(&fixture.backend, IOS_FS_SUPERBLOCK_SECTORS, 1, fat), IOS_OK);
    IOS_TEST_ASSERT(probe_state(&fixture) == IOS_MOUNT_RW);
    write_u32(fat + 4 * 4, 3);
    IOS_TEST_ASSERT_STATUS(
        fake_block_write(&fixture.backend, IOS_FS_SUPERBLOCK_SECTORS, 1, fat), IOS_OK);
    IOS_TEST_ASSERT(probe_state(&fixture) == IOS_MOUNT_REJECTED);
    fake_block_destroy(&fixture.backend);
}

static void test_mount_read_failure_never_exposes_a_volume(void)
{
    struct mount_fixture fixture;
    struct ios_fs_mount mount;

    IOS_TEST_ASSERT_STATUS(fixture_initialize(&fixture), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        fault_injector_fail_once(
            &fixture.faults, IOS_FAULT_BLOCK_READ, 1, IOS_ERROR(IOS_E_IO)), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_fs_mount_probe(&mount, &fixture.device), IOS_ERROR(IOS_E_IO));
    IOS_TEST_ASSERT(mount.vfs.state == IOS_MOUNT_REJECTED);
    fake_block_destroy(&fixture.backend);
}

static void test_invalid_device_geometry_is_rejected_without_io(void)
{
    struct mount_fixture fixture;
    struct ios_fs_mount mount;

    IOS_TEST_ASSERT_STATUS(fixture_initialize(&fixture), IOS_OK);
    fixture.device.logical_sector_size = IOS_FS_SECTOR_SIZE / 2;
    IOS_TEST_ASSERT_STATUS(ios_fs_mount_probe(&mount, &fixture.device), IOS_OK);
    IOS_TEST_ASSERT(mount.vfs.state == IOS_MOUNT_REJECTED);
    IOS_TEST_ASSERT(fixture.backend.read_operations == 0);
    fixture.device.logical_sector_size = IOS_FS_SECTOR_SIZE;
    fixture.device.sector_count = IOS_FS_MINIMUM_VOLUME_BYTES / IOS_FS_SECTOR_SIZE - 1;
    IOS_TEST_ASSERT_STATUS(ios_fs_mount_probe(&mount, &fixture.device), IOS_OK);
    IOS_TEST_ASSERT(mount.vfs.state == IOS_MOUNT_REJECTED);
    IOS_TEST_ASSERT(fixture.backend.read_operations == 0);
    fake_block_destroy(&fixture.backend);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_each_trusted_superblock_field_requires_a_valid_peer),
    IOS_TEST_CASE(test_valid_but_disagreeing_superblocks_are_read_only),
    IOS_TEST_CASE(test_untrustworthy_root_fat_values_are_rejected),
    IOS_TEST_CASE(test_corrupt_reserved_fat_entries_are_rejected),
    IOS_TEST_CASE(test_root_chain_traversal_accepts_growth_and_rejects_loops),
    IOS_TEST_CASE(test_mount_read_failure_never_exposes_a_volume),
    IOS_TEST_CASE(test_invalid_device_geometry_is_rejected_without_io)
};

const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
