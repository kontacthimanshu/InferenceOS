#include "test_assert.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool current_test_failed;

static void report_location(const char *file, uint32_t line)
{
    (void)fprintf(stderr, "    %s:%" PRIu32 ": ", file, line);
    current_test_failed = true;
}

bool inferenceos_test_assert_true_impl(
    bool condition,
    const char *expression,
    const char *file,
    uint32_t line
)
{
    if (condition) {
        return true;
    }
    report_location(file, line);
    (void)fprintf(stderr, "assertion failed: %s\n", expression);
    return false;
}

bool inferenceos_test_assert_u64_equal_impl(
    uint64_t expected,
    uint64_t actual,
    const char *expected_expression,
    const char *actual_expression,
    const char *file,
    uint32_t line
)
{
    if (expected == actual) {
        return true;
    }
    report_location(file, line);
    (void)fprintf(stderr,
        "expected %s = 0x%016" PRIx64 ", got %s = 0x%016" PRIx64 "\n",
        expected_expression, expected, actual_expression, actual);
    return false;
}

bool inferenceos_test_assert_i64_equal_impl(
    int64_t expected,
    int64_t actual,
    const char *expected_expression,
    const char *actual_expression,
    const char *file,
    uint32_t line
)
{
    if (expected == actual) {
        return true;
    }
    report_location(file, line);
    (void)fprintf(stderr,
        "expected %s = %" PRId64 ", got %s = %" PRId64 "\n",
        expected_expression, expected, actual_expression, actual);
    return false;
}

bool inferenceos_test_assert_pointer_equal_impl(
    const void *expected,
    const void *actual,
    const char *expected_expression,
    const char *actual_expression,
    const char *file,
    uint32_t line
)
{
    if (expected == actual) {
        return true;
    }
    report_location(file, line);
    (void)fprintf(stderr, "expected %s = %p, got %s = %p\n",
        expected_expression, expected, actual_expression, actual);
    return false;
}

bool inferenceos_test_assert_memory_equal_impl(
    const void *expected,
    const void *actual,
    size_t size,
    const char *expected_expression,
    const char *actual_expression,
    const char *file,
    uint32_t line
)
{
    const unsigned char *expected_bytes = expected;
    const unsigned char *actual_bytes = actual;

    if (size == 0U) {
        return true;
    }
    if (expected == NULL || actual == NULL) {
        report_location(file, line);
        (void)fprintf(stderr,
            "cannot compare %zu bytes: %s = %p, %s = %p\n",
            size, expected_expression, expected, actual_expression, actual);
        return false;
    }
    for (size_t index = 0U; index < size; ++index) {
        if (expected_bytes[index] != actual_bytes[index]) {
            report_location(file, line);
            (void)fprintf(stderr,
                "memory differs at byte %zu: %s = 0x%02x, %s = 0x%02x\n",
                index, expected_expression, (unsigned int)expected_bytes[index],
                actual_expression, (unsigned int)actual_bytes[index]);
            return false;
        }
    }
    return true;
}

bool inferenceos_test_assert_string_equal_impl(
    const char *expected,
    const char *actual,
    const char *expected_expression,
    const char *actual_expression,
    const char *file,
    uint32_t line
)
{
    if (expected == actual) {
        return true;
    }
    if (expected != NULL && actual != NULL && strcmp(expected, actual) == 0) {
        return true;
    }
    report_location(file, line);
    (void)fprintf(stderr, "expected %s = \"%s\", got %s = \"%s\"\n",
        expected_expression, expected == NULL ? "(null)" : expected,
        actual_expression, actual == NULL ? "(null)" : actual);
    return false;
}

static bool test_name_matches(const char *filter, const char *name)
{
    return filter == NULL || strcmp(filter, name) == 0;
}

static int list_tests(const inferenceos_test_suite *suite)
{
    for (size_t index = 0U; index < suite->case_count; ++index) {
        (void)printf("%s.%s\n", suite->name, suite->cases[index].name);
    }
    return EXIT_SUCCESS;
}

int main(int argument_count, char **arguments)
{
    const inferenceos_test_suite *suite = inferenceos_test_suite_definition();
    const char *filter = NULL;
    size_t selected = 0U;
    size_t passed = 0U;
    size_t failed = 0U;

    if (suite == NULL || suite->name == NULL || suite->cases == NULL
        || suite->case_count == 0U) {
        (void)fprintf(stderr, "invalid or empty test suite\n");
        return EXIT_FAILURE;
    }
    if (argument_count == 2 && strcmp(arguments[1], "--list") == 0) {
        return list_tests(suite);
    }
    if (argument_count > 2) {
        (void)fprintf(stderr, "usage: %s [--list|test-name]\n", arguments[0]);
        return EXIT_FAILURE;
    }
    if (argument_count == 2) {
        filter = arguments[1];
    }

    (void)printf("suite %s: %zu test(s)\n", suite->name, suite->case_count);
    for (size_t index = 0U; index < suite->case_count; ++index) {
        const inferenceos_test_case *test_case = &suite->cases[index];

        if (test_case->name == NULL || test_case->function == NULL
            || !test_name_matches(filter, test_case->name)) {
            continue;
        }
        ++selected;
        current_test_failed = false;
        (void)printf("  RUN  %s\n", test_case->name);
        test_case->function();
        if (current_test_failed) {
            ++failed;
            (void)printf("  FAIL %s\n", test_case->name);
        } else {
            ++passed;
            (void)printf("  PASS %s\n", test_case->name);
        }
    }

    if (selected == 0U) {
        (void)fprintf(stderr, "no test matched '%s'\n",
            filter == NULL ? "" : filter);
        return EXIT_FAILURE;
    }
    (void)printf("summary: %zu passed, %zu failed\n", passed, failed);
    return failed == 0U ? EXIT_SUCCESS : EXIT_FAILURE;
}
