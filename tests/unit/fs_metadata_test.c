#include <inferenceos/test.h>

#include <inferenceos/fs/records.h>

#include <string.h>

static void test_83_canonicalization_vectors(void)
{
    static const struct {
        const char *input;
        ios_u8 expected[IOS_FS_NAME_SIZE];
        ios_size extension_length;
    } vectors[] = {
        { "README", { 'R', 'E', 'A', 'D', 'M', 'E', ' ', ' ', ' ', ' ', ' ' }, 0 },
        { "a.t", { 'A', ' ', ' ', ' ', ' ', ' ', ' ', ' ', 'T', ' ', ' ' }, 1 },
        { "ab.tx", { 'A', 'B', ' ', ' ', ' ', ' ', ' ', ' ', 'T', 'X', ' ' }, 2 },
        { "report.txt", { 'R', 'E', 'P', 'O', 'R', 'T', ' ', ' ', 'T', 'X', 'T' }, 3 }
    };
    ios_u8 actual[IOS_FS_NAME_SIZE];
    ios_u8 extension[IOS_FS_EXTENSION_SIZE];
    ios_size length;
    for (ios_size index = 0; index < IOS_ARRAY_COUNT(vectors); ++index) {
        IOS_TEST_ASSERT_STATUS(ios_fs_name_canonicalize_83(vectors[index].input, actual), IOS_OK);
        IOS_TEST_ASSERT(memcmp(actual, vectors[index].expected, sizeof(actual)) == 0);
        IOS_TEST_ASSERT_STATUS(ios_fs_name_extension(actual, extension, &length), IOS_OK);
        IOS_TEST_ASSERT(length == vectors[index].extension_length);
        IOS_TEST_ASSERT(memcmp(extension, actual + 8, length) == 0);
    }
}

static void test_83_rejects_invalid_inputs_without_truncation(void)
{
    static const char *const invalid[] = {
        "", ".TXT", "A.B.C", "A/B.TXT", "BAD NAME.TXT", "TOOLONG99.TXT", "A.LONG"
    };
    ios_u8 output[IOS_FS_NAME_SIZE];
    for (ios_size index = 0; index < IOS_ARRAY_COUNT(invalid); ++index) {
        IOS_TEST_ASSERT_STATUS(
            ios_fs_name_canonicalize_83(invalid[index], output),
            IOS_ERROR(IOS_E_INVALID_ARGUMENT)
        );
    }
}

static void test_fnv1a_and_uppercase_hash_text_vectors(void)
{
    static const struct {
        const char *extension;
        ios_u32 hash;
        ios_u8 text[IOS_FS_HASH_TEXT_SIZE];
    } vectors[] = {
        { "", UINT32_C(0x811c9dc5), { '8', '1', '1', 'C', '9', 'D', 'C', '5' } },
        { "T", UINT32_C(0xd10c0b43), { 'D', '1', '0', 'C', '0', 'B', '4', '3' } },
        { "TX", UINT32_C(0x30f57b81), { '3', '0', 'F', '5', '7', 'B', '8', '1' } },
        { "TXT", UINT32_C(0xe771f04f), { 'E', '7', '7', '1', 'F', '0', '4', 'F' } },
        { "BIN", UINT32_C(0xdf81ecde), { 'D', 'F', '8', '1', 'E', 'C', 'D', 'E' } }
    };
    ios_u8 text[IOS_FS_HASH_TEXT_SIZE];
    for (ios_size index = 0; index < IOS_ARRAY_COUNT(vectors); ++index) {
        const ios_u32 hash = ios_fs_fnv1a32(
            vectors[index].extension, strlen(vectors[index].extension)
        );
        IOS_TEST_ASSERT(hash == vectors[index].hash);
        ios_fs_hash_text(hash, text);
        IOS_TEST_ASSERT(memcmp(text, vectors[index].text, sizeof(text)) == 0);
    }
}

static void test_checksum_and_crc_vectors(void)
{
    static const ios_u8 name[IOS_FS_NAME_SIZE] = {
        'R', 'E', 'P', 'O', 'R', 'T', ' ', ' ', 'T', 'X', 'T'
    };
    static const ios_u8 standard[] = "123456789";
    ios_u8 record[32];
    IOS_TEST_ASSERT(ios_fs_primary_name_checksum(name) == 0xa3);
    for (ios_size index = 0; index < sizeof(record); ++index) record[index] = (ios_u8)index;
    memset(record + 16, 0, 4);
    IOS_TEST_ASSERT(ios_fs_crc32_iso_hdlc(standard, 9) == UINT32_C(0xcbf43926));
    IOS_TEST_ASSERT(ios_fs_crc32_iso_hdlc(record, sizeof(record)) == UINT32_C(0x04893666));
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_83_canonicalization_vectors),
    IOS_TEST_CASE(test_83_rejects_invalid_inputs_without_truncation),
    IOS_TEST_CASE(test_fnv1a_and_uppercase_hash_text_vectors),
    IOS_TEST_CASE(test_checksum_and_crc_vectors)
};
const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
