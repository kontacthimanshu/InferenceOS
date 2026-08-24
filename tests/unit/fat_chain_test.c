#include <inferenceos/test.h>

#include <inferenceos/fs/fat.h>
#include <inferenceos/fs/validator.h>

#include <string.h>

enum {
    TEST_FAT_ENTRIES = 16,
    TEST_CLUSTER_BYTES = 4096
};

static void initialize_fat(ios_u32 fat[TEST_FAT_ENTRIES])
{
    memset(fat, 0, sizeof(ios_u32) * TEST_FAT_ENTRIES);
    fat[0] = IOS_FS_FAT_END_OF_CHAIN;
    fat[1] = IOS_FS_FAT_END_OF_CHAIN;
    fat[2] = IOS_FS_FAT_END_OF_CHAIN;
}

static void test_fat_entry_is_exact_little_endian_28_bit_value(void)
{
    static const ios_u8 expected[4] = { 0x67, 0x45, 0x23, 0x01 };
    ios_u8 disk[4];
    ios_u32 value;
    IOS_TEST_ASSERT_STATUS(ios_fs_fat_entry_encode(UINT32_C(0x01234567), disk), IOS_OK);
    IOS_TEST_ASSERT(memcmp(disk, expected, sizeof(expected)) == 0);
    IOS_TEST_ASSERT_STATUS(ios_fs_fat_entry_decode(disk, &value), IOS_OK);
    IOS_TEST_ASSERT(value == UINT32_C(0x01234567));
    IOS_TEST_ASSERT_STATUS(
        ios_fs_fat_entry_encode(UINT32_C(0xf1234567), disk),
        IOS_ERROR(IOS_E_INVALID_ARGUMENT)
    );
    disk[3] = 0xf1;
    IOS_TEST_ASSERT_STATUS(ios_fs_fat_entry_decode(disk, &value), IOS_ERROR(IOS_E_PROTOCOL));
}

static void test_traversal_returns_ordered_chain_and_accepts_eoc_range(void)
{
    ios_u32 fat[TEST_FAT_ENTRIES];
    ios_u32 clusters[TEST_FAT_ENTRIES];
    ios_size count;
    initialize_fat(fat);
    fat[2] = 5;
    fat[5] = 9;
    fat[9] = IOS_FS_FAT_EOC_FIRST;
    IOS_TEST_ASSERT_STATUS(ios_fs_fat_traverse(
        fat, IOS_ARRAY_COUNT(fat), 2, clusters, IOS_ARRAY_COUNT(clusters), &count
    ), IOS_OK);
    IOS_TEST_ASSERT(count == 3);
    IOS_TEST_ASSERT(clusters[0] == 2 && clusters[1] == 5 && clusters[2] == 9);
}

static void test_traversal_detects_loops_and_small_output_buffers(void)
{
    ios_u32 fat[TEST_FAT_ENTRIES];
    ios_u32 clusters[TEST_FAT_ENTRIES];
    ios_size count;
    initialize_fat(fat);
    fat[3] = 3;
    IOS_TEST_ASSERT_STATUS(ios_fs_fat_traverse(
        fat, IOS_ARRAY_COUNT(fat), 3, clusters, IOS_ARRAY_COUNT(clusters), &count
    ), IOS_ERROR(IOS_E_CORRUPT));
    fat[3] = 4;
    fat[4] = 7;
    fat[7] = 3;
    IOS_TEST_ASSERT_STATUS(ios_fs_fat_traverse(
        fat, IOS_ARRAY_COUNT(fat), 3, clusters, IOS_ARRAY_COUNT(clusters), &count
    ), IOS_ERROR(IOS_E_CORRUPT));
    fat[7] = IOS_FS_FAT_END_OF_CHAIN;
    IOS_TEST_ASSERT_STATUS(
        ios_fs_fat_traverse(fat, IOS_ARRAY_COUNT(fat), 3, clusters, 2, &count),
        IOS_ERROR(IOS_E_NO_SPACE)
    );
}

