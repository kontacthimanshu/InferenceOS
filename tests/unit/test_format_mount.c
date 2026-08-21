#include "../support/memory_block_device.h"
#include "../support/test_assert.h"

#include <inferencefs/format.h>
#include <inferencefs/formatter.h>
#include <inferenceos/crc32.h>
#include <inferenceos/endian.h>
#include <inferenceos/memory.h>

#define MIB_SECTORS(mebibytes) ((inferenceos_u32)(mebibytes) * 2048U)
#define MINIMUM_SECTORS MIB_SECTORS(16U)
#define REFERENCE_SECTORS MIB_SECTORS(64U)
#define MAXIMUM_SECTORS MIB_SECTORS(1024U)

static inferenceos_u8 format_storage[
    MINIMUM_SECTORS * INFERENCEFS_LOGICAL_SECTOR_SIZE
];
static inferenceos_memory_block_device format_memory_device;

static inferenceos_result solve(
    inferenceos_u64 sector_count,
    inferencefs_geometry *geometry
)
{
    return inferencefs_geometry_solve(
        INFERENCEFS_LOGICAL_SECTOR_SIZE, sector_count, geometry);
}

static void assert_geometry(
    inferenceos_u32 sectors,
    inferenceos_u32 fat_sectors,
    inferenceos_u32 clusters
)
{
    inferencefs_geometry geometry;

    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_OK,
        solve(sectors, &geometry));
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(sectors, geometry.total_sectors);
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(fat_sectors, geometry.sectors_per_fat);
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(
        INFERENCEFS_RESERVED_SECTORS + fat_sectors, geometry.data_start_lba);
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(clusters, geometry.data_cluster_count);
    INFERENCEOS_TEST_ASSERT(
        ((inferenceos_u64)fat_sectors * INFERENCEFS_LOGICAL_SECTOR_SIZE / 4U)
        >= (inferenceos_u64)clusters + 2U);
}

static void test_geometry_accepts_inclusive_size_boundaries(void)
{
    assert_geometry(MINIMUM_SECTORS, 32U, 4091U);
    assert_geometry(REFERENCE_SECTORS, 128U, 16367U);
    assert_geometry(MAXIMUM_SECTORS, 2047U, 261887U);
}

static void test_geometry_rejects_size_and_sector_boundaries(void)
{
    inferencefs_geometry geometry;

    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_OUT_OF_RANGE,
        solve(MINIMUM_SECTORS - 1U, &geometry));
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_OUT_OF_RANGE,
        solve((inferenceos_u64)MAXIMUM_SECTORS + 1U, &geometry));
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_UNSUPPORTED,
        inferencefs_geometry_solve(4096U, MINIMUM_SECTORS, &geometry));
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_INVALID_ARGUMENT,
        solve(MINIMUM_SECTORS, NULL));
}

static void test_geometry_rejects_unrepresentable_capacity_without_overflow(void)
{
    inferencefs_geometry geometry = {
        .total_sectors = UINT32_C(0xA5A5A5A5),
        .sectors_per_fat = UINT32_C(0xA5A5A5A5),
        .data_start_lba = UINT32_C(0xA5A5A5A5),
        .data_cluster_count = UINT32_C(0xA5A5A5A5)
    };

    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_OVERFLOW,
        solve(UINT64_MAX, &geometry));
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(
        UINT32_C(0xA5A5A5A5), geometry.total_sectors);
}

static void setup_format_device(void)
{
    (void)memset(format_storage, 0xA5, sizeof(format_storage));
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_OK,
        inferenceos_memory_block_device_initialize(
            &format_memory_device,
            format_storage,
            sizeof(format_storage),
            INFERENCEFS_LOGICAL_SECTOR_SIZE,
            "format-test-device"));
}

static const inferencefs_superblock_disk *superblock_at(inferenceos_u32 lba)
{
    return (const inferencefs_superblock_disk *)(
        format_storage + ((inferenceos_size)lba * INFERENCEFS_LOGICAL_SECTOR_SIZE));
}

static void format_minimum_volume(inferencefs_geometry *geometry)
{
    static const inferenceos_u8 label[11] = {
        'I', 'N', 'F', 'E', 'R', 'E', 'N', 'C', 'E', ' ', ' '
    };

    setup_format_device();
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_OK,
        inferencefs_format_device(
            inferenceos_memory_block_device_interface(&format_memory_device),
            UINT32_C(0x1234ABCD), label, geometry));
}

