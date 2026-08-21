#include "../support/test_assert.h"

#include <inferencefs/name.h>
#include <inferenceos/memory.h>
#include <inferenceos/result.h>

static void assert_canonical_name(
    const char *input,
    inferenceos_size input_length,
    const inferenceos_u8 expected[INFERENCEFS_SHORT_NAME_SIZE]
)
{
    inferenceos_u8 actual[INFERENCEFS_SHORT_NAME_SIZE];

    (void)memset(actual, 0xA5, sizeof(actual));
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_OK,
        inferencefs_name_canonicalize(input, input_length, actual));
    INFERENCEOS_TEST_ASSERT_MEMORY_EQUAL(expected, actual, sizeof(actual));
}

static void test_supported_characters(void)
{
    INFERENCEOS_TEST_ASSERT(inferencefs_name_character_is_supported('A'));
    INFERENCEOS_TEST_ASSERT(inferencefs_name_character_is_supported('z'));
    INFERENCEOS_TEST_ASSERT(inferencefs_name_character_is_supported('0'));
    INFERENCEOS_TEST_ASSERT(inferencefs_name_character_is_supported('_'));
    INFERENCEOS_TEST_ASSERT(inferencefs_name_character_is_supported('-'));
    INFERENCEOS_TEST_ASSERT_FALSE(inferencefs_name_character_is_supported('.'));
    INFERENCEOS_TEST_ASSERT_FALSE(inferencefs_name_character_is_supported(' '));
    INFERENCEOS_TEST_ASSERT_FALSE(inferencefs_name_character_is_supported('/'));
}

static void test_canonicalization_vectors(void)
{
    static const inferenceos_u8 test_txt[] = "TEST    TXT";
    static const inferenceos_u8 readme[] = "README     ";
    static const inferenceos_u8 a_b[] = "A       B  ";
    static const inferenceos_u8 maximum[] = "EIGHT888123";

    assert_canonical_name("test.txt", 8U, test_txt);
    assert_canonical_name("README", 6U, readme);
    assert_canonical_name("a.b", 3U, a_b);
    assert_canonical_name("eight888.123", 12U, maximum);
}

static void assert_rejected_name(
    const char *input,
    inferenceos_size input_length,
    inferenceos_result expected_result
)
{
    inferenceos_u8 output[INFERENCEFS_SHORT_NAME_SIZE];
    inferenceos_u8 unchanged[INFERENCEFS_SHORT_NAME_SIZE];

    (void)memset(output, 0xA5, sizeof(output));
    (void)memset(unchanged, 0xA5, sizeof(unchanged));
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(expected_result,
        inferencefs_name_canonicalize(input, input_length, output));
    INFERENCEOS_TEST_ASSERT_MEMORY_EQUAL(unchanged, output, sizeof(output));
}

static void test_invalid_names_are_not_truncated(void)
{
    assert_rejected_name("", 0U, INFERENCEOS_RESULT_OUT_OF_RANGE);
    assert_rejected_name(".TXT", 4U, INFERENCEOS_RESULT_INVALID_ARGUMENT);
    assert_rejected_name("TEST.", 5U, INFERENCEOS_RESULT_INVALID_ARGUMENT);
    assert_rejected_name("A.B.C", 5U, INFERENCEOS_RESULT_INVALID_ARGUMENT);
    assert_rejected_name("HAS SPACE", 9U, INFERENCEOS_RESULT_INVALID_ARGUMENT);
    assert_rejected_name("DIR/FILE", 8U, INFERENCEOS_RESULT_INVALID_ARGUMENT);
    assert_rejected_name("NINECHARS", 9U, INFERENCEOS_RESULT_OUT_OF_RANGE);
    assert_rejected_name("TEST.FOUR", 9U, INFERENCEOS_RESULT_OUT_OF_RANGE);
}

static void test_extension_extraction(void)
{
    const inferenceos_u8 with_extension[] = "TEST    TXT";
    const inferenceos_u8 without_extension[] = "README     ";
    const inferenceos_u8 internal_space[] = "NAME    A B";
    const inferenceos_u8 expected_txt[3] = { 'T', 'X', 'T' };
    const inferenceos_u8 expected_empty[3] = { ' ', ' ', ' ' };
    const inferenceos_u8 expected_internal[3] = { 'A', ' ', 'B' };
    inferenceos_u8 extension[3];
    inferenceos_u8 length;

    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_OK,
        inferencefs_name_extension(with_extension, extension, &length));
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(3U, length);
    INFERENCEOS_TEST_ASSERT_MEMORY_EQUAL(expected_txt, extension, 3U);

    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_OK,
        inferencefs_name_extension(without_extension, extension, &length));
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(0U, length);
    INFERENCEOS_TEST_ASSERT_MEMORY_EQUAL(expected_empty, extension, 3U);

    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_OK,
        inferencefs_name_extension(internal_space, extension, &length));
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(3U, length);
    INFERENCEOS_TEST_ASSERT_MEMORY_EQUAL(expected_internal, extension, 3U);
}

static void test_name_equality(void)
{
    inferenceos_u8 first[11];
    inferenceos_u8 second[11];

    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_OK,
        inferencefs_name_canonicalize("test.txt", 8U, first));
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_OK,
        inferencefs_name_canonicalize("TEST.TXT", 8U, second));
    INFERENCEOS_TEST_ASSERT(inferencefs_name_equal(first, second));

    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_OK,
        inferencefs_name_canonicalize("TEST.LOG", 8U, second));
    INFERENCEOS_TEST_ASSERT_FALSE(inferencefs_name_equal(first, second));
    INFERENCEOS_TEST_ASSERT_FALSE(inferencefs_name_equal(NULL, second));
}

static void test_primary_name_checksum_vectors(void)
{
    const inferenceos_u8 test_txt[] = "TEST    TXT";
    const inferenceos_u8 readme[] = "README     ";
    const inferenceos_u8 a_b[] = "A       B  ";
    const inferenceos_u8 maximum[] = "EIGHT888123";

    INFERENCEOS_TEST_ASSERT_U64_EQUAL(0x8FU,
        inferencefs_primary_name_checksum(test_txt));
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(0x96U,
        inferencefs_primary_name_checksum(readme));
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(0x08U,
        inferencefs_primary_name_checksum(a_b));
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(0xF2U,
        inferencefs_primary_name_checksum(maximum));
}

static const inferenceos_test_case name_cases[] = {
    INFERENCEOS_TEST_CASE(test_supported_characters),
    INFERENCEOS_TEST_CASE(test_canonicalization_vectors),
    INFERENCEOS_TEST_CASE(test_invalid_names_are_not_truncated),
    INFERENCEOS_TEST_CASE(test_extension_extraction),
    INFERENCEOS_TEST_CASE(test_name_equality),
    INFERENCEOS_TEST_CASE(test_primary_name_checksum_vectors)
};

const inferenceos_test_suite *inferenceos_test_suite_definition(void)
{
    static const inferenceos_test_suite suite = {
        .name = "names",
        .cases = name_cases,
        .case_count = INFERENCEOS_ARRAY_COUNT(name_cases)
    };
    return &suite;
}
