#include <inferenceos/test.h>

#include <inferenceos/fs/directory.h>
#include <inferenceos/fs/subdirectory.h>

#include <string.h>

enum {
    TEST_CLUSTER_COUNT = 2,
    TEST_SLOT_COUNT = IOS_FS_DIRECTORY_SLOTS_PER_CLUSTER * TEST_CLUSTER_COUNT,
    STORE_CLUSTER_COUNT = 16,
    STORE_CLUSTER_BYTES = IOS_FS_SECTOR_SIZE * IOS_FS_SECTORS_PER_CLUSTER
};

struct cluster_fixture {
    ios_u8 clusters[STORE_CLUSTER_COUNT][STORE_CLUSTER_BYTES];
    ios_u32 reads;
    ios_u32 writes;
    ios_u32 zeroes;
};

static ios_status read_cluster(void *context, ios_u32 cluster, void *buffer)
{
    struct cluster_fixture *fixture = context;
    if (cluster >= STORE_CLUSTER_COUNT) return IOS_ERROR(IOS_E_OUT_OF_RANGE);
    memcpy(buffer, fixture->clusters[cluster], STORE_CLUSTER_BYTES);
    ++fixture->reads;
    return IOS_OK;
}

static ios_status write_cluster(void *context, ios_u32 cluster, const void *buffer)
{
    struct cluster_fixture *fixture = context;
    if (cluster >= STORE_CLUSTER_COUNT) return IOS_ERROR(IOS_E_OUT_OF_RANGE);
    memcpy(fixture->clusters[cluster], buffer, STORE_CLUSTER_BYTES);
    ++fixture->writes;
    return IOS_OK;
}

static ios_status zero_cluster(void *context, ios_u32 cluster)
{
    struct cluster_fixture *fixture = context;
    if (cluster >= STORE_CLUSTER_COUNT) return IOS_ERROR(IOS_E_OUT_OF_RANGE);
    memset(fixture->clusters[cluster], 0, STORE_CLUSTER_BYTES);
    ++fixture->zeroes;
    return IOS_OK;
}

static struct ios_fs_directory_store initialize_store(
    struct cluster_fixture *fixture, ios_u32 fat[STORE_CLUSTER_COUNT]
)
{
    memset(fixture, 0, sizeof(*fixture));
    memset(fat, 0, sizeof(ios_u32) * STORE_CLUSTER_COUNT);
    fat[0] = IOS_FS_FAT_END_OF_CHAIN;
    fat[1] = IOS_FS_FAT_END_OF_CHAIN;
    fat[2] = IOS_FS_FAT_END_OF_CHAIN;
    fat[3] = IOS_FS_FAT_END_OF_CHAIN;
    return (struct ios_fs_directory_store){
        fat, STORE_CLUSTER_COUNT, STORE_CLUSTER_BYTES, fixture,
        { read_cluster, write_cluster, zero_cluster }, true
    };
}

static ios_u8 *slot_at(ios_u8 *slots, ios_size slot)
{
    return slots + slot * IOS_FS_PRIMARY_RECORD_SIZE;
}

static struct ios_fs_primary regular_file(ios_u8 suffix, const ios_u8 extension[3])
{
    struct ios_fs_primary primary = {
        { 'F', 'I', 'L', 'E', '0', '0', '0', '0', ' ', ' ', ' ' },
        IOS_FS_ATTRIBUTE_REGULAR, 2, 1
    };
    primary.name[7] = suffix;
    memcpy(primary.name + 8, extension, 3);
    return primary;
}

static void write_pair(ios_u8 *slots, ios_size companion_slot, struct ios_fs_primary primary)
{
    struct ios_fs_companion_disk companion;
    struct ios_fs_primary_disk primary_disk;
    IOS_TEST_ASSERT_STATUS(
        ios_fs_companion_encode(primary.name, true, &companion), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_fs_primary_encode(&primary, &primary_disk), IOS_OK);
    memcpy(slot_at(slots, companion_slot), &companion, sizeof(companion));
    memcpy(slot_at(slots, companion_slot + 1), &primary_disk, sizeof(primary_disk));
}

