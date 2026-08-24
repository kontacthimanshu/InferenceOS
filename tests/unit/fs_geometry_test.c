#include <inferenceos/test.h>

#include <inferenceos/block.h>
#include <inferenceos/fs/format.h>

#include <string.h>

struct sparse_format_backend {
    ios_u64 sector_count;
    ios_u64 zero_start;
    ios_u64 zero_end;
    ios_u64 write_operations;
    ios_u64 flush_operations;
    struct ios_fs_superblock_disk primary;
    struct ios_fs_superblock_disk backup;
    ios_u8 fat_sector[IOS_FS_SECTOR_SIZE];
    bool primary_seen;
    bool backup_seen;
    bool fat_seen;
    bool fail_flush;
};

static bool all_zero(const ios_u8 *bytes, ios_size count)
{
    for (ios_size index = 0; index < count; ++index) if (bytes[index] != 0) return false;
    return true;
}
static ios_status sparse_read(
    void *opaque, ios_u64 first_sector, ios_size sector_count, void *buffer)
{
    struct sparse_format_backend *backend = opaque;
    if (first_sector >= backend->sector_count || sector_count > backend->sector_count - first_sector) {
        return IOS_ERROR(IOS_E_OUT_OF_RANGE);
    }
    memset(buffer, 0, sector_count * IOS_FS_SECTOR_SIZE);
    return IOS_OK;
}
static ios_status sparse_write(
    void *opaque, ios_u64 first_sector, ios_size sector_count, const void *buffer)
{
    struct sparse_format_backend *backend = opaque;
    const ios_u8 *bytes = buffer;
    if (sector_count == 0 || first_sector >= backend->sector_count
        || sector_count > backend->sector_count - first_sector) return IOS_ERROR(IOS_E_OUT_OF_RANGE);
    ++backend->write_operations;
    if (first_sector == 0 && sector_count == 1) {
        memcpy(&backend->primary, buffer, sizeof(backend->primary));
        backend->primary_seen = true;
    } else if (first_sector == 1 && sector_count == 1) {
        memcpy(&backend->backup, buffer, sizeof(backend->backup));
        backend->backup_seen = true;
    } else if (first_sector == 2 && sector_count == 1 && !all_zero(bytes, IOS_FS_SECTOR_SIZE)) {
        memcpy(backend->fat_sector, buffer, sizeof(backend->fat_sector));
        backend->fat_seen = true;
    } else {
        IOS_TEST_ASSERT(all_zero(bytes, sector_count * IOS_FS_SECTOR_SIZE));
        if (backend->zero_start == 0 || first_sector < backend->zero_start) backend->zero_start = first_sector;
        if (first_sector + sector_count > backend->zero_end) backend->zero_end = first_sector + sector_count;
    }
    return IOS_OK;
}
static ios_status sparse_flush(void *opaque)
{
    struct sparse_format_backend *backend = opaque;
    ++backend->flush_operations;
    return backend->fail_flush ? IOS_ERROR(IOS_E_IO) : IOS_OK;
}
static ios_status initialize_sparse_device(
    struct ios_block_device *device, struct sparse_format_backend *backend, ios_u32 sector_size)
{
    const struct ios_block_device_operations operations = { sparse_read, sparse_write, sparse_flush };
    return block_device_initialize(device, backend, &operations, sector_size, backend->sector_count,
                                   IOS_BLOCK_DEVICE_READY);
}

static void assert_geometry(const struct ios_fs_geometry *geometry)
{
    ios_u64 fat_entries = (ios_u64)geometry->fat_sectors * IOS_FS_SECTOR_SIZE / 4;
    ios_u64 data_sectors = geometry->total_sectors - geometry->data_start_sector;
    IOS_TEST_ASSERT(geometry->registry_start_sector == IOS_FS_SUPERBLOCK_SECTORS + geometry->fat_sectors);
    IOS_TEST_ASSERT(geometry->data_start_sector == geometry->registry_start_sector + IOS_FS_REGISTRY_SECTORS);
    IOS_TEST_ASSERT(geometry->cluster_count == data_sectors / IOS_FS_SECTORS_PER_CLUSTER);
    IOS_TEST_ASSERT(fat_entries >= (ios_u64)geometry->cluster_count + 2);
    IOS_TEST_ASSERT(fat_entries - ((ios_u64)geometry->cluster_count + 2) < 256);
    IOS_TEST_ASSERT(geometry->usable_bytes
                    == (ios_u64)geometry->cluster_count * IOS_FS_SECTORS_PER_CLUSTER * IOS_FS_SECTOR_SIZE);
}

static void test_exact_minimum_and_reference_geometry(void)
{
    struct ios_fs_geometry geometry;
    IOS_TEST_ASSERT_STATUS(ios_fs_calculate_geometry(
        IOS_FS_SECTOR_SIZE, IOS_FS_MINIMUM_VOLUME_BYTES / IOS_FS_SECTOR_SIZE, &geometry), IOS_OK);
    IOS_TEST_ASSERT(geometry.fat_sectors == 95271);
    IOS_TEST_ASSERT(geometry.registry_start_sector == UINT64_C(95273));
    IOS_TEST_ASSERT(geometry.data_start_sector == UINT64_C(99369));
    IOS_TEST_ASSERT(geometry.cluster_count == 12194610);
    assert_geometry(&geometry);
    IOS_TEST_ASSERT_STATUS(ios_fs_calculate_geometry(
        IOS_FS_SECTOR_SIZE, UINT64_C(64) * 1024 * 1024 * 1024 / IOS_FS_SECTOR_SIZE,
        &geometry), IOS_OK);
    IOS_TEST_ASSERT(geometry.fat_sectors == 130941 && geometry.cluster_count == 16760336);
    assert_geometry(&geometry);
}

