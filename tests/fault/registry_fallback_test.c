#include <inferenceos/test.h>

#include <inferenceos/fs/registry.h>

#include <string.h>

static void make_pair(
    const ios_u8 base[8],
    const ios_u8 extension[IOS_FS_EXTENSION_SIZE],
    ios_size extension_length,
    ios_u32 directory_cluster,
    ios_u16 primary_slot,
    struct ios_fs_registry_source_entry *entry
)
{
    struct ios_fs_primary value = {
        { ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' },
        IOS_FS_ATTRIBUTE_REGULAR, 0, 0
    };
    IOS_TEST_ASSERT(extension_length <= IOS_FS_EXTENSION_SIZE);
    memset(entry, 0, sizeof(*entry));
    memcpy(value.name, base, 8);
    if (extension_length != 0) memcpy(value.name + 8, extension, extension_length);
    IOS_TEST_ASSERT_STATUS(ios_fs_primary_encode(&value, &entry->primary), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_companion_encode(value.name, true, &entry->companion), IOS_OK
    );
    entry->directory_cluster = directory_cluster;
    entry->primary_slot = primary_slot;
}

static void make_txt(
    const ios_u8 base[8],
    ios_u32 directory_cluster,
    ios_u16 primary_slot,
    struct ios_fs_registry_source_entry *entry
)
{
    static const ios_u8 txt[IOS_FS_EXTENSION_SIZE] = { 'T', 'X', 'T' };
    make_pair(base, txt, IOS_ARRAY_COUNT(txt), directory_cluster, primary_slot, entry);
}

static void make_log(
    const ios_u8 base[8],
    ios_u32 directory_cluster,
    ios_u16 primary_slot,
    struct ios_fs_registry_source_entry *entry
)
{
    static const ios_u8 log[IOS_FS_EXTENSION_SIZE] = { 'L', 'O', 'G' };
    make_pair(base, log, IOS_ARRAY_COUNT(log), directory_cluster, primary_slot, entry);
}

static ios_status lookup_txt(
    struct ios_fs_registry *registry,
    const struct ios_fs_registry_source_entry *entries,
    ios_size entry_count,
    ios_size *matches,
    ios_size match_capacity,
    ios_size *match_count
)
{
    static const ios_u8 txt[IOS_FS_EXTENSION_SIZE] = { 'T', 'X', 'T' };
    return ios_fs_registry_lookup(
        registry, entries, entry_count, txt, IOS_ARRAY_COUNT(txt),
        matches, match_capacity, match_count
    );
}

static void test_registry_is_disabled_by_default_and_authoritative_lookup_still_works(void)
{
    static const ios_u8 report[8] = { 'R', 'E', 'P', 'O', 'R', 'T', ' ', ' ' };
    struct ios_fs_registry_record_disk storage[2];
    struct ios_fs_registry_record_disk before[2];
    struct ios_fs_registry_source_entry entries[1];
    struct ios_fs_registry registry;
    ios_size matches[1] = { SIZE_MAX };
    ios_size match_count = 0;

    IOS_TEST_ASSERT(!IOS_FS_REGISTRY_DEFAULT_ENABLED);
    memset(storage, 0xa5, sizeof(storage));
    memcpy(before, storage, sizeof(before));
    make_txt(report, 30, 2, &entries[0]);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_initialize(
            &registry, storage, IOS_ARRAY_COUNT(storage), IOS_FS_REGISTRY_DEFAULT_ENABLED
        ),
        IOS_OK
    );
    IOS_TEST_ASSERT(!ios_fs_registry_enabled(&registry));
    IOS_TEST_ASSERT(ios_fs_registry_health(&registry) == IOS_FS_REGISTRY_DISABLED);
    IOS_TEST_ASSERT_STATUS(
        lookup_txt(
            &registry, entries, IOS_ARRAY_COUNT(entries),
            matches, IOS_ARRAY_COUNT(matches), &match_count
        ),
        IOS_OK
    );
    IOS_TEST_ASSERT(match_count == 1 && matches[0] == 0);
    IOS_TEST_ASSERT(memcmp(before, storage, sizeof(before)) == 0);
}

