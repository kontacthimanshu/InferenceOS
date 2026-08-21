#include "../support/test_assert.h"

#include <inferenceos/base.h>
#include <inferenceos/crc32.h>
#include <inferenceos/endian.h>
#include <inferenceos/fnv1a.h>
#include <inferenceos/memory.h>
#include <inferenceos/result.h>
#include <inferenceos/string.h>

static void test_memory_runtime(void)
{
    inferenceos_u8 source[8] = { 0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U };
    inferenceos_u8 destination[8];
    inferenceos_u8 expected[8] = { 0U, 1U, 0U, 1U, 2U, 3U, 4U, 5U };

    (void)memset(destination, 0xA5, sizeof(destination));
    for (inferenceos_size index = 0U; index < sizeof(destination); ++index) {
        INFERENCEOS_TEST_ASSERT_U64_EQUAL(0xA5U, destination[index]);
    }

    (void)memcpy(destination, source, sizeof(source));
    INFERENCEOS_TEST_ASSERT_MEMORY_EQUAL(source, destination, sizeof(source));

    (void)memmove(source + 2U, source, 6U);
    INFERENCEOS_TEST_ASSERT_MEMORY_EQUAL(expected, source, sizeof(source));

    (void)memmove(source, source + 2U, 6U);
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(0U, source[0]);
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(1U, source[1]);
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(2U, source[2]);
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(3U, source[3]);
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(4U, source[4]);
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(5U, source[5]);

    INFERENCEOS_TEST_ASSERT_I64_EQUAL(0, memcmp(source, source, sizeof(source)));
    INFERENCEOS_TEST_ASSERT(memcmp("A", "B", 1U) < 0);
    INFERENCEOS_TEST_ASSERT(memcmp("B", "A", 1U) > 0);
}

static void test_bounded_strings(void)
{
    char destination[8] = "KEEP";
    inferenceos_size length = 99U;
    bool equal = false;

    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_OK,
        inferenceos_string_length("TEST", 5U, &length));
    INFERENCEOS_TEST_ASSERT_SIZE_EQUAL(4U, length);
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_OUT_OF_RANGE,
        inferenceos_string_length("TEST", 4U, &length));
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_INVALID_ARGUMENT,
        inferenceos_string_length(NULL, 4U, &length));

    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_OK,
        inferenceos_string_copy(destination, sizeof(destination),
            "ABC", 4U, &length));
    INFERENCEOS_TEST_ASSERT_STRING_EQUAL("ABC", destination);
    INFERENCEOS_TEST_ASSERT_SIZE_EQUAL(3U, length);

    (void)memcpy(destination, "KEEP", 5U);
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_NO_SPACE,
        inferenceos_string_copy(destination, 3U, "LONG", 5U, NULL));
    INFERENCEOS_TEST_ASSERT_STRING_EQUAL("KEEP", destination);

    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_OK,
        inferenceos_string_equal("same", 5U, "same", 5U, &equal));
    INFERENCEOS_TEST_ASSERT(equal);
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_OK,
        inferenceos_string_equal("same", 5U, "diff", 5U, &equal));
    INFERENCEOS_TEST_ASSERT_FALSE(equal);
    INFERENCEOS_TEST_ASSERT_I64_EQUAL('A', inferenceos_ascii_to_upper('a'));
    INFERENCEOS_TEST_ASSERT_I64_EQUAL('-', inferenceos_ascii_to_upper('-'));
    INFERENCEOS_TEST_ASSERT(inferenceos_ascii_is_printable(' '));
    INFERENCEOS_TEST_ASSERT(inferenceos_ascii_is_printable('~'));
    INFERENCEOS_TEST_ASSERT_FALSE(inferenceos_ascii_is_printable('\n'));
}

static void test_little_endian_helpers(void)
{
    inferenceos_u8 buffer[10] = { 0U };

    inferenceos_store_le16(buffer + 1U, UINT16_C(0xA1B2));
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(0xB2U, buffer[1]);
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(0xA1U, buffer[2]);
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(UINT16_C(0xA1B2),
        inferenceos_load_le16(buffer + 1U));

    inferenceos_store_le32(buffer + 1U, UINT32_C(0x89ABCDEF));
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(UINT32_C(0x89ABCDEF),
        inferenceos_load_le32(buffer + 1U));
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(0xEFU, buffer[1]);
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(0x89U, buffer[4]);

    inferenceos_store_le64(buffer + 1U, UINT64_C(0x0123456789ABCDEF));
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(UINT64_C(0x0123456789ABCDEF),
        inferenceos_load_le64(buffer + 1U));
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(0xEFU, buffer[1]);
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(0x01U, buffer[8]);
}

