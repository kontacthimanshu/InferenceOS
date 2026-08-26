#include <inferenceos/test.h>

#include <inferenceos/fs/registry.h>

#include <stddef.h>
#include <string.h>

static void make_pair(
    const ios_u8 base[8],
    const ios_u8 extension[IOS_FS_EXTENSION_SIZE],
    ios_size extension_length,
    struct ios_fs_companion_disk *companion,
    struct ios_fs_primary_disk *primary
)
{
    struct ios_fs_primary value = {
        { ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },
        IOS_FS_ATTRIBUTE_REGULAR, 0, 0
    };
    IOS_TEST_ASSERT(extension_length <= IOS_FS_EXTENSION_SIZE);
    memcpy(value.name, base, 8);
    if (extension_length != 0) memcpy(value.name + 8, extension, extension_length);
    IOS_TEST_ASSERT_STATUS(ios_fs_primary_encode(&value, primary), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_fs_companion_encode(value.name, true, companion), IOS_OK);
}

static void make_txt_pair(
    const ios_u8 base[8],
    struct ios_fs_companion_disk *companion,
    struct ios_fs_primary_disk *primary
)
{
    static const ios_u8 txt[IOS_FS_EXTENSION_SIZE] = { 'T', 'X', 'T' };
    make_pair(base, txt, IOS_ARRAY_COUNT(txt), companion, primary);
}

static void test_registry_record_exact_layout_and_round_trip(void)
{
    static const ios_u8 expected_extension[IOS_FS_EXTENSION_SIZE] = { 'T', 'X', 'T' };
    static const ios_u8 expected_hash[IOS_FS_HASH_TEXT_SIZE] = {
        'E', '7', '7', '1', 'F', '0', '4', 'F'
    };
    const struct ios_fs_registry_record value = {
        .active = true,
        .extension_length = 3,
        .canonical_extension = { 'T', 'X', 'T' },
        .last_directory_cluster = UINT32_C(0x11223344),
        .last_directory_slot = UINT16_C(0x5566),
        .update_generation = UINT16_C(0x7788)
    };
    struct ios_fs_registry_record_disk disk;
    struct ios_fs_registry_record decoded;

    IOS_TEST_ASSERT(sizeof(disk) == 32);
    IOS_TEST_ASSERT(offsetof(struct ios_fs_registry_record_disk, record_type) == 0x00);
    IOS_TEST_ASSERT(offsetof(struct ios_fs_registry_record_disk, record_version) == 0x01);
    IOS_TEST_ASSERT(offsetof(struct ios_fs_registry_record_disk, flags) == 0x02);
    IOS_TEST_ASSERT(offsetof(struct ios_fs_registry_record_disk, extension_length) == 0x03);
    IOS_TEST_ASSERT(offsetof(struct ios_fs_registry_record_disk, hash_algorithm_id) == 0x04);
    IOS_TEST_ASSERT(offsetof(struct ios_fs_registry_record_disk, canonical_extension) == 0x08);
    IOS_TEST_ASSERT(offsetof(struct ios_fs_registry_record_disk, extension_hash_text) == 0x0c);
    IOS_TEST_ASSERT(offsetof(struct ios_fs_registry_record_disk, last_directory_cluster) == 0x14);
    IOS_TEST_ASSERT(offsetof(struct ios_fs_registry_record_disk, last_directory_slot) == 0x18);
    IOS_TEST_ASSERT(offsetof(struct ios_fs_registry_record_disk, update_generation) == 0x1a);
    IOS_TEST_ASSERT(offsetof(struct ios_fs_registry_record_disk, crc32) == 0x1c);

    IOS_TEST_ASSERT_STATUS(ios_fs_registry_record_encode(&value, &disk), IOS_OK);
    IOS_TEST_ASSERT(disk.record_type == IOS_FS_REGISTRY_RECORD_TYPE);
    IOS_TEST_ASSERT(disk.record_version == IOS_FS_REGISTRY_VERSION);
    IOS_TEST_ASSERT(disk.flags == IOS_FS_REGISTRY_FLAG_ACTIVE);
    IOS_TEST_ASSERT(disk.extension_length == 3);
    IOS_TEST_ASSERT(disk.hash_algorithm_id == IOS_FS_HASH_FNV1A32);
    IOS_TEST_ASSERT(memcmp(disk.reserved0, "\0\0\0", sizeof(disk.reserved0)) == 0);
    IOS_TEST_ASSERT(memcmp(disk.canonical_extension, expected_extension, 3) == 0);
    IOS_TEST_ASSERT(disk.reserved1 == 0);
    IOS_TEST_ASSERT(memcmp(disk.extension_hash_text, expected_hash, sizeof(expected_hash)) == 0);
    IOS_TEST_ASSERT(disk.last_directory_cluster[0] == 0x44);
    IOS_TEST_ASSERT(disk.last_directory_cluster[3] == 0x11);
    IOS_TEST_ASSERT(disk.last_directory_slot[0] == 0x66);
    IOS_TEST_ASSERT(disk.last_directory_slot[1] == 0x55);
    IOS_TEST_ASSERT(disk.update_generation[0] == 0x88);
    IOS_TEST_ASSERT(disk.update_generation[1] == 0x77);

    IOS_TEST_ASSERT_STATUS(ios_fs_registry_record_decode(&disk, &decoded), IOS_OK);
    IOS_TEST_ASSERT(decoded.active);
    IOS_TEST_ASSERT(decoded.extension_length == 3);
    IOS_TEST_ASSERT(memcmp(decoded.canonical_extension, expected_extension, 3) == 0);
    IOS_TEST_ASSERT(memcmp(decoded.extension_hash_text, expected_hash, sizeof(expected_hash)) == 0);
    IOS_TEST_ASSERT(decoded.last_directory_cluster == UINT32_C(0x11223344));
    IOS_TEST_ASSERT(decoded.last_directory_slot == UINT16_C(0x5566));
    IOS_TEST_ASSERT(decoded.update_generation == UINT16_C(0x7788));
}

