#include <inferenceos/test.h>

#include <inferenceos/fs/fat.h>
#include <inferenceos/fs/records.h>

#include <string.h>

#define LOWER_CANARY UINT64_C(0x13579bdf2468ace0)
#define UPPER_CANARY UINT64_C(0xfedcba9876543210)

enum { TEST_FAT_ENTRIES = 32 };

struct guarded_primary {
    ios_u64 lower;
    struct ios_fs_primary value;
    ios_u64 upper;
};

struct guarded_companion {
    ios_u64 lower;
    struct ios_fs_companion value;
    ios_u64 upper;
};

struct guarded_clusters {
    ios_u64 lower;
    ios_u32 values[TEST_FAT_ENTRIES];
    ios_u64 upper;
};

static struct ios_fs_primary report_txt(void)
{
    const struct ios_fs_primary value = {
        { 'R', 'E', 'P', 'O', 'R', 'T', ' ', ' ', 'T', 'X', 'T' },
        IOS_FS_ATTRIBUTE_REGULAR, 2, 1
    };
    return value;
}

static void assert_primary_guard(const struct guarded_primary *guarded)
{
    struct ios_fs_primary zero;
    memset(&zero, 0, sizeof(zero));
    IOS_TEST_ASSERT(guarded->lower == LOWER_CANARY);
    IOS_TEST_ASSERT(guarded->upper == UPPER_CANARY);
    IOS_TEST_ASSERT(memcmp(&guarded->value, &zero, sizeof(zero)) == 0);
}

static void assert_companion_guard(const struct guarded_companion *guarded)
{
    struct ios_fs_companion zero;
    memset(&zero, 0, sizeof(zero));
    IOS_TEST_ASSERT(guarded->lower == LOWER_CANARY);
    IOS_TEST_ASSERT(guarded->upper == UPPER_CANARY);
    IOS_TEST_ASSERT(memcmp(&guarded->value, &zero, sizeof(zero)) == 0);
}

static void test_malformed_primary_diagnostic_is_output_bounded(void)
{
    const struct ios_fs_primary value = report_txt();
    struct ios_fs_primary_disk disk;
    struct guarded_primary guarded;
    IOS_TEST_ASSERT_STATUS(ios_fs_primary_encode(&value, &disk), IOS_OK);
    disk.reserved = 1;
    memset(&guarded.value, 0xa5, sizeof(guarded.value));
    guarded.lower = LOWER_CANARY;
    guarded.upper = UPPER_CANARY;
    IOS_TEST_ASSERT_STATUS(
        ios_fs_primary_decode(&disk, &guarded.value), IOS_ERROR(IOS_E_PROTOCOL)
    );
    assert_primary_guard(&guarded);
}

static void test_malformed_companion_diagnostic_is_output_bounded(void)
{
    const struct ios_fs_primary primary = report_txt();
    struct ios_fs_companion_disk disk;
    struct guarded_companion guarded;
    IOS_TEST_ASSERT_STATUS(ios_fs_companion_encode(primary.name, true, &disk), IOS_OK);
    disk.crc32[0] ^= 1;
    memset(&guarded.value, 0xa5, sizeof(guarded.value));
    guarded.lower = LOWER_CANARY;
    guarded.upper = UPPER_CANARY;
    IOS_TEST_ASSERT_STATUS(
        ios_fs_companion_decode(&disk, &guarded.value), IOS_ERROR(IOS_E_CORRUPT)
    );
    assert_companion_guard(&guarded);
}

static void initialize_linear_fat(ios_u32 fat[TEST_FAT_ENTRIES])
{
    memset(fat, 0, sizeof(ios_u32) * TEST_FAT_ENTRIES);
    fat[0] = IOS_FS_FAT_END_OF_CHAIN;
    fat[1] = IOS_FS_FAT_END_OF_CHAIN;
    for (ios_u32 cluster = 2; cluster + 1 < TEST_FAT_ENTRIES; ++cluster) {
        fat[cluster] = cluster + 1;
    }
    fat[TEST_FAT_ENTRIES - 1] = IOS_FS_FAT_END_OF_CHAIN;
}

static void assert_cluster_guards(const struct guarded_clusters *guarded)
{
    IOS_TEST_ASSERT(guarded->lower == LOWER_CANARY);
    IOS_TEST_ASSERT(guarded->upper == UPPER_CANARY);
}

static void test_maximum_chain_diagnostic_is_entry_bounded(void)
{
    ios_u32 fat[TEST_FAT_ENTRIES];
    struct guarded_clusters guarded;
    ios_size count = 0;
    initialize_linear_fat(fat);
    memset(guarded.values, 0xa5, sizeof(guarded.values));
    guarded.lower = LOWER_CANARY;
    guarded.upper = UPPER_CANARY;
    IOS_TEST_ASSERT_STATUS(ios_fs_fat_traverse(
        fat, IOS_ARRAY_COUNT(fat), 2, guarded.values,
        IOS_ARRAY_COUNT(guarded.values), &count
    ), IOS_OK);
    IOS_TEST_ASSERT(count == TEST_FAT_ENTRIES - 2);
    for (ios_size index = 0; index < count; ++index) {
        IOS_TEST_ASSERT(guarded.values[index] == index + 2);
    }
    assert_cluster_guards(&guarded);
}

static void test_malformed_chain_diagnostics_are_workspace_bounded(void)
{
    ios_u32 fat[TEST_FAT_ENTRIES];
    struct guarded_clusters guarded;
    ios_size count;
    initialize_linear_fat(fat);
    fat[TEST_FAT_ENTRIES - 1] = 2;
    memset(guarded.values, 0xa5, sizeof(guarded.values));
    guarded.lower = LOWER_CANARY;
    guarded.upper = UPPER_CANARY;
    IOS_TEST_ASSERT_STATUS(ios_fs_fat_traverse(
        fat, IOS_ARRAY_COUNT(fat), 2, guarded.values,
        IOS_ARRAY_COUNT(guarded.values), &count
    ), IOS_ERROR(IOS_E_CORRUPT));
    IOS_TEST_ASSERT(count <= IOS_ARRAY_COUNT(guarded.values));
    assert_cluster_guards(&guarded);

    initialize_linear_fat(fat);
    fat[3] = TEST_FAT_ENTRIES;
    IOS_TEST_ASSERT_STATUS(ios_fs_fat_traverse(
        fat, IOS_ARRAY_COUNT(fat), 2, guarded.values, 2, &count
    ), IOS_ERROR(IOS_E_CORRUPT));
    IOS_TEST_ASSERT(count <= 2);
    assert_cluster_guards(&guarded);

    initialize_linear_fat(fat);
    IOS_TEST_ASSERT_STATUS(ios_fs_fat_traverse(
        fat, IOS_ARRAY_COUNT(fat), 2, guarded.values, 2, &count
    ), IOS_ERROR(IOS_E_NO_SPACE));
    IOS_TEST_ASSERT(count <= 2);
    assert_cluster_guards(&guarded);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_malformed_primary_diagnostic_is_output_bounded),
    IOS_TEST_CASE(test_malformed_companion_diagnostic_is_output_bounded),
    IOS_TEST_CASE(test_maximum_chain_diagnostic_is_entry_bounded),
    IOS_TEST_CASE(test_malformed_chain_diagnostics_are_workspace_bounded)
};
const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