static void test_full_registry_falls_back_without_overwriting_existing_type(void)
{
    static const ios_u8 report[8] = { 'R', 'E', 'P', 'O', 'R', 'T', ' ', ' ' };
    static const ios_u8 event[8] = { 'E', 'V', 'E', 'N', 'T', ' ', ' ', ' ' };
    struct ios_fs_registry_record_disk storage[1] = { 0 };
    struct ios_fs_registry_record_disk before[1];
    struct ios_fs_registry_source_entry entries[2];
    struct ios_fs_registry registry;
    ios_size matches[2] = { SIZE_MAX, SIZE_MAX };
    ios_size match_count = 0;
    ios_size record_index;

    make_txt(report, 40, 2, &entries[0]);
    make_log(event, 41, 4, &entries[1]);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_initialize(&registry, storage, IOS_ARRAY_COUNT(storage), true), IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_refresh(
            &registry, &entries[0].companion, &entries[0].primary,
            entries[0].directory_cluster, entries[0].primary_slot, &record_index
        ),
        IOS_OK
    );
    memcpy(before, storage, sizeof(before));
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_refresh(
            &registry, &entries[1].companion, &entries[1].primary,
            entries[1].directory_cluster, entries[1].primary_slot, &record_index
        ),
        IOS_ERROR(IOS_E_NO_SPACE)
    );
    IOS_TEST_ASSERT(ios_fs_registry_health(&registry) == IOS_FS_REGISTRY_FULL);
    IOS_TEST_ASSERT(memcmp(before, storage, sizeof(before)) == 0);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_lookup(
            &registry, entries, IOS_ARRAY_COUNT(entries), (const ios_u8 *)"LOG", 3,
            matches, IOS_ARRAY_COUNT(matches), &match_count
        ),
        IOS_OK
    );
    IOS_TEST_ASSERT(match_count == 1 && matches[0] == 1);
    IOS_TEST_ASSERT(memcmp(before, storage, sizeof(before)) == 0);
}

static void test_corrupt_registry_after_reboot_is_nonfatal_and_not_repaired_implicitly(void)
{
    static const ios_u8 report[8] = { 'R', 'E', 'P', 'O', 'R', 'T', ' ', ' ' };
    struct ios_fs_registry_record_disk storage[2] = { 0 };
    struct ios_fs_registry_record_disk before[2];
    struct ios_fs_registry_source_entry entries[1];
    struct ios_fs_registry writer;
    struct ios_fs_registry rebooted;
    ios_size matches[1] = { SIZE_MAX };
    ios_size match_count = 0;
    ios_size record_index;

    make_txt(report, 50, 2, &entries[0]);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_initialize(&writer, storage, IOS_ARRAY_COUNT(storage), true), IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_refresh(
            &writer, &entries[0].companion, &entries[0].primary,
            entries[0].directory_cluster, entries[0].primary_slot, &record_index
        ),
        IOS_OK
    );
    storage[0].crc32[0] ^= 1;
    memcpy(before, storage, sizeof(before));
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_initialize(&rebooted, storage, IOS_ARRAY_COUNT(storage), true), IOS_OK
    );
    IOS_TEST_ASSERT(ios_fs_registry_health(&rebooted) == IOS_FS_REGISTRY_CORRUPT);
    IOS_TEST_ASSERT_STATUS(
        lookup_txt(
            &rebooted, entries, IOS_ARRAY_COUNT(entries),
            matches, IOS_ARRAY_COUNT(matches), &match_count
        ),
        IOS_OK
    );
    IOS_TEST_ASSERT(match_count == 1 && matches[0] == 0);
    IOS_TEST_ASSERT(memcmp(before, storage, sizeof(before)) == 0);
}

