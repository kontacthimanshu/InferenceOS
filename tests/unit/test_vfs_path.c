#include "../support/test_assert.h"

#include <inferenceos/memory.h>
#include <inferenceos/vfs.h>

#define TEST_PATH_LITERAL(value)                                                \
    ((inferenceos_vfs_path) { (value), sizeof(value) - 1U })

static void assert_normalized(
    const inferenceos_vfs_normalized_path *base,
    inferenceos_vfs_path input,
    const char *expected,
    inferenceos_size expected_components
)
{
    inferenceos_vfs_normalized_path result;
    inferenceos_size expected_length = 0U;

    while (expected[expected_length] != '\0') {
        ++expected_length;
    }

    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_STATUS_OK,
        inferenceos_vfs_path_normalize(base, input, &result));
    INFERENCEOS_TEST_ASSERT_STRING_EQUAL(expected, result.text);
    INFERENCEOS_TEST_ASSERT_SIZE_EQUAL(expected_length, result.length);
    INFERENCEOS_TEST_ASSERT_SIZE_EQUAL(
        expected_components, result.component_count);
}

static void test_absolute_normalization(void)
{
    inferenceos_vfs_normalized_path invalid_base;

    (void)memset(&invalid_base, 0, sizeof(invalid_base));
    assert_normalized(NULL, TEST_PATH_LITERAL("/"), "/", 0U);
    assert_normalized(NULL, TEST_PATH_LITERAL("///DOCS//./SUB/../NOTE.TXT/"),
        "/DOCS/NOTE.TXT", 2U);
    assert_normalized(&invalid_base, TEST_PATH_LITERAL("/ROOT"), "/ROOT", 1U);
}

static void test_relative_normalization(void)
{
    inferenceos_vfs_normalized_path base;

    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_STATUS_OK,
        inferenceos_vfs_path_normalize(
            NULL, TEST_PATH_LITERAL("/DOCS/SUB"), &base));
    assert_normalized(&base, TEST_PATH_LITERAL(".././NOTE.TXT"),
        "/DOCS/NOTE.TXT", 2U);
    assert_normalized(&base, TEST_PATH_LITERAL("CHILD"),
        "/DOCS/SUB/CHILD", 3U);
    assert_normalized(NULL, TEST_PATH_LITERAL("RELATIVE"),
        "/RELATIVE", 1U);
}

static void test_root_parent_is_confined(void)
{
    inferenceos_vfs_normalized_path result;

    assert_normalized(NULL, TEST_PATH_LITERAL(".."), "/", 0U);
    assert_normalized(NULL, TEST_PATH_LITERAL("/../../DOCS/.."), "/", 0U);
    assert_normalized(NULL, TEST_PATH_LITERAL("../../../SAFE"), "/SAFE", 1U);

    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_STATUS_OK,
        inferenceos_vfs_path_normalize(
            NULL, TEST_PATH_LITERAL("/../../.."), &result));
    INFERENCEOS_TEST_ASSERT(inferenceos_vfs_path_is_root(&result));
}

static void test_component_access(void)
{
    inferenceos_vfs_normalized_path result;
    inferenceos_vfs_path component;

    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_STATUS_OK,
        inferenceos_vfs_path_normalize(
            NULL, TEST_PATH_LITERAL("/DOCS/NOTE.TXT"), &result));
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_STATUS_OK,
        inferenceos_vfs_path_component(&result, 0U, &component));
    INFERENCEOS_TEST_ASSERT_SIZE_EQUAL(4U, component.length);
    INFERENCEOS_TEST_ASSERT_MEMORY_EQUAL("DOCS", component.data, 4U);
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_STATUS_OK,
        inferenceos_vfs_path_component(&result, 1U, &component));
    INFERENCEOS_TEST_ASSERT_SIZE_EQUAL(8U, component.length);
    INFERENCEOS_TEST_ASSERT_MEMORY_EQUAL("NOTE.TXT", component.data, 8U);
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_STATUS_OUT_OF_RANGE,
        inferenceos_vfs_path_component(&result, 2U, &component));
    INFERENCEOS_TEST_ASSERT_FALSE(inferenceos_vfs_path_is_root(&result));
}

