#include <inferenceos/test.h>

#include <inferenceos/fs/file.h>

#include <string.h>

enum {
    TEST_CLUSTER_COUNT = 8,
    TEST_CLUSTER_BYTES = IOS_FS_SECTOR_SIZE * IOS_FS_SECTORS_PER_CLUSTER
};

struct memory_clusters {
    ios_u8 bytes[TEST_CLUSTER_COUNT][TEST_CLUSTER_BYTES];
    bool zeroed[TEST_CLUSTER_COUNT];
};

static ios_status memory_read(void *context, ios_u32 cluster, void *buffer)
{
    struct memory_clusters *memory = context;
    if (memory == NULL || buffer == NULL || cluster >= TEST_CLUSTER_COUNT) {
        return IOS_ERROR(IOS_E_OUT_OF_RANGE);
    }
    memcpy(buffer, memory->bytes[cluster], TEST_CLUSTER_BYTES);
    return IOS_OK;
}

static ios_status memory_write(void *context, ios_u32 cluster, const void *buffer)
{
    struct memory_clusters *memory = context;
    if (memory == NULL || buffer == NULL || cluster >= TEST_CLUSTER_COUNT) {
        return IOS_ERROR(IOS_E_OUT_OF_RANGE);
    }
    memcpy(memory->bytes[cluster], buffer, TEST_CLUSTER_BYTES);
    return IOS_OK;
}

static ios_status memory_zero(void *context, ios_u32 cluster)
{
    struct memory_clusters *memory = context;
    if (memory == NULL || cluster >= TEST_CLUSTER_COUNT) return IOS_ERROR(IOS_E_OUT_OF_RANGE);
    memset(memory->bytes[cluster], 0, TEST_CLUSTER_BYTES);
    memory->zeroed[cluster] = true;
    return IOS_OK;
}

static void initialize_store(
    struct ios_fs_file_store *store,
    struct memory_clusters *memory,
    ios_u32 fat[TEST_CLUSTER_COUNT]
)
{
    memset(memory, 0xa5, sizeof(*memory));
    memset(fat, 0, sizeof(ios_u32) * TEST_CLUSTER_COUNT);
    fat[0] = IOS_FS_FAT_END_OF_CHAIN;
    fat[1] = IOS_FS_FAT_END_OF_CHAIN;
    *store = (struct ios_fs_file_store){
        fat, TEST_CLUSTER_COUNT, TEST_CLUSTER_BYTES, memory,
        { memory_read, memory_write, memory_zero }, true
    };
}

static void build_pair(
    ios_u32 first_cluster,
    ios_u32 file_size,
    struct ios_fs_companion_disk *companion,
    struct ios_fs_primary_disk *primary_disk
)
{
    const struct ios_fs_primary primary = {
        { 'R', 'E', 'P', 'O', 'R', 'T', ' ', ' ', 'T', 'X', 'T' },
        IOS_FS_ATTRIBUTE_REGULAR, first_cluster, file_size
    };
    IOS_TEST_ASSERT_STATUS(ios_fs_primary_encode(&primary, primary_disk), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_fs_companion_encode(primary.name, true, companion), IOS_OK);
}

static void test_open_read_seek_and_eof(void)
{
    struct memory_clusters memory;
    struct ios_fs_file_store store;
    struct ios_fs_companion_disk companion;
    struct ios_fs_primary_disk primary;
    struct ios_fs_file file;
    ios_u32 fat[TEST_CLUSTER_COUNT];
    ios_u32 chain[TEST_CLUSTER_COUNT];
    ios_u8 scratch[TEST_CLUSTER_BYTES];
    ios_u8 output[8] = { 0 };
    ios_size transferred;
    ios_u32 position;
    initialize_store(&store, &memory, fat);
    fat[2] = IOS_FS_FAT_END_OF_CHAIN;
    memcpy(memory.bytes[2], "hello", 5);
    build_pair(2, 5, &companion, &primary);
    IOS_TEST_ASSERT_STATUS(ios_fs_file_open(
        &file, &store, &companion, &primary, false,
        chain, IOS_ARRAY_COUNT(chain), scratch, sizeof(scratch)
    ), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_fs_file_read(&file, output, sizeof(output), &transferred), IOS_OK);
    IOS_TEST_ASSERT(transferred == 5 && memcmp(output, "hello", 5) == 0);
    IOS_TEST_ASSERT_STATUS(ios_fs_file_read(&file, output, 1, &transferred), IOS_OK);
    IOS_TEST_ASSERT(transferred == 0);
    IOS_TEST_ASSERT_STATUS(ios_fs_file_seek(&file, IOS_FS_SEEK_END, -2, &position), IOS_OK);
    IOS_TEST_ASSERT(position == 3);
    IOS_TEST_ASSERT_STATUS(ios_fs_file_read(&file, output, 2, &transferred), IOS_OK);
    IOS_TEST_ASSERT(transferred == 2 && memcmp(output, "lo", 2) == 0);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_file_write(&file, "x", 1, &transferred), IOS_ERROR(IOS_E_READ_ONLY)
    );
    IOS_TEST_ASSERT_STATUS(ios_fs_file_close(&file), IOS_OK);
}