static void test_formatter_writes_exact_superblock_layout_and_backup(void)
{
    static const inferenceos_u8 magic[8] = {
        'I', 'N', 'F', 'F', 'A', 'T', '3', '2'
    };
    inferencefs_geometry geometry;
    const inferencefs_superblock_disk *primary;
    const inferencefs_superblock_disk *backup;

    format_minimum_volume(&geometry);
    primary = superblock_at(0U);
    backup = superblock_at(1U);
    INFERENCEOS_TEST_ASSERT_MEMORY_EQUAL(primary, backup, sizeof(*primary));
    INFERENCEOS_TEST_ASSERT_MEMORY_EQUAL(magic, primary->magic, sizeof(magic));
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(INFERENCEFS_FORMAT_VERSION,
        inferenceos_load_le16(primary->format_version_le));
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(INFERENCEFS_LOGICAL_SECTOR_SIZE,
        inferenceos_load_le16(primary->bytes_per_sector_le));
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(INFERENCEFS_SECTORS_PER_CLUSTER,
        primary->sectors_per_cluster);
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(MINIMUM_SECTORS,
        inferenceos_load_le32(primary->total_sectors_le));
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(geometry.sectors_per_fat,
        inferenceos_load_le32(primary->sectors_per_fat_le));
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(INFERENCEFS_ROOT_CLUSTER,
        inferenceos_load_le32(primary->root_cluster_le));
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(UINT32_C(0x1234ABCD),
        inferenceos_load_le32(primary->volume_serial_le));
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(INFERENCEFS_SUPERBLOCK_TRAILER_SIGNATURE,
        inferenceos_load_le16(primary->trailer_signature_le));
}

static void test_superblock_crc_covers_zeroed_crc_field(void)
{
    inferencefs_geometry geometry;
    inferenceos_u8 header[INFERENCEFS_SUPERBLOCK_CRC_LENGTH];
    const inferencefs_superblock_disk *primary;
    inferenceos_u32 stored_crc;

    format_minimum_volume(&geometry);
    primary = superblock_at(0U);
    (void)memcpy(header, primary, sizeof(header));
    stored_crc = inferenceos_load_le32(primary->superblock_crc32_le);
    (void)memset(header + INFERENCEOS_OFFSETOF(
        inferencefs_superblock_disk, superblock_crc32_le), 0, 4U);
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(
        inferenceos_crc32(header, sizeof(header)), stored_crc);
    header[0] ^= 1U;
    INFERENCEOS_TEST_ASSERT_FALSE(
        inferenceos_crc32(header, sizeof(header)) == stored_crc);
}

static void test_formatter_initializes_fat_and_zero_root_cluster(void)
{
    inferencefs_geometry geometry;
    const inferenceos_u8 *fat;
    const inferenceos_u8 *root;

    format_minimum_volume(&geometry);
    fat = format_storage
        + ((inferenceos_size)INFERENCEFS_RESERVED_SECTORS
            * INFERENCEFS_LOGICAL_SECTOR_SIZE);
    root = format_storage
        + ((inferenceos_size)geometry.data_start_lba
            * INFERENCEFS_LOGICAL_SECTOR_SIZE);
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(UINT32_C(0x0FFFFFF8),
        inferenceos_load_le32(fat + 0U));
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(UINT32_C(0x0FFFFFFF),
        inferenceos_load_le32(fat + 4U));
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(UINT32_C(0x0FFFFFFF),
        inferenceos_load_le32(fat + 8U));
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(0U, inferenceos_load_le32(fat + 12U));
    for (inferenceos_size index = 0U; index < 4096U; ++index) {
        INFERENCEOS_TEST_ASSERT_U64_EQUAL(0U, root[index]);
    }
}

static const inferenceos_test_case format_mount_cases[] = {
    INFERENCEOS_TEST_CASE(test_geometry_accepts_inclusive_size_boundaries),
    INFERENCEOS_TEST_CASE(test_geometry_rejects_size_and_sector_boundaries),
    INFERENCEOS_TEST_CASE(test_geometry_rejects_unrepresentable_capacity_without_overflow),
    INFERENCEOS_TEST_CASE(test_formatter_writes_exact_superblock_layout_and_backup),
    INFERENCEOS_TEST_CASE(test_superblock_crc_covers_zeroed_crc_field),
    INFERENCEOS_TEST_CASE(test_formatter_initializes_fat_and_zero_root_cluster)
};

const inferenceos_test_suite *inferenceos_test_suite_definition(void)
{
    static const inferenceos_test_suite suite = {
        .name = "format_mount",
        .cases = format_mount_cases,
        .case_count = INFERENCEOS_ARRAY_COUNT(format_mount_cases)
    };
    return &suite;
}