static void test_fixed_point_properties_and_invalid_ranges(void)
{
    struct ios_fs_geometry previous;
    ios_u64 sectors = IOS_FS_MINIMUM_VOLUME_BYTES / IOS_FS_SECTOR_SIZE;
    IOS_TEST_ASSERT_STATUS(ios_fs_calculate_geometry(IOS_FS_SECTOR_SIZE, sectors, &previous), IOS_OK);
    for (ios_u32 sample = 1; sample <= 4096; ++sample) {
        struct ios_fs_geometry current;
        sectors += (ios_u64)((sample * UINT32_C(2654435761)) | 1) % UINT32_C(131072);
        IOS_TEST_ASSERT_STATUS(ios_fs_calculate_geometry(IOS_FS_SECTOR_SIZE, sectors, &current), IOS_OK);
        assert_geometry(&current);
        IOS_TEST_ASSERT(current.data_start_sector >= previous.data_start_sector);
        IOS_TEST_ASSERT(current.cluster_count >= previous.cluster_count);
        previous = current;
    }
    IOS_TEST_ASSERT_STATUS(ios_fs_calculate_geometry(4096, sectors, &previous),
                           IOS_ERROR(IOS_E_INVALID_ARGUMENT));
    IOS_TEST_ASSERT_STATUS(ios_fs_calculate_geometry(IOS_FS_SECTOR_SIZE,
        IOS_FS_MINIMUM_VOLUME_BYTES / IOS_FS_SECTOR_SIZE - 1, &previous), IOS_ERROR(IOS_E_NO_SPACE));
    IOS_TEST_ASSERT_STATUS(ios_fs_calculate_geometry(IOS_FS_SECTOR_SIZE, UINT64_MAX, &previous),
                           IOS_ERROR(IOS_E_OUT_OF_RANGE));
    IOS_TEST_ASSERT(previous.total_sectors == 0 && previous.cluster_count == 0);
}

static void test_formatter_initializes_required_regions_and_flushes(void)
{
    static const ios_u8 label[IOS_FS_VOLUME_LABEL_SIZE] =
        { 'I', 'N', 'F', 'E', 'R', 'E', 'N', 'C', 'E', ' ', ' ' };
    struct sparse_format_backend backend = {
        .sector_count = IOS_FS_MINIMUM_VOLUME_BYTES / IOS_FS_SECTOR_SIZE
    };
    struct ios_block_device device;
    struct ios_fs_geometry geometry;
    struct ios_fs_superblock decoded;
    IOS_TEST_ASSERT_STATUS(initialize_sparse_device(&device, &backend, IOS_FS_SECTOR_SIZE), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_fs_format(&device, UINT32_C(0x1234abcd), label, &geometry), IOS_OK);
    IOS_TEST_ASSERT(backend.primary_seen && backend.backup_seen && backend.fat_seen);
    IOS_TEST_ASSERT(backend.zero_start == 2);
    IOS_TEST_ASSERT(backend.zero_end == geometry.data_start_sector + IOS_FS_SECTORS_PER_CLUSTER);
    IOS_TEST_ASSERT(backend.flush_operations == 1);
    IOS_TEST_ASSERT(ios_fs_superblock_classify_pair(&backend.primary, &backend.backup)
                    == IOS_FS_SUPERBLOCK_PAIR_READ_WRITE);
    IOS_TEST_ASSERT_STATUS(ios_fs_superblock_decode(&backend.primary, &decoded), IOS_OK);
    IOS_TEST_ASSERT(decoded.volume_serial == UINT32_C(0x1234abcd));
    for (ios_size index = 0; index < 12; ++index) {
        IOS_TEST_ASSERT(backend.fat_sector[index] == (index % 4 == 3 ? 0x0f : 0xff));
    }
    IOS_TEST_ASSERT(all_zero(backend.fat_sector + 12, IOS_FS_SECTOR_SIZE - 12));
}

static void test_formatter_rejects_sector_size_and_propagates_flush_failure(void)
{
    static const ios_u8 label[IOS_FS_VOLUME_LABEL_SIZE] =
        { 'I', 'N', 'F', 'E', 'R', 'E', 'N', 'C', 'E', ' ', ' ' };
    struct sparse_format_backend backend = {
        .sector_count = IOS_FS_MINIMUM_VOLUME_BYTES / IOS_FS_SECTOR_SIZE,
        .fail_flush = true
    };
    struct ios_block_device device;
    struct ios_fs_geometry geometry;
    IOS_TEST_ASSERT_STATUS(initialize_sparse_device(&device, &backend, 4096), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_fs_format(&device, 1, label, &geometry),
                           IOS_ERROR(IOS_E_INVALID_ARGUMENT));
    IOS_TEST_ASSERT_STATUS(initialize_sparse_device(&device, &backend, IOS_FS_SECTOR_SIZE), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_fs_format(&device, 1, label, &geometry), IOS_ERROR(IOS_E_IO));
    IOS_TEST_ASSERT(geometry.total_sectors == 0 && backend.flush_operations == 1);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_exact_minimum_and_reference_geometry),
    IOS_TEST_CASE(test_fixed_point_properties_and_invalid_ranges),
    IOS_TEST_CASE(test_formatter_initializes_required_regions_and_flushes),
    IOS_TEST_CASE(test_formatter_rejects_sector_size_and_propagates_flush_failure)
};
const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