static void test_write_allocates_zeroed_cluster_and_updates_metadata(void)
{
    static const ios_u8 content[] = { 'a', 'b', 'c' };
    struct memory_clusters memory;
    struct ios_fs_file_store store;
    struct ios_fs_companion_disk companion;
    struct ios_fs_primary_disk primary_disk;
    struct ios_fs_primary metadata;
    struct ios_fs_file file;
    ios_u32 fat[TEST_CLUSTER_COUNT];
    ios_u32 chain[TEST_CLUSTER_COUNT];
    ios_u8 scratch[TEST_CLUSTER_BYTES];
    ios_size transferred;
    initialize_store(&store, &memory, fat);
    build_pair(0, 0, &companion, &primary_disk);
    IOS_TEST_ASSERT_STATUS(ios_fs_file_open(
        &file, &store, &companion, &primary_disk, true,
        chain, IOS_ARRAY_COUNT(chain), scratch, sizeof(scratch)
    ), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_fs_file_append(
        &file, content, sizeof(content), &transferred
    ), IOS_OK);
    IOS_TEST_ASSERT(transferred == sizeof(content));
    IOS_TEST_ASSERT_STATUS(ios_fs_file_metadata(&file, &metadata), IOS_OK);
    IOS_TEST_ASSERT(metadata.first_cluster == 2 && metadata.file_size == sizeof(content));
    IOS_TEST_ASSERT(fat[2] == IOS_FS_FAT_END_OF_CHAIN && memory.zeroed[2]);
    IOS_TEST_ASSERT(memcmp(memory.bytes[2], content, sizeof(content)) == 0);
    for (ios_size index = sizeof(content); index < TEST_CLUSTER_BYTES; ++index) {
        IOS_TEST_ASSERT(memory.bytes[2][index] == 0);
    }
}

static void test_append_extends_chain_and_non_sparse_rule_is_enforced(void)
{
    struct memory_clusters memory;
    struct ios_fs_file_store store;
    struct ios_fs_companion_disk companion;
    struct ios_fs_primary_disk primary_disk;
    struct ios_fs_primary metadata;
    struct ios_fs_file file;
    ios_u32 fat[TEST_CLUSTER_COUNT];
    ios_u32 chain[TEST_CLUSTER_COUNT];
    ios_u8 scratch[TEST_CLUSTER_BYTES];
    ios_u8 content[TEST_CLUSTER_BYTES];
    ios_size transferred;
    ios_u32 position;
    memset(content, 0x5a, sizeof(content));
    initialize_store(&store, &memory, fat);
    fat[2] = IOS_FS_FAT_END_OF_CHAIN;
    memcpy(memory.bytes[2], "hello", 5);
    build_pair(2, 5, &companion, &primary_disk);
    IOS_TEST_ASSERT_STATUS(ios_fs_file_open(
        &file, &store, &companion, &primary_disk, true,
        chain, IOS_ARRAY_COUNT(chain), scratch, sizeof(scratch)
    ), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_fs_file_append(
        &file, content, sizeof(content), &transferred
    ), IOS_OK);
    IOS_TEST_ASSERT(transferred == sizeof(content));
    IOS_TEST_ASSERT(fat[2] == 3 && fat[3] == IOS_FS_FAT_END_OF_CHAIN && memory.zeroed[3]);
    IOS_TEST_ASSERT_STATUS(ios_fs_file_metadata(&file, &metadata), IOS_OK);
    IOS_TEST_ASSERT(metadata.file_size == TEST_CLUSTER_BYTES + 5);
    IOS_TEST_ASSERT_STATUS(ios_fs_file_seek(&file, IOS_FS_SEEK_END, 1, &position), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_file_write(&file, content, 1, &transferred), IOS_ERROR(IOS_E_INVALID_ARGUMENT)
    );
}

static void test_open_rejects_short_chain_and_write_rejects_overflow(void)
{
    struct memory_clusters memory;
    struct ios_fs_file_store store;
    struct ios_fs_companion_disk companion;
    struct ios_fs_primary_disk primary_disk;
    struct ios_fs_file file;
    ios_u32 fat[TEST_CLUSTER_COUNT];
    ios_u32 chain[TEST_CLUSTER_COUNT];
    ios_u8 scratch[TEST_CLUSTER_BYTES];
    ios_u8 byte = 1;
    ios_size transferred;
    initialize_store(&store, &memory, fat);
    fat[2] = IOS_FS_FAT_END_OF_CHAIN;
    build_pair(2, TEST_CLUSTER_BYTES + 1, &companion, &primary_disk);
    IOS_TEST_ASSERT_STATUS(ios_fs_file_open(
        &file, &store, &companion, &primary_disk, true,
        chain, IOS_ARRAY_COUNT(chain), scratch, sizeof(scratch)
    ), IOS_ERROR(IOS_E_CORRUPT));
    build_pair(0, 0, &companion, &primary_disk);
    IOS_TEST_ASSERT_STATUS(ios_fs_file_open(
        &file, &store, &companion, &primary_disk, true,
        chain, IOS_ARRAY_COUNT(chain), scratch, sizeof(scratch)
    ), IOS_OK);
    file.primary.file_size = UINT32_MAX;
    file.position = UINT32_MAX;
    IOS_TEST_ASSERT_STATUS(
        ios_fs_file_append(&file, &byte, 1, &transferred), IOS_ERROR(IOS_E_OVERFLOW)
    );
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_open_read_seek_and_eof),
    IOS_TEST_CASE(test_write_allocates_zeroed_cluster_and_updates_metadata),
    IOS_TEST_CASE(test_append_extends_chain_and_non_sparse_rule_is_enforced),
    IOS_TEST_CASE(test_open_rejects_short_chain_and_write_rejects_overflow)
};
const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