static void test_registry_record_rejects_version_reserved_hash_and_crc_corruption(void)
{
    const struct ios_fs_registry_record value = {
        .active = true,
        .extension_length = 3,
        .canonical_extension = { 'T', 'X', 'T' },
        .last_directory_cluster = 7,
        .last_directory_slot = 9,
        .update_generation = 1
    };
    struct ios_fs_registry_record_disk disk;
    struct ios_fs_registry_record decoded;

    IOS_TEST_ASSERT_STATUS(ios_fs_registry_record_encode(&value, &disk), IOS_OK);
    disk.record_version = IOS_FS_REGISTRY_VERSION + 1;
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_record_decode(&disk, &decoded), IOS_ERROR(IOS_E_UNSUPPORTED_VERSION)
    );
    IOS_TEST_ASSERT_STATUS(ios_fs_registry_record_encode(&value, &disk), IOS_OK);
    disk.reserved0[1] = 1;
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_record_decode(&disk, &decoded), IOS_ERROR(IOS_E_CORRUPT)
    );
    IOS_TEST_ASSERT_STATUS(ios_fs_registry_record_encode(&value, &disk), IOS_OK);
    disk.extension_hash_text[0] = '0';
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_record_decode(&disk, &decoded), IOS_ERROR(IOS_E_CORRUPT)
    );
    IOS_TEST_ASSERT_STATUS(ios_fs_registry_record_encode(&value, &disk), IOS_OK);
    disk.crc32[0] ^= 1;
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_record_decode(&disk, &decoded), IOS_ERROR(IOS_E_CORRUPT)
    );
}

static void test_first_type_creates_and_same_type_refreshes_one_record(void)
{
    static const ios_u8 report[8] = { 'R', 'E', 'P', 'O', 'R', 'T', ' ', ' ' };
    static const ios_u8 second[8] = { 'S', 'E', 'C', 'O', 'N', 'D', ' ', ' ' };
    static const ios_u8 event[8] = { 'E', 'V', 'E', 'N', 'T', ' ', ' ', ' ' };
    static const ios_u8 log[IOS_FS_EXTENSION_SIZE] = { 'L', 'O', 'G' };
    struct ios_fs_registry_record_disk storage[4] = { 0 };
    struct ios_fs_registry registry;
    struct ios_fs_companion_disk companion;
    struct ios_fs_primary_disk primary;
    struct ios_fs_registry_record record;
    ios_size record_index;

    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_initialize(&registry, storage, IOS_ARRAY_COUNT(storage), true), IOS_OK
    );
    make_txt_pair(report, &companion, &primary);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_refresh(&registry, &companion, &primary, 10, 2, &record_index), IOS_OK
    );
    IOS_TEST_ASSERT(record_index == 0);
    IOS_TEST_ASSERT(ios_fs_registry_active_count(&registry) == 1);

    make_txt_pair(second, &companion, &primary);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_refresh(&registry, &companion, &primary, 12, 6, &record_index), IOS_OK
    );
    IOS_TEST_ASSERT(record_index == 0);
    IOS_TEST_ASSERT(ios_fs_registry_active_count(&registry) == 1);
    IOS_TEST_ASSERT_STATUS(ios_fs_registry_record_decode(&storage[0], &record), IOS_OK);
    IOS_TEST_ASSERT(record.last_directory_cluster == 12);
    IOS_TEST_ASSERT(record.last_directory_slot == 6);
    IOS_TEST_ASSERT(record.update_generation == 2);

    make_pair(event, log, IOS_ARRAY_COUNT(log), &companion, &primary);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_refresh(&registry, &companion, &primary, 15, 8, &record_index), IOS_OK
    );
    IOS_TEST_ASSERT(record_index == 1);
    IOS_TEST_ASSERT(ios_fs_registry_active_count(&registry) == 2);
}