static void test_path_length_boundaries(void)
{
    char maximum[INFERENCEOS_VFS_PATH_CAPACITY];
    char overlong[INFERENCEOS_VFS_PATH_CAPACITY];
    inferenceos_vfs_normalized_path output;
    inferenceos_vfs_normalized_path unchanged;

    maximum[0] = '/';
    for (inferenceos_size index = 1U;
         index < INFERENCEOS_VFS_MAX_PATH_LENGTH;
         ++index) {
        maximum[index] = 'A';
    }
    maximum[INFERENCEOS_VFS_MAX_PATH_LENGTH] = '\0';
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_STATUS_OK,
        inferenceos_vfs_path_normalize(NULL,
            (inferenceos_vfs_path) {
                maximum,
                INFERENCEOS_VFS_MAX_PATH_LENGTH
            },
            &output));
    INFERENCEOS_TEST_ASSERT_SIZE_EQUAL(
        INFERENCEOS_VFS_MAX_PATH_LENGTH, output.length);

    overlong[0] = '/';
    for (inferenceos_size index = 1U;
         index < INFERENCEOS_VFS_PATH_CAPACITY;
         ++index) {
        overlong[index] = 'B';
    }
    (void)memset(&output, 0xA5, sizeof(output));
    (void)memset(&unchanged, 0xA5, sizeof(unchanged));
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_STATUS_OUT_OF_RANGE,
        inferenceos_vfs_path_normalize(NULL,
            (inferenceos_vfs_path) {
                overlong,
                INFERENCEOS_VFS_PATH_CAPACITY
            },
            &output));
    INFERENCEOS_TEST_ASSERT_MEMORY_EQUAL(
        &unchanged, &output, sizeof(output));
}

static inferenceos_size build_depth_path(char *path, inferenceos_size depth)
{
    inferenceos_size length = 0U;

    for (inferenceos_size index = 0U; index < depth; ++index) {
        path[length] = '/';
        path[length + 1U] = 'A';
        length += 2U;
    }
    return length;
}

static void test_directory_depth_boundaries(void)
{
    char path[(INFERENCEOS_VFS_MAX_DIRECTORY_LEVELS + 1U) * 2U];
    inferenceos_vfs_normalized_path output;
    inferenceos_vfs_normalized_path unchanged;
    inferenceos_size length = build_depth_path(
        path, INFERENCEOS_VFS_MAX_DIRECTORY_LEVELS);

    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_STATUS_OK,
        inferenceos_vfs_path_normalize(NULL,
            (inferenceos_vfs_path) { path, length }, &output));
    INFERENCEOS_TEST_ASSERT_SIZE_EQUAL(
        INFERENCEOS_VFS_MAX_DIRECTORY_LEVELS, output.component_count);

    length = build_depth_path(path, INFERENCEOS_VFS_MAX_DIRECTORY_LEVELS + 1U);
    (void)memset(&output, 0xA5, sizeof(output));
    (void)memset(&unchanged, 0xA5, sizeof(unchanged));
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_STATUS_OUT_OF_RANGE,
        inferenceos_vfs_path_normalize(NULL,
            (inferenceos_vfs_path) { path, length }, &output));
    INFERENCEOS_TEST_ASSERT_MEMORY_EQUAL(
        &unchanged, &output, sizeof(output));
}

static void test_malformed_paths_fail_transactionally(void)
{
    const char embedded_null[3] = { 'A', '\0', 'B' };
    inferenceos_vfs_normalized_path invalid_base;
    inferenceos_vfs_normalized_path output;
    inferenceos_vfs_normalized_path unchanged;

    (void)memset(&output, 0xA5, sizeof(output));
    (void)memset(&unchanged, 0xA5, sizeof(unchanged));
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_STATUS_INVALID_PATH,
        inferenceos_vfs_path_normalize(NULL,
            (inferenceos_vfs_path) { "", 0U }, &output));
    INFERENCEOS_TEST_ASSERT_MEMORY_EQUAL(
        &unchanged, &output, sizeof(output));

    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_STATUS_INVALID_PATH,
        inferenceos_vfs_path_normalize(NULL,
            (inferenceos_vfs_path) { embedded_null, 3U }, &output));
    INFERENCEOS_TEST_ASSERT_MEMORY_EQUAL(
        &unchanged, &output, sizeof(output));

    (void)memset(&invalid_base, 0, sizeof(invalid_base));
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_VFS_STATUS_INVALID_PATH,
        inferenceos_vfs_path_normalize(
            &invalid_base, TEST_PATH_LITERAL("RELATIVE"), &output));
    INFERENCEOS_TEST_ASSERT_MEMORY_EQUAL(
        &unchanged, &output, sizeof(output));
}

static const inferenceos_test_case path_cases[] = {
    INFERENCEOS_TEST_CASE(test_absolute_normalization),
    INFERENCEOS_TEST_CASE(test_relative_normalization),
    INFERENCEOS_TEST_CASE(test_root_parent_is_confined),
    INFERENCEOS_TEST_CASE(test_component_access),
    INFERENCEOS_TEST_CASE(test_path_length_boundaries),
    INFERENCEOS_TEST_CASE(test_directory_depth_boundaries),
    INFERENCEOS_TEST_CASE(test_malformed_paths_fail_transactionally)
};

const inferenceos_test_suite *inferenceos_test_suite_definition(void)
{
    static const inferenceos_test_suite suite = {
        .name = "vfs-path",
        .cases = path_cases,
        .case_count = INFERENCEOS_ARRAY_COUNT(path_cases)
    };
    return &suite;
}