static void test_full_directory_requires_growth_then_allocates_in_new_cluster(void)
{
    ios_u8 slots[TEST_SLOT_COUNT * IOS_FS_PRIMARY_RECORD_SIZE];
    ios_size companion_slot = SIZE_MAX;
    memset(slots, 0x7f, sizeof(slots));
    IOS_TEST_ASSERT_STATUS(ios_fs_directory_find_pair_slots(
        slots, IOS_FS_DIRECTORY_SLOTS_PER_CLUSTER,
        IOS_FS_DIRECTORY_SLOTS_PER_CLUSTER, &companion_slot), IOS_ERROR(IOS_E_NO_SPACE));

    memset(slot_at(slots, IOS_FS_DIRECTORY_SLOTS_PER_CLUSTER), 0,
           IOS_FS_SECTOR_SIZE * IOS_FS_SECTORS_PER_CLUSTER);
    IOS_TEST_ASSERT_STATUS(ios_fs_directory_find_pair_slots(
        slots, TEST_SLOT_COUNT, IOS_FS_DIRECTORY_SLOTS_PER_CLUSTER,
        &companion_slot), IOS_OK);
    IOS_TEST_ASSERT(companion_slot == IOS_FS_DIRECTORY_SLOTS_PER_CLUSTER);
}

static void test_pair_allocation_never_spans_growth_boundary(void)
{
    ios_u8 slots[TEST_SLOT_COUNT * IOS_FS_PRIMARY_RECORD_SIZE];
    const ios_size last_first = IOS_FS_DIRECTORY_SLOTS_PER_CLUSTER - 1;
    ios_size companion_slot = SIZE_MAX;
    memset(slots, 0x7f, sizeof(slots));
    *slot_at(slots, last_first) = 0xe5;
    *slot_at(slots, last_first + 1) = 0xe5;
    IOS_TEST_ASSERT_STATUS(ios_fs_directory_find_pair_slots(
        slots, TEST_SLOT_COUNT, IOS_FS_DIRECTORY_SLOTS_PER_CLUSTER,
        &companion_slot), IOS_ERROR(IOS_E_NO_SPACE));

    *slot_at(slots, last_first + 2) = 0xe5;
    IOS_TEST_ASSERT_STATUS(ios_fs_directory_find_pair_slots(
        slots, TEST_SLOT_COUNT, IOS_FS_DIRECTORY_SLOTS_PER_CLUSTER,
        &companion_slot), IOS_OK);
    IOS_TEST_ASSERT(companion_slot == IOS_FS_DIRECTORY_SLOTS_PER_CLUSTER);
}

static void test_scan_exposes_pairs_on_both_sides_of_growth_boundary_once(void)
{
    static const ios_u8 txt[3] = { 'T', 'X', 'T' };
    static const ios_u8 log[3] = { 'L', 'O', 'G' };
    ios_u8 slots[TEST_SLOT_COUNT * IOS_FS_PRIMARY_RECORD_SIZE];
    struct ios_fs_directory_entry entries[2];
    ios_size entry_count = 0;
    memset(slots, 0xe5, sizeof(slots));
    write_pair(slots, IOS_FS_DIRECTORY_SLOTS_PER_CLUSTER - 2, regular_file('1', txt));
    write_pair(slots, IOS_FS_DIRECTORY_SLOTS_PER_CLUSTER, regular_file('2', log));
    IOS_TEST_ASSERT_STATUS(ios_fs_directory_scan(
        slots, TEST_SLOT_COUNT, IOS_FS_DIRECTORY_SLOTS_PER_CLUSTER,
        entries, IOS_ARRAY_COUNT(entries), &entry_count), IOS_OK);
    IOS_TEST_ASSERT(entry_count == 2);
    IOS_TEST_ASSERT(entries[0].companion_slot == IOS_FS_DIRECTORY_SLOTS_PER_CLUSTER - 2);
    IOS_TEST_ASSERT(entries[0].primary_slot == IOS_FS_DIRECTORY_SLOTS_PER_CLUSTER - 1);
    IOS_TEST_ASSERT(entries[1].companion_slot == IOS_FS_DIRECTORY_SLOTS_PER_CLUSTER);
    IOS_TEST_ASSERT(entries[1].primary_slot == IOS_FS_DIRECTORY_SLOTS_PER_CLUSTER + 1);
}