static void test_stale_location_after_reboot_uses_authoritative_entry_and_marks_stale(void)
{
    static const ios_u8 report[8] = { 'R', 'E', 'P', 'O', 'R', 'T', ' ', ' ' };
    static const ios_u8 second[8] = { 'S', 'E', 'C', 'O', 'N', 'D', ' ', ' ' };
    struct ios_fs_registry_record_disk storage[2] = { 0 };
    struct ios_fs_registry_record_disk before[2];
    struct ios_fs_registry_source_entry original;
    struct ios_fs_registry_source_entry reboot_entries[2];
    struct ios_fs_registry writer;
    struct ios_fs_registry rebooted;
    ios_size matches[2] = { SIZE_MAX, SIZE_MAX };
    ios_size match_count = 0;
    ios_size record_index;

    make_txt(report, 60, 2, &original);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_initialize(&writer, storage, IOS_ARRAY_COUNT(storage), true), IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_refresh(
            &writer, &original.companion, &original.primary,
            original.directory_cluster, original.primary_slot, &record_index
        ),
        IOS_OK
    );
    make_log(report, 60, 2, &reboot_entries[0]);
    make_txt(second, 61, 4, &reboot_entries[1]);
    memcpy(before, storage, sizeof(before));
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_initialize(&rebooted, storage, IOS_ARRAY_COUNT(storage), true), IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(
        lookup_txt(
            &rebooted, reboot_entries, IOS_ARRAY_COUNT(reboot_entries),
            matches, IOS_ARRAY_COUNT(matches), &match_count
        ),
        IOS_OK
    );
    IOS_TEST_ASSERT(match_count == 1 && matches[0] == 1);
    IOS_TEST_ASSERT(ios_fs_registry_health(&rebooted) == IOS_FS_REGISTRY_STALE);
    IOS_TEST_ASSERT(memcmp(before, storage, sizeof(before)) == 0);
}

static void test_stale_registry_hint_never_creates_a_false_authoritative_match(void)
{
    static const ios_u8 report[8] = { 'R', 'E', 'P', 'O', 'R', 'T', ' ', ' ' };
    struct ios_fs_registry_record_disk storage[1] = { 0 };
    struct ios_fs_registry_record_disk before[1];
    struct ios_fs_registry_source_entry txt_entry;
    struct ios_fs_registry_source_entry authoritative_log;
    struct ios_fs_registry registry;
    ios_size matches[1] = { SIZE_MAX };
    ios_size match_count = SIZE_MAX;
    ios_size record_index;

    make_txt(report, 70, 2, &txt_entry);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_initialize(&registry, storage, IOS_ARRAY_COUNT(storage), true), IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_refresh(
            &registry, &txt_entry.companion, &txt_entry.primary,
            txt_entry.directory_cluster, txt_entry.primary_slot, &record_index
        ),
        IOS_OK
    );
    make_log(report, 70, 2, &authoritative_log);
    memcpy(before, storage, sizeof(before));
    IOS_TEST_ASSERT_STATUS(
        lookup_txt(&registry, &authoritative_log, 1, matches, 1, &match_count), IOS_OK
    );
    IOS_TEST_ASSERT(match_count == 0);
    IOS_TEST_ASSERT(matches[0] == SIZE_MAX);
    IOS_TEST_ASSERT(ios_fs_registry_health(&registry) == IOS_FS_REGISTRY_STALE);
    IOS_TEST_ASSERT(memcmp(before, storage, sizeof(before)) == 0);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(
        test_registry_is_disabled_by_default_and_authoritative_lookup_still_works
    ),
    IOS_TEST_CASE(test_full_registry_falls_back_without_overwriting_existing_type),
    IOS_TEST_CASE(
        test_corrupt_registry_after_reboot_is_nonfatal_and_not_repaired_implicitly
    ),
    IOS_TEST_CASE(
        test_stale_location_after_reboot_uses_authoritative_entry_and_marks_stale
    ),
    IOS_TEST_CASE(test_stale_registry_hint_never_creates_a_false_authoritative_match)
};

const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
