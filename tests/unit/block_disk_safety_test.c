#include <inferenceos/block.h>
#include <inferenceos/test.h>

#include <string.h>

enum { TEST_SECTORS = 8192 };
static ios_u8 disk[TEST_SECTORS][IOS_BLOCK_SECTOR_SIZE];
static ios_size write_count;

static ios_status memory_read(
    void *context, ios_u64 first_sector, ios_size sector_count, void *buffer
)
{
    (void)context;
    if (buffer == NULL || sector_count == 0 || first_sector >= TEST_SECTORS
        || sector_count > TEST_SECTORS - first_sector) {
        return IOS_ERROR(IOS_E_OUT_OF_RANGE);
    }
    memcpy(buffer, &disk[first_sector][0], sector_count * IOS_BLOCK_SECTOR_SIZE);
    return IOS_OK;
}

static ios_status memory_write(
    void *context, ios_u64 first_sector, ios_size sector_count, const void *buffer
)
{
    (void)context;
    if (buffer == NULL || sector_count == 0 || first_sector >= TEST_SECTORS
        || sector_count > TEST_SECTORS - first_sector) {
        return IOS_ERROR(IOS_E_OUT_OF_RANGE);
    }
    ++write_count;
    memcpy(&disk[first_sector][0], buffer, sector_count * IOS_BLOCK_SECTOR_SIZE);
    return IOS_OK;
}

static ios_status memory_flush(void *context)
{
    (void)context;
    return IOS_OK;
}

static struct ios_block_device make_device(void)
{
    static const struct ios_block_device_operations operations = {
        memory_read, memory_write, memory_flush
    };
    struct ios_block_device device;

    IOS_TEST_ASSERT_STATUS(block_device_initialize(
        &device, NULL, &operations, IOS_BLOCK_SECTOR_SIZE,
        TEST_SECTORS, IOS_BLOCK_DEVICE_READY), IOS_OK);
    return device;
}

static void write_le32(ios_u8 *bytes, ios_u32 value)
{
    bytes[0] = (ios_u8)value;
    bytes[1] = (ios_u8)(value >> 8);
    bytes[2] = (ios_u8)(value >> 16);
    bytes[3] = (ios_u8)(value >> 24);
}

static void write_le64(ios_u8 *bytes, ios_u64 value)
{
    write_le32(bytes, (ios_u32)value);
    write_le32(bytes + 4, (ios_u32)(value >> 32));
}

static ios_u32 header_crc(const ios_u8 *bytes, ios_size size)
{
    ios_u32 crc = UINT32_C(0xffffffff);
    for (ios_size index = 0; index < size; ++index) {
        ios_u8 value = index >= 16 && index < 20 ? 0 : bytes[index];
        crc ^= value;
        for (ios_u32 bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1U) ? UINT32_C(0xedb88320) : 0);
        }
    }
    return crc ^ UINT32_C(0xffffffff);
}

static void make_gpt_with_partition(const ios_u8 partition_guid[16])
{
    ios_u8 *header = disk[1];
    ios_u8 *entry = disk[2];

    memcpy(header, "EFI PART", 8);
    write_le32(header + 8, 0x00010000);
    write_le32(header + 12, 92);
    write_le64(header + 24, 1);
    write_le64(header + 32, TEST_SECTORS - 1);
    write_le64(header + 40, 34);
    write_le64(header + 48, TEST_SECTORS - 34);
    write_le64(header + 72, 2);
    write_le32(header + 80, 1);
    write_le32(header + 84, 128);
    entry[0] = 0x28;
    memcpy(entry + 16, partition_guid, 16);
    write_le64(entry + 32, 2048);
    write_le64(entry + 40, 4095);
    write_le32(header + 16, header_crc(header, 92));
}

static void blank_disk_is_eligible(void)
{
    struct ios_block_device device;
    enum ios_block_disk_classification classification;
    const ios_u8 no_boot_guid[16] = {0};

    memset(disk, 0, sizeof(disk));
    write_count = 0;
    device = make_device();
    IOS_TEST_ASSERT_STATUS(block_classify_data_disk(
        &device, no_boot_guid, &classification), IOS_OK);
    IOS_TEST_ASSERT(classification == IOS_BLOCK_DISK_ELIGIBLE_BLANK);
    IOS_TEST_ASSERT(write_count == 0);
}

static void boot_partition_guid_protects_the_entire_disk(void)
{
    static const ios_u8 boot_guid[16] = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
    };
    struct ios_block_device device;
    enum ios_block_disk_classification classification;

    memset(disk, 0, sizeof(disk));
    write_count = 0;
    make_gpt_with_partition(boot_guid);
    device = make_device();
    IOS_TEST_ASSERT_STATUS(block_classify_data_disk(
        &device, boot_guid, &classification), IOS_OK);
    IOS_TEST_ASSERT(classification == IOS_BLOCK_DISK_PROTECTED_BOOT);
    IOS_TEST_ASSERT(write_count == 0);
}

static void partitioned_and_foreign_disks_are_never_format_candidates(void)
{
    const ios_u8 no_boot_guid[16] = {0};
    const ios_u8 other_guid[16] = {9};
    struct ios_block_device device;
    enum ios_block_disk_classification classification;

    memset(disk, 0, sizeof(disk));
    write_count = 0;
    make_gpt_with_partition(other_guid);
    device = make_device();
    IOS_TEST_ASSERT_STATUS(block_classify_data_disk(
        &device, no_boot_guid, &classification), IOS_OK);
    IOS_TEST_ASSERT(classification == IOS_BLOCK_DISK_PROTECTED_PARTITIONED);
    IOS_TEST_ASSERT(write_count == 0);

    memset(disk, 0, sizeof(disk));
    disk[2][7] = 1;
    device = make_device();
    IOS_TEST_ASSERT_STATUS(block_classify_data_disk(
        &device, no_boot_guid, &classification), IOS_OK);
    IOS_TEST_ASSERT(classification == IOS_BLOCK_DISK_PROTECTED_FOREIGN);
    IOS_TEST_ASSERT(write_count == 0);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(blank_disk_is_eligible),
    IOS_TEST_CASE(boot_partition_guid_protects_the_entire_disk),
    IOS_TEST_CASE(partitioned_and_foreign_disks_are_never_format_candidates)
};

const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