static void test_scan_rejects_companion_at_cluster_boundary(void)
{
    static const ios_u8 txt[3] = { 'T', 'X', 'T' };
    ios_u8 slots[TEST_SLOT_COUNT * IOS_FS_PRIMARY_RECORD_SIZE];
    struct ios_fs_directory_entry entry;
    struct ios_fs_primary primary = regular_file('3', txt);
    struct ios_fs_companion_disk companion;
    struct ios_fs_primary_disk primary_disk;
    ios_size entry_count = 99;
    memset(slots, 0xe5, sizeof(slots));
    IOS_TEST_ASSERT_STATUS(
        ios_fs_companion_encode(primary.name, true, &companion), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_fs_primary_encode(&primary, &primary_disk), IOS_OK);
    memcpy(slot_at(slots, IOS_FS_DIRECTORY_SLOTS_PER_CLUSTER - 1),
           &companion, sizeof(companion));
    memcpy(slot_at(slots, IOS_FS_DIRECTORY_SLOTS_PER_CLUSTER),
           &primary_disk, sizeof(primary_disk));
    IOS_TEST_ASSERT_STATUS(ios_fs_directory_scan(
        slots, TEST_SLOT_COUNT, IOS_FS_DIRECTORY_SLOTS_PER_CLUSTER,
        &entry, 1, &entry_count), IOS_ERROR(IOS_E_CORRUPT));
    IOS_TEST_ASSERT(entry_count == 0);
}

static void test_subdirectory_initializes_internal_links_and_hides_them(void)
{
    struct cluster_fixture fixture;
    ios_u32 fat[STORE_CLUSTER_COUNT];
    struct ios_fs_directory_store store = initialize_store(&fixture, fat);
    ios_u32 chain[STORE_CLUSTER_COUNT];
    ios_u8 scratch[STORE_CLUSTER_BYTES];
    struct ios_fs_directory_entry entries[2];
    ios_size count = 99;
    IOS_TEST_ASSERT_STATUS(ios_fs_subdirectory_initialize(
        &store, 3, 2, scratch, sizeof(scratch)), IOS_OK);
    IOS_TEST_ASSERT(fixture.writes == 1);
    IOS_TEST_ASSERT_STATUS(ios_fs_subdirectory_enumerate(
        &store, 3, chain, IOS_ARRAY_COUNT(chain), scratch, sizeof(scratch),
        entries, IOS_ARRAY_COUNT(entries), &count), IOS_OK);
    IOS_TEST_ASSERT(count == 0);
    IOS_TEST_ASSERT(fixture.clusters[3][0] == '.');
    IOS_TEST_ASSERT(fixture.clusters[3][IOS_FS_PRIMARY_RECORD_SIZE] == '.');
    IOS_TEST_ASSERT(fixture.clusters[3][IOS_FS_PRIMARY_RECORD_SIZE + 1] == '.');
}

static void test_subdirectory_growth_links_zeroed_cluster_and_enumerates_pair(void)
{
    static const ios_u8 txt[3] = { 'T', 'X', 'T' };
    struct cluster_fixture fixture;
    ios_u32 fat[STORE_CLUSTER_COUNT];
    struct ios_fs_directory_store store = initialize_store(&fixture, fat);
    ios_u32 chain[STORE_CLUSTER_COUNT];
    ios_u8 scratch[STORE_CLUSTER_BYTES];
    struct ios_fs_directory_entry entry;
    ios_u32 new_cluster;
    ios_size count;
    IOS_TEST_ASSERT_STATUS(ios_fs_subdirectory_initialize(
        &store, 3, 2, scratch, sizeof(scratch)), IOS_OK);
    memset(fixture.clusters[3] + 2 * IOS_FS_PRIMARY_RECORD_SIZE, 0xe5,
           STORE_CLUSTER_BYTES - 2 * IOS_FS_PRIMARY_RECORD_SIZE);
    IOS_TEST_ASSERT_STATUS(ios_fs_subdirectory_grow(
        &store, 3, chain, IOS_ARRAY_COUNT(chain), &new_cluster), IOS_OK);
    IOS_TEST_ASSERT(new_cluster == 4 && fat[3] == 4
                    && fat[4] == IOS_FS_FAT_END_OF_CHAIN && fixture.zeroes == 1);
    write_pair(fixture.clusters[new_cluster], 0, regular_file('4', txt));
    IOS_TEST_ASSERT_STATUS(ios_fs_subdirectory_enumerate(
        &store, 3, chain, IOS_ARRAY_COUNT(chain), scratch, sizeof(scratch),
        &entry, 1, &count), IOS_OK);
    IOS_TEST_ASSERT(count == 1 && entry.kind == IOS_FS_DIRECTORY_ENTRY_REGULAR);
    IOS_TEST_ASSERT(entry.companion_slot == IOS_FS_DIRECTORY_SLOTS_PER_CLUSTER);
    IOS_TEST_ASSERT(entry.primary_slot == IOS_FS_DIRECTORY_SLOTS_PER_CLUSTER + 1);
}

