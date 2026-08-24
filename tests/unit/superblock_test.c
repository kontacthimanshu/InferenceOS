#include <inferenceos/test.h>

#include <inferenceos/fs/format.h>

#include <string.h>

static struct ios_fs_superblock minimum_values(void)
{
    const struct ios_fs_superblock values = {
        UINT64_C(97656250), 95271, UINT64_C(95273), UINT32_C(0x1234abcd), 0,
        { 'I', 'N', 'F', 'E', 'R', 'E', 'N', 'C', 'E', ' ', ' ' }
    };
    return values;
}

static void rewrite_crc(struct ios_fs_superblock_disk *disk)
{
    ios_u8 *bytes = (ios_u8 *)disk;
    ios_u32 crc;
    memset(disk->crc32, 0, sizeof(disk->crc32));
    crc = ios_fs_crc32_iso_hdlc(disk, sizeof(*disk));
    for (ios_size index = 0; index < 4; ++index) bytes[0x48 + index] = (ios_u8)(crc >> (index * 8));
}

static void test_crc_standard_vector_and_crc_field_zeroing(void)
{
    static const ios_u8 vector[] = "123456789";
    struct ios_fs_superblock values = minimum_values();
    struct ios_fs_superblock decoded;
    struct ios_fs_superblock_disk disk;
    IOS_TEST_ASSERT(ios_fs_crc32_iso_hdlc(vector, 9) == UINT32_C(0xcbf43926));
    IOS_TEST_ASSERT_STATUS(ios_fs_superblock_encode(&values, &disk), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_fs_superblock_decode(&disk, &decoded), IOS_OK);
    disk.crc32[0] ^= 1;
    IOS_TEST_ASSERT_STATUS(ios_fs_superblock_decode(&disk, &decoded), IOS_ERROR(IOS_E_CORRUPT));
}

static void test_encoder_uses_exact_little_endian_layout_and_round_trips(void)
{
    struct ios_fs_superblock values = minimum_values();
    struct ios_fs_superblock decoded;
    struct ios_fs_superblock_disk disk;
    const ios_u8 *bytes = (const ios_u8 *)&disk;
    IOS_TEST_ASSERT_STATUS(ios_fs_superblock_encode(&values, &disk), IOS_OK);
    IOS_TEST_ASSERT(memcmp(bytes, IOS_FS_MAGIC, 8) == 0);
    IOS_TEST_ASSERT(bytes[0x0a] == 80 && bytes[0x0c] == 0 && bytes[0x0d] == 2);
    IOS_TEST_ASSERT(bytes[0x20] == 0x27 && bytes[0x21] == 0x74 && bytes[0x22] == 0x01);
    IOS_TEST_ASSERT(bytes[0x1fe] == 0x55 && bytes[0x1ff] == 0xaa);
    for (ios_size index = 0x50; index < 0x1fe; ++index) IOS_TEST_ASSERT(bytes[index] == 0);
    IOS_TEST_ASSERT_STATUS(ios_fs_superblock_decode(&disk, &decoded), IOS_OK);
    IOS_TEST_ASSERT(decoded.total_sectors == values.total_sectors);
    IOS_TEST_ASSERT(decoded.sectors_per_fat == values.sectors_per_fat);
    IOS_TEST_ASSERT(decoded.registry_start_sector == values.registry_start_sector);
    IOS_TEST_ASSERT(decoded.volume_serial == values.volume_serial);
    IOS_TEST_ASSERT(memcmp(decoded.volume_label, values.volume_label, IOS_FS_VOLUME_LABEL_SIZE) == 0);
}

static void test_crc_valid_semantic_corruption_is_rejected(void)
{
    static const ios_size invalid_offsets[] = {
        0x08, 0x0c, 0x0e, 0x0f, 0x10, 0x12, 0x14, 0x16, 0x24, 0x30,
        0x43, 0x44, 0x46, 0x4c, 0x50, 0x1fe
    };
    struct ios_fs_superblock values = minimum_values();
    struct ios_fs_superblock decoded;
    struct ios_fs_superblock_disk disk;
    for (ios_size index = 0; index < IOS_ARRAY_COUNT(invalid_offsets); ++index) {
        IOS_TEST_ASSERT_STATUS(ios_fs_superblock_encode(&values, &disk), IOS_OK);
        ((ios_u8 *)&disk)[invalid_offsets[index]] ^= 1;
        rewrite_crc(&disk);
        IOS_TEST_ASSERT_STATUS(ios_fs_superblock_decode(&disk, &decoded), IOS_ERROR(IOS_E_PROTOCOL));
    }
}

static void test_backup_corruption_and_disagreement_refuse_writable_state(void)
{
    struct ios_fs_superblock values = minimum_values();
    struct ios_fs_superblock_disk primary;
    struct ios_fs_superblock_disk backup;
    IOS_TEST_ASSERT_STATUS(ios_fs_superblock_encode(&values, &primary), IOS_OK);
    backup = primary;
    IOS_TEST_ASSERT(ios_fs_superblock_classify_pair(&primary, &backup)
                    == IOS_FS_SUPERBLOCK_PAIR_READ_WRITE);
    backup.reserved[10] ^= 0x80;
    IOS_TEST_ASSERT(ios_fs_superblock_classify_pair(&primary, &backup)
                    == IOS_FS_SUPERBLOCK_PAIR_DIAGNOSTIC);
    primary.reserved[11] ^= 0x40;
    IOS_TEST_ASSERT(ios_fs_superblock_classify_pair(&primary, &backup)
                    == IOS_FS_SUPERBLOCK_PAIR_REJECTED);
    IOS_TEST_ASSERT_STATUS(ios_fs_superblock_encode(&values, &primary), IOS_OK);
    values.volume_serial ^= UINT32_C(0x01020304);
    IOS_TEST_ASSERT_STATUS(ios_fs_superblock_encode(&values, &backup), IOS_OK);
    IOS_TEST_ASSERT(ios_fs_superblock_classify_pair(&primary, &backup)
                    == IOS_FS_SUPERBLOCK_PAIR_DIAGNOSTIC);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_crc_standard_vector_and_crc_field_zeroing),
    IOS_TEST_CASE(test_encoder_uses_exact_little_endian_layout_and_round_trips),
    IOS_TEST_CASE(test_crc_valid_semantic_corruption_is_rejected),
    IOS_TEST_CASE(test_backup_corruption_and_disagreement_refuse_writable_state)
};
const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
