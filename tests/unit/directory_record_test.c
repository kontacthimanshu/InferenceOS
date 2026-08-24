#include <inferenceos/test.h>

#include <inferenceos/fs/directory.h>
#include <inferenceos/fs/records.h>

#include <string.h>

static struct ios_fs_primary report_txt(void)
{
    const struct ios_fs_primary value = {
        { 'R', 'E', 'P', 'O', 'R', 'T', ' ', ' ', 'T', 'X', 'T' },
        IOS_FS_ATTRIBUTE_REGULAR,
        UINT32_C(0x01234567),
        UINT32_C(0x89abcdef)
    };
    return value;
}

static void test_primary_exact_layout_and_round_trip(void)
{
    static const ios_u8 expected[IOS_FS_PRIMARY_RECORD_SIZE] = {
        'R', 'E', 'P', 'O', 'R', 'T', ' ', ' ', 'T', 'X', 'T', 0x20,
        0, 0, 0, 0, 0, 0, 0, 0, 0x23, 0x01, 0, 0, 0, 0, 0x67, 0x45,
        0xef, 0xcd, 0xab, 0x89
    };
    const struct ios_fs_primary input = report_txt();
    struct ios_fs_primary decoded;
    struct ios_fs_primary_disk disk;
    IOS_TEST_ASSERT_STATUS(ios_fs_primary_encode(&input, &disk), IOS_OK);
    IOS_TEST_ASSERT(memcmp(&disk, expected, sizeof(expected)) == 0);
    IOS_TEST_ASSERT_STATUS(ios_fs_primary_decode(&disk, &decoded), IOS_OK);
    IOS_TEST_ASSERT(memcmp(&decoded, &input, sizeof(input)) == 0);
}

static void test_primary_rejects_invalid_layouts(void)
{
    struct ios_fs_primary value = report_txt();
    struct ios_fs_primary decoded;
    struct ios_fs_primary_disk disk;
    IOS_TEST_ASSERT_STATUS(ios_fs_primary_encode(&value, &disk), IOS_OK);
    disk.reserved = 1;
    IOS_TEST_ASSERT_STATUS(ios_fs_primary_decode(&disk, &decoded), IOS_ERROR(IOS_E_PROTOCOL));
    IOS_TEST_ASSERT_STATUS(ios_fs_primary_encode(&value, &disk), IOS_OK);
    disk.attributes = 0x08;
    IOS_TEST_ASSERT_STATUS(ios_fs_primary_decode(&disk, &decoded), IOS_ERROR(IOS_E_PROTOCOL));
    value.file_size = 0;
    IOS_TEST_ASSERT_STATUS(ios_fs_primary_encode(&value, &disk), IOS_ERROR(IOS_E_INVALID_ARGUMENT));
    value.first_cluster = 0;
    IOS_TEST_ASSERT_STATUS(ios_fs_primary_encode(&value, &disk), IOS_OK);
}

