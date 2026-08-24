#include <inferenceos/test.h>

#include <inferenceos/runtime.h>

static void test_memory_copy_set_and_compare(void)
{
    unsigned char source[] = { 0, 1, 2, 127, 128, 255 };
    unsigned char destination[sizeof(source)];
    void *result;

    result = memset(destination, 0xa5, sizeof(destination));
    IOS_TEST_ASSERT(result == destination);
    for (size_t index = 0; index < sizeof(destination); ++index) {
        IOS_TEST_ASSERT(destination[index] == 0xa5);
    }

    result = memcpy(destination, source, sizeof(source));
    IOS_TEST_ASSERT(result == destination);
    IOS_TEST_ASSERT(memcmp(destination, source, sizeof(source)) == 0);
    IOS_TEST_ASSERT(memcmp((unsigned char[]){ 0x00 }, (unsigned char[]){ 0xff }, 1) < 0);
    IOS_TEST_ASSERT(memcmp((unsigned char[]){ 0xff }, (unsigned char[]){ 0x00 }, 1) > 0);
    IOS_TEST_ASSERT(memcpy(destination, source, 0) == destination);
    IOS_TEST_ASSERT(memset(destination, 0, 0) == destination);
    IOS_TEST_ASSERT(memcmp(destination, source, 0) == 0);
}

static void test_memmove_handles_both_overlap_directions(void)
{
    char right[] = "abcdef";
    char left[] = "abcdef";

    IOS_TEST_ASSERT(memmove(right + 2, right, 4) == right + 2);
    IOS_TEST_ASSERT(memcmp(right, "ababcd", 6) == 0);

    IOS_TEST_ASSERT(memmove(left, left + 2, 4) == left);
    IOS_TEST_ASSERT(memcmp(left, "cdefef", 6) == 0);
    IOS_TEST_ASSERT(memmove(left, left, sizeof(left)) == left);
}

static void test_string_lengths_and_comparisons(void)
{
    IOS_TEST_ASSERT(strlen("") == 0);
    IOS_TEST_ASSERT(strlen("InferenceOS") == 11);
    IOS_TEST_ASSERT(strnlen("InferenceOS", 4) == 4);
    IOS_TEST_ASSERT(strnlen("OS", 8) == 2);
    IOS_TEST_ASSERT(strnlen("OS", 0) == 0);

    IOS_TEST_ASSERT(strcmp("same", "same") == 0);
    IOS_TEST_ASSERT(strcmp("alpha", "beta") < 0);
    IOS_TEST_ASSERT(strcmp("beta", "alpha") > 0);
    IOS_TEST_ASSERT(strcmp("a", "aa") < 0);
    IOS_TEST_ASSERT(strncmp("prefix-a", "prefix-b", 7) == 0);
    IOS_TEST_ASSERT(strncmp("prefix-a", "prefix-b", 8) < 0);
    IOS_TEST_ASSERT(strncmp("anything", "different", 0) == 0);
}

static void test_character_search_includes_terminator(void)
{
    const char value[] = "abca";

    IOS_TEST_ASSERT(strchr(value, 'b') == value + 1);
    IOS_TEST_ASSERT(strchr(value, 'z') == NULL);
    IOS_TEST_ASSERT(strchr(value, '\0') == value + 4);
    IOS_TEST_ASSERT(strrchr(value, 'a') == value + 3);
    IOS_TEST_ASSERT(strrchr(value, 'z') == NULL);
    IOS_TEST_ASSERT(strrchr(value, '\0') == value + 4);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_memory_copy_set_and_compare),
    IOS_TEST_CASE(test_memmove_handles_both_overlap_directions),
    IOS_TEST_CASE(test_string_lengths_and_comparisons),
    IOS_TEST_CASE(test_character_search_includes_terminator)
};

const size_t ios_test_case_count = sizeof(ios_test_cases) / sizeof(ios_test_cases[0]);