static void test_checked_arithmetic(void)
{
    inferenceos_u32 result32 = UINT32_C(0xDEADBEEF);
    inferenceos_u64 result64 = UINT64_C(0xDEADBEEFDEADBEEF);
    inferenceos_size size_result = 77U;

    INFERENCEOS_TEST_ASSERT(inferenceos_checked_add_u32(40U, 2U, &result32));
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(42U, result32);
    result32 = UINT32_C(0xDEADBEEF);
    INFERENCEOS_TEST_ASSERT_FALSE(
        inferenceos_checked_add_u32(UINT32_MAX, 1U, &result32));
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(UINT32_C(0xDEADBEEF), result32);
    INFERENCEOS_TEST_ASSERT(inferenceos_checked_mul_u32(6U, 7U, &result32));
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(42U, result32);
    INFERENCEOS_TEST_ASSERT_FALSE(
        inferenceos_checked_mul_u32(UINT32_MAX, 2U, &result32));

    INFERENCEOS_TEST_ASSERT(inferenceos_checked_add_u64(40U, 2U, &result64));
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(42U, result64);
    INFERENCEOS_TEST_ASSERT_FALSE(
        inferenceos_checked_add_u64(UINT64_MAX, 1U, &result64));
    INFERENCEOS_TEST_ASSERT(inferenceos_checked_sub_u64(42U, 2U, &result64));
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(40U, result64);
    INFERENCEOS_TEST_ASSERT_FALSE(
        inferenceos_checked_sub_u64(1U, 2U, &result64));
    INFERENCEOS_TEST_ASSERT_FALSE(
        inferenceos_checked_mul_u64(UINT64_MAX, 2U, &result64));

    INFERENCEOS_TEST_ASSERT(
        inferenceos_checked_align_up_size(17U, 16U, &size_result));
    INFERENCEOS_TEST_ASSERT_SIZE_EQUAL(32U, size_result);
    INFERENCEOS_TEST_ASSERT_FALSE(
        inferenceos_checked_align_up_size(17U, 3U, &size_result));
    INFERENCEOS_TEST_ASSERT(inferenceos_range_within_u64(8U, 2U, 10U));
    INFERENCEOS_TEST_ASSERT_FALSE(
        inferenceos_range_within_u64(UINT64_MAX, 1U, UINT64_MAX));
}

static void test_fnv1a_vectors(void)
{
    const char digits[] = "123456789";
    inferenceos_u32 state = inferenceos_fnv1a32_begin();

    INFERENCEOS_TEST_ASSERT_U64_EQUAL(UINT32_C(0x811C9DC5),
        inferenceos_fnv1a32(NULL, 0U));
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(UINT32_C(0xE40C292C),
        inferenceos_fnv1a32("a", 1U));
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(UINT32_C(0xBF9CF968),
        inferenceos_fnv1a32("foobar", 6U));
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(UINT32_C(0xBB86B11C),
        inferenceos_fnv1a32(digits, 9U));

    state = inferenceos_fnv1a32_update(state, digits, 4U);
    state = inferenceos_fnv1a32_update(state, digits + 4U, 5U);
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(UINT32_C(0xBB86B11C), state);
}

static void test_crc32_vectors(void)
{
    const char digits[] = "123456789";
    inferenceos_u32 state = inferenceos_crc32_begin();

    INFERENCEOS_TEST_ASSERT_U64_EQUAL(0U, inferenceos_crc32(NULL, 0U));
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(UINT32_C(0xCBF43926),
        inferenceos_crc32(digits, 9U));

    state = inferenceos_crc32_update(state, digits, 4U);
    state = inferenceos_crc32_update(state, digits + 4U, 5U);
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(UINT32_C(0xCBF43926),
        inferenceos_crc32_finish(state));
}

static const inferenceos_test_case primitive_cases[] = {
    INFERENCEOS_TEST_CASE(test_memory_runtime),
    INFERENCEOS_TEST_CASE(test_bounded_strings),
    INFERENCEOS_TEST_CASE(test_little_endian_helpers),
    INFERENCEOS_TEST_CASE(test_checked_arithmetic),
    INFERENCEOS_TEST_CASE(test_fnv1a_vectors),
    INFERENCEOS_TEST_CASE(test_crc32_vectors)
};

const inferenceos_test_suite *inferenceos_test_suite_definition(void)
{
    static const inferenceos_test_suite suite = {
        .name = "primitives",
        .cases = primitive_cases,
        .case_count = INFERENCEOS_ARRAY_COUNT(primitive_cases)
    };
    return &suite;
}