static void test_subdirectory_remove_rejects_nonempty_then_releases_chain(void)
{
    static const ios_u8 txt[3] = { 'T', 'X', 'T' };
    struct cluster_fixture fixture;
    ios_u32 fat[STORE_CLUSTER_COUNT];
    struct ios_fs_directory_store store = initialize_store(&fixture, fat);
    ios_u32 chain[STORE_CLUSTER_COUNT];
    ios_u8 scratch[STORE_CLUSTER_BYTES];
    IOS_TEST_ASSERT_STATUS(ios_fs_subdirectory_initialize(
        &store, 3, 2, scratch, sizeof(scratch)), IOS_OK);
    write_pair(fixture.clusters[3], 2, regular_file('5', txt));
    IOS_TEST_ASSERT_STATUS(ios_fs_subdirectory_remove(
        &store, 3, 2, chain, IOS_ARRAY_COUNT(chain), scratch, sizeof(scratch)),
        IOS_ERROR(IOS_E_NOT_EMPTY));
    IOS_TEST_ASSERT(fat[3] == IOS_FS_FAT_END_OF_CHAIN);
    *slot_at(fixture.clusters[3], 2) = 0xe5;
    *slot_at(fixture.clusters[3], 3) = 0xe5;
    IOS_TEST_ASSERT_STATUS(ios_fs_subdirectory_remove(
        &store, 3, 2, chain, IOS_ARRAY_COUNT(chain), scratch, sizeof(scratch)), IOS_OK);
    IOS_TEST_ASSERT(fat[3] == IOS_FS_FAT_FREE);
}

static void test_subdirectory_remove_rejects_malformed_parent_link(void)
{
    struct cluster_fixture fixture;
    ios_u32 fat[STORE_CLUSTER_COUNT];
    struct ios_fs_directory_store store = initialize_store(&fixture, fat);
    ios_u32 chain[STORE_CLUSTER_COUNT];
    ios_u8 scratch[STORE_CLUSTER_BYTES];
    IOS_TEST_ASSERT_STATUS(ios_fs_subdirectory_initialize(
        &store, 3, 2, scratch, sizeof(scratch)), IOS_OK);
    fixture.clusters[3][IOS_FS_PRIMARY_RECORD_SIZE + 0x1a] = 7;
    IOS_TEST_ASSERT_STATUS(ios_fs_subdirectory_remove(
        &store, 3, 2, chain, IOS_ARRAY_COUNT(chain), scratch, sizeof(scratch)),
        IOS_ERROR(IOS_E_CORRUPT));
    IOS_TEST_ASSERT(fat[3] == IOS_FS_FAT_END_OF_CHAIN);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_full_directory_requires_growth_then_allocates_in_new_cluster),
    IOS_TEST_CASE(test_pair_allocation_never_spans_growth_boundary),
    IOS_TEST_CASE(test_scan_exposes_pairs_on_both_sides_of_growth_boundary_once),
    IOS_TEST_CASE(test_scan_rejects_companion_at_cluster_boundary),
    IOS_TEST_CASE(test_subdirectory_initializes_internal_links_and_hides_them),
    IOS_TEST_CASE(test_subdirectory_growth_links_zeroed_cluster_and_enumerates_pair),
    IOS_TEST_CASE(test_subdirectory_remove_rejects_nonempty_then_releases_chain),
    IOS_TEST_CASE(test_subdirectory_remove_rejects_malformed_parent_link)
};
const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