static void test_companion_exact_layout_and_pair_validation(void)
{
    static const ios_u8 expected[IOS_FS_COMPANION_RECORD_SIZE] = {
        0xf1, 1, 1, 1, 3, 0xa3, 0, 0, 'E', '7', '7', '1', 'F', '0', '4', 'F',
        0xd8, 0x01, 0x34, 0x28, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    const struct ios_fs_primary primary = report_txt();
    struct ios_fs_primary_disk primary_disk;
    struct ios_fs_companion_disk companion_disk;
    struct ios_fs_companion decoded;
    IOS_TEST_ASSERT_STATUS(ios_fs_primary_encode(&primary, &primary_disk), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_fs_companion_encode(primary.name, true, &companion_disk), IOS_OK);
    IOS_TEST_ASSERT(memcmp(&companion_disk, expected, sizeof(expected)) == 0);
    IOS_TEST_ASSERT_STATUS(ios_fs_companion_decode(&companion_disk, &decoded), IOS_OK);
    IOS_TEST_ASSERT(decoded.committed && decoded.extension_length == 3);
    IOS_TEST_ASSERT_STATUS(ios_fs_record_pair_validate(&companion_disk, &primary_disk), IOS_OK);
}

static void test_pair_rejects_integrity_and_association_mismatches(void)
{
    const struct ios_fs_primary primary = report_txt();
    struct ios_fs_primary renamed = report_txt();
    struct ios_fs_primary_disk primary_disk;
    struct ios_fs_companion_disk companion_disk;
    IOS_TEST_ASSERT_STATUS(ios_fs_primary_encode(&primary, &primary_disk), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_fs_companion_encode(primary.name, false, &companion_disk), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_record_pair_validate(&companion_disk, &primary_disk), IOS_ERROR(IOS_E_CORRUPT)
    );
    renamed.name[0] = 'S';
    IOS_TEST_ASSERT_STATUS(ios_fs_primary_encode(&renamed, &primary_disk), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_fs_companion_encode(primary.name, true, &companion_disk), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_record_pair_validate(&companion_disk, &primary_disk), IOS_ERROR(IOS_E_CORRUPT)
    );
    IOS_TEST_ASSERT_STATUS(ios_fs_companion_encode(primary.name, true, &companion_disk), IOS_OK);
    companion_disk.primary_name_checksum ^= 1;
    companion_disk.crc32[0] ^= 1;
    IOS_TEST_ASSERT_STATUS(
        ios_fs_record_pair_validate(&companion_disk, &primary_disk), IOS_ERROR(IOS_E_CORRUPT)
    );
    IOS_TEST_ASSERT_STATUS(ios_fs_companion_encode(primary.name, true, &companion_disk), IOS_OK);
    companion_disk.crc32[0] ^= 1;
    IOS_TEST_ASSERT_STATUS(
        ios_fs_record_pair_validate(&companion_disk, &primary_disk), IOS_ERROR(IOS_E_CORRUPT)
    );
}

static void test_injected_hash_collision_defers_to_authoritative_extension(void)
{
    static const ios_u8 injected_hash[IOS_FS_HASH_TEXT_SIZE] = {
        'D', 'E', 'A', 'D', 'B', 'E', 'E', 'F'
    };
    struct ios_fs_primary txt = report_txt();
    struct ios_fs_primary log = report_txt();
    struct ios_fs_primary second_txt = report_txt();
    static const ios_u8 log_extension[3] = { 'L', 'O', 'G' };
    static const ios_u8 second_base[8] = { 'S', 'E', 'C', 'O', 'N', 'D', ' ', ' ' };
    memcpy(log.name + 8, log_extension, sizeof(log_extension));
    memcpy(second_txt.name, second_base, sizeof(second_base));
    IOS_TEST_ASSERT(!ios_fs_primary_types_equal(&txt, &log, injected_hash, injected_hash));
    IOS_TEST_ASSERT(ios_fs_primary_types_equal(
        &txt, &second_txt, injected_hash, injected_hash
    ));
}

static ios_u8 *directory_slot(ios_u8 *slots, ios_size index)
{
    return slots + index * IOS_FS_PRIMARY_RECORD_SIZE;
}

static void test_directory_scan_exposes_pair_once_and_skips_deleted_slots(void)
{
    ios_u8 slots[8 * IOS_FS_PRIMARY_RECORD_SIZE] = { 0 };
    struct ios_fs_directory_entry entries[4];
    struct ios_fs_primary directory = {
        { 'D', 'O', 'C', 'S', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },
        IOS_FS_ATTRIBUTE_DIRECTORY, 2, 0
    };
    struct ios_fs_primary regular = report_txt();
    struct ios_fs_primary_disk primary_disk;
    struct ios_fs_companion_disk companion_disk;
    ios_size count;
    IOS_TEST_ASSERT_STATUS(ios_fs_primary_encode(&directory, &primary_disk), IOS_OK);
    memcpy(directory_slot(slots, 0), &primary_disk, sizeof(primary_disk));
    IOS_TEST_ASSERT_STATUS(ios_fs_companion_encode(regular.name, true, &companion_disk), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_fs_primary_encode(&regular, &primary_disk), IOS_OK);
    memcpy(directory_slot(slots, 1), &companion_disk, sizeof(companion_disk));
    memcpy(directory_slot(slots, 2), &primary_disk, sizeof(primary_disk));
    *directory_slot(slots, 3) = 0xe5;
    IOS_TEST_ASSERT_STATUS(ios_fs_directory_scan(
        slots, 8, 4, entries, IOS_ARRAY_COUNT(entries), &count
    ), IOS_OK);
    IOS_TEST_ASSERT(count == 2);
    IOS_TEST_ASSERT(entries[0].kind == IOS_FS_DIRECTORY_ENTRY_DIRECTORY);
    IOS_TEST_ASSERT(entries[0].primary_slot == 0 && entries[0].companion_slot == SIZE_MAX);
    IOS_TEST_ASSERT(entries[1].kind == IOS_FS_DIRECTORY_ENTRY_REGULAR);
    IOS_TEST_ASSERT(entries[1].companion_slot == 1 && entries[1].primary_slot == 2);
}

static void test_directory_scan_rejects_orphans_incomplete_pairs_and_duplicates(void)
{
    ios_u8 slots[4 * IOS_FS_PRIMARY_RECORD_SIZE] = { 0 };
    struct ios_fs_directory_entry entries[4];
    struct ios_fs_primary regular = report_txt();
    struct ios_fs_primary_disk primary_disk;
    struct ios_fs_companion_disk companion_disk;
    ios_size count;
    IOS_TEST_ASSERT_STATUS(ios_fs_primary_encode(&regular, &primary_disk), IOS_OK);
    memcpy(directory_slot(slots, 0), &primary_disk, sizeof(primary_disk));
    IOS_TEST_ASSERT_STATUS(ios_fs_directory_scan(
        slots, 4, 4, entries, IOS_ARRAY_COUNT(entries), &count
    ), IOS_ERROR(IOS_E_CORRUPT));

    memset(slots, 0, sizeof(slots));
    IOS_TEST_ASSERT_STATUS(ios_fs_companion_encode(regular.name, false, &companion_disk), IOS_OK);
    memcpy(directory_slot(slots, 0), &companion_disk, sizeof(companion_disk));
    memcpy(directory_slot(slots, 1), &primary_disk, sizeof(primary_disk));
    IOS_TEST_ASSERT_STATUS(ios_fs_directory_scan(
        slots, 4, 4, entries, IOS_ARRAY_COUNT(entries), &count
    ), IOS_ERROR(IOS_E_CORRUPT));

    memset(slots, 0, sizeof(slots));
    IOS_TEST_ASSERT_STATUS(ios_fs_companion_encode(regular.name, true, &companion_disk), IOS_OK);
    memcpy(directory_slot(slots, 0), &companion_disk, sizeof(companion_disk));
    memcpy(directory_slot(slots, 1), &primary_disk, sizeof(primary_disk));
    memcpy(directory_slot(slots, 2), &companion_disk, sizeof(companion_disk));
    memcpy(directory_slot(slots, 3), &primary_disk, sizeof(primary_disk));
    IOS_TEST_ASSERT_STATUS(ios_fs_directory_scan(
        slots, 4, 4, entries, IOS_ARRAY_COUNT(entries), &count
    ), IOS_ERROR(IOS_E_ALREADY_EXISTS));
}

static void test_pair_allocation_never_crosses_cluster_boundary(void)
{
    ios_u8 slots[8 * IOS_FS_PRIMARY_RECORD_SIZE];
    ios_size slot;
    memset(slots, 0x7f, sizeof(slots));
    *directory_slot(slots, 3) = 0xe5;
    *directory_slot(slots, 4) = 0xe5;
    *directory_slot(slots, 5) = 0xe5;
    IOS_TEST_ASSERT_STATUS(ios_fs_directory_find_pair_slots(slots, 8, 4, &slot), IOS_OK);
    IOS_TEST_ASSERT(slot == 4);
    memset(slots, 0x7f, sizeof(slots));
    *directory_slot(slots, 3) = 0;
    IOS_TEST_ASSERT_STATUS(ios_fs_directory_find_pair_slots(slots, 8, 4, &slot), IOS_OK);
    IOS_TEST_ASSERT(slot == 4);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_primary_exact_layout_and_round_trip),
    IOS_TEST_CASE(test_primary_rejects_invalid_layouts),
    IOS_TEST_CASE(test_companion_exact_layout_and_pair_validation),
    IOS_TEST_CASE(test_pair_rejects_integrity_and_association_mismatches),
    IOS_TEST_CASE(test_injected_hash_collision_defers_to_authoritative_extension),
    IOS_TEST_CASE(test_directory_scan_exposes_pair_once_and_skips_deleted_slots),
    IOS_TEST_CASE(test_directory_scan_rejects_orphans_incomplete_pairs_and_duplicates),
    IOS_TEST_CASE(test_pair_allocation_never_crosses_cluster_boundary)
};
const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