static void test_rebuild_deduplicates_committed_entry_sets_and_replaces_stale_state(void)
{
    static const ios_u8 report[8] = { 'R', 'E', 'P', 'O', 'R', 'T', ' ', ' ' };
    static const ios_u8 second[8] = { 'S', 'E', 'C', 'O', 'N', 'D', ' ', ' ' };
    static const ios_u8 event[8] = { 'E', 'V', 'E', 'N', 'T', ' ', ' ', ' ' };
    static const ios_u8 log[IOS_FS_EXTENSION_SIZE] = { 'L', 'O', 'G' };
    const struct ios_fs_registry_record stale = {
        .active = true,
        .extension_length = 3,
        .canonical_extension = { 'B', 'I', 'N' },
        .last_directory_cluster = 5,
        .last_directory_slot = 1,
        .update_generation = 9
    };
    struct ios_fs_registry_record_disk storage[4] = { 0 };
    struct ios_fs_registry registry;
    struct ios_fs_registry_source_entry entries[3];
    struct ios_fs_registry_record txt_record;
    struct ios_fs_registry_record log_record;
    ios_size txt_index;
    ios_size log_index;

    IOS_TEST_ASSERT_STATUS(ios_fs_registry_record_encode(&stale, &storage[0]), IOS_OK);
    memset(entries, 0, sizeof(entries));
    make_txt_pair(report, &entries[0].companion, &entries[0].primary);
    entries[0].directory_cluster = 20;
    entries[0].primary_slot = 2;
    make_pair(event, log, IOS_ARRAY_COUNT(log), &entries[1].companion, &entries[1].primary);
    entries[1].directory_cluster = 21;
    entries[1].primary_slot = 4;
    make_txt_pair(second, &entries[2].companion, &entries[2].primary);
    entries[2].directory_cluster = 22;
    entries[2].primary_slot = 6;

    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_initialize(&registry, storage, IOS_ARRAY_COUNT(storage), true), IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_rebuild(&registry, entries, IOS_ARRAY_COUNT(entries)), IOS_OK
    );
    IOS_TEST_ASSERT(ios_fs_registry_active_count(&registry) == 2);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_find(&registry, (const ios_u8 *)"TXT", 3, &txt_index, &txt_record), IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_find(&registry, (const ios_u8 *)"LOG", 3, &log_index, &log_record), IOS_OK
    );
    IOS_TEST_ASSERT(txt_index != log_index);
    IOS_TEST_ASSERT(txt_record.last_directory_cluster == 22);
    IOS_TEST_ASSERT(txt_record.last_directory_slot == 6);
    IOS_TEST_ASSERT(txt_record.update_generation == 2);
    IOS_TEST_ASSERT(log_record.last_directory_cluster == 21);
    IOS_TEST_ASSERT(log_record.last_directory_slot == 4);
    IOS_TEST_ASSERT(log_record.update_generation == 1);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_registry_record_exact_layout_and_round_trip),
    IOS_TEST_CASE(test_registry_record_rejects_version_reserved_hash_and_crc_corruption),
    IOS_TEST_CASE(test_first_type_creates_and_same_type_refreshes_one_record),
    IOS_TEST_CASE(test_rebuild_deduplicates_committed_entry_sets_and_replaces_stale_state)
};

const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