static void test_traversal_rejects_invalid_bounds_and_markers(void)
{
    static const ios_u32 invalid[] = {
        IOS_FS_FAT_FREE, 1, TEST_FAT_ENTRIES, IOS_FS_FAT_BAD,
        IOS_FS_FAT_RESERVED_FIRST, IOS_FS_FAT_RESERVED_LAST, UINT32_C(0x10000002)
    };
    ios_u32 fat[TEST_FAT_ENTRIES];
    ios_u32 clusters[TEST_FAT_ENTRIES];
    ios_size count;
    for (ios_size index = 0; index < IOS_ARRAY_COUNT(invalid); ++index) {
        initialize_fat(fat);
        fat[3] = invalid[index];
        IOS_TEST_ASSERT(IOS_FAILED(ios_fs_fat_traverse(
            fat, IOS_ARRAY_COUNT(fat), 3, clusters, IOS_ARRAY_COUNT(clusters), &count
        )));
    }
    IOS_TEST_ASSERT_STATUS(ios_fs_fat_traverse(
        fat, IOS_ARRAY_COUNT(fat), 0, clusters, IOS_ARRAY_COUNT(clusters), &count
    ), IOS_ERROR(IOS_E_INVALID_ARGUMENT));
    IOS_TEST_ASSERT_STATUS(ios_fs_fat_traverse(
        fat, IOS_ARRAY_COUNT(fat), TEST_FAT_ENTRIES,
        clusters, IOS_ARRAY_COUNT(clusters), &count
    ), IOS_ERROR(IOS_E_INVALID_ARGUMENT));
}

static void test_allocation_links_free_clusters_and_is_atomic_when_full(void)
{
    ios_u32 fat[TEST_FAT_ENTRIES];
    ios_u32 snapshot[TEST_FAT_ENTRIES];
    ios_u32 allocated[TEST_FAT_ENTRIES];
    ios_size count;
    initialize_fat(fat);
    fat[3] = IOS_FS_FAT_END_OF_CHAIN;
    IOS_TEST_ASSERT_STATUS(ios_fs_fat_allocate(
        fat, IOS_ARRAY_COUNT(fat), 3, allocated, IOS_ARRAY_COUNT(allocated), &count
    ), IOS_OK);
    IOS_TEST_ASSERT(count == 3);
    IOS_TEST_ASSERT(allocated[0] == 4 && allocated[1] == 5 && allocated[2] == 6);
    IOS_TEST_ASSERT(fat[4] == 5 && fat[5] == 6 && fat[6] == IOS_FS_FAT_END_OF_CHAIN);
    memcpy(snapshot, fat, sizeof(snapshot));
    IOS_TEST_ASSERT_STATUS(ios_fs_fat_allocate(
        fat, IOS_ARRAY_COUNT(fat), 12, allocated, IOS_ARRAY_COUNT(allocated), &count
    ), IOS_ERROR(IOS_E_NO_SPACE));
    IOS_TEST_ASSERT(memcmp(fat, snapshot, sizeof(snapshot)) == 0);
}

static void test_free_validates_before_mutation_and_releases_once(void)
{
    ios_u32 fat[TEST_FAT_ENTRIES];
    ios_u32 snapshot[TEST_FAT_ENTRIES];
    ios_u32 workspace[TEST_FAT_ENTRIES];
    initialize_fat(fat);
    fat[3] = 4;
    fat[4] = 3;
    memcpy(snapshot, fat, sizeof(snapshot));
    IOS_TEST_ASSERT_STATUS(ios_fs_fat_free(
        fat, IOS_ARRAY_COUNT(fat), 3, workspace, IOS_ARRAY_COUNT(workspace)
    ), IOS_ERROR(IOS_E_CORRUPT));
    IOS_TEST_ASSERT(memcmp(fat, snapshot, sizeof(snapshot)) == 0);
    fat[4] = 7;
    fat[7] = IOS_FS_FAT_END_OF_CHAIN;
    IOS_TEST_ASSERT_STATUS(ios_fs_fat_free(
        fat, IOS_ARRAY_COUNT(fat), 3, workspace, IOS_ARRAY_COUNT(workspace)
    ), IOS_OK);
    IOS_TEST_ASSERT(fat[3] == IOS_FS_FAT_FREE && fat[4] == IOS_FS_FAT_FREE
                    && fat[7] == IOS_FS_FAT_FREE);
}

static void test_duplicate_ownership_and_short_file_chains_are_rejected(void)
{
    static const ios_u32 distinct_starts[] = { 2, 6 };
    static const ios_u32 overlapping_starts[] = { 2, 5 };
    ios_u32 fat[TEST_FAT_ENTRIES];
    ios_u32 owners[TEST_FAT_ENTRIES];
    initialize_fat(fat);
    fat[2] = 5;
    fat[5] = IOS_FS_FAT_END_OF_CHAIN;
    fat[6] = 7;
    fat[7] = IOS_FS_FAT_END_OF_CHAIN;
    IOS_TEST_ASSERT_STATUS(ios_fs_fat_validate_ownership(
        fat, IOS_ARRAY_COUNT(fat), distinct_starts, IOS_ARRAY_COUNT(distinct_starts),
        owners, IOS_ARRAY_COUNT(owners)
    ), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_fs_fat_validate_ownership(
        fat, IOS_ARRAY_COUNT(fat), overlapping_starts, IOS_ARRAY_COUNT(overlapping_starts),
        owners, IOS_ARRAY_COUNT(owners)
    ), IOS_ERROR(IOS_E_CORRUPT));
    IOS_TEST_ASSERT_STATUS(ios_fs_fat_validate_file_capacity(
        fat, IOS_ARRAY_COUNT(fat), 0, 0, TEST_CLUSTER_BYTES
    ), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_fs_fat_validate_file_capacity(
        fat, IOS_ARRAY_COUNT(fat), 2, 8192, TEST_CLUSTER_BYTES
    ), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_fs_fat_validate_file_capacity(
        fat, IOS_ARRAY_COUNT(fat), 2, 8193, TEST_CLUSTER_BYTES
    ), IOS_ERROR(IOS_E_CORRUPT));
}

static void test_validator_bounds_complete_owner_sets(void)
{
    struct ios_fs_allocation_owner owners[] = {
        { 2, 0, IOS_FS_OWNER_DIRECTORY },
        { 4, 8192, IOS_FS_OWNER_REGULAR_FILE },
        { 0, 0, IOS_FS_OWNER_REGULAR_FILE }
    };
    ios_u32 fat[TEST_FAT_ENTRIES];
    ios_u32 owner_map[TEST_FAT_ENTRIES];
    initialize_fat(fat);
    fat[4] = 5;
    fat[5] = IOS_FS_FAT_END_OF_CHAIN;
    IOS_TEST_ASSERT_STATUS(
        ios_fs_validate_owners(
            fat, IOS_ARRAY_COUNT(fat), owners, IOS_ARRAY_COUNT(owners),
            owner_map, IOS_ARRAY_COUNT(owner_map)),
        IOS_OK);
    IOS_TEST_ASSERT(owner_map[2] == 1 && owner_map[4] == 2 && owner_map[5] == 2);

    owners[1].file_size = 8193;
    IOS_TEST_ASSERT_STATUS(
        ios_fs_validate_owners(
            fat, IOS_ARRAY_COUNT(fat), owners, IOS_ARRAY_COUNT(owners),
            owner_map, IOS_ARRAY_COUNT(owner_map)),
        IOS_ERROR(IOS_E_CORRUPT));
    owners[1].file_size = 8192;
    owners[1].first_cluster = 2;
    IOS_TEST_ASSERT_STATUS(
        ios_fs_validate_owners(
            fat, IOS_ARRAY_COUNT(fat), owners, IOS_ARRAY_COUNT(owners),
            owner_map, IOS_ARRAY_COUNT(owner_map)),
        IOS_ERROR(IOS_E_CORRUPT));
    owners[1].first_cluster = 4;
    fat[5] = 4;
    IOS_TEST_ASSERT_STATUS(
        ios_fs_validate_owners(
            fat, IOS_ARRAY_COUNT(fat), owners, IOS_ARRAY_COUNT(owners),
            owner_map, IOS_ARRAY_COUNT(owner_map)),
        IOS_ERROR(IOS_E_CORRUPT));
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_fat_entry_is_exact_little_endian_28_bit_value),
    IOS_TEST_CASE(test_traversal_returns_ordered_chain_and_accepts_eoc_range),
    IOS_TEST_CASE(test_traversal_detects_loops_and_small_output_buffers),
    IOS_TEST_CASE(test_traversal_rejects_invalid_bounds_and_markers),
    IOS_TEST_CASE(test_allocation_links_free_clusters_and_is_atomic_when_full),
    IOS_TEST_CASE(test_free_validates_before_mutation_and_releases_once),
    IOS_TEST_CASE(test_duplicate_ownership_and_short_file_chains_are_rejected),
    IOS_TEST_CASE(test_validator_bounds_complete_owner_sets)
};
const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
