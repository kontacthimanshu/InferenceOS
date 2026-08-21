#ifndef INFERENCEOS_TEST_ASSERT_H
#define INFERENCEOS_TEST_ASSERT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*inferenceos_test_function)(void);

typedef struct inferenceos_test_case {
    const char *name;
    inferenceos_test_function function;
} inferenceos_test_case;

typedef struct inferenceos_test_suite {
    const char *name;
    const inferenceos_test_case *cases;
    size_t case_count;
} inferenceos_test_suite;

/* Every test executable supplies exactly one suite through this symbol. */
const inferenceos_test_suite *inferenceos_test_suite_definition(void);

bool inferenceos_test_assert_true_impl(
    bool condition,
    const char *expression,
    const char *file,
    uint32_t line
);

bool inferenceos_test_assert_u64_equal_impl(
    uint64_t expected,
    uint64_t actual,
    const char *expected_expression,
    const char *actual_expression,
    const char *file,
    uint32_t line
);

bool inferenceos_test_assert_i64_equal_impl(
    int64_t expected,
    int64_t actual,
    const char *expected_expression,
    const char *actual_expression,
    const char *file,
    uint32_t line
);

bool inferenceos_test_assert_pointer_equal_impl(
    const void *expected,
    const void *actual,
    const char *expected_expression,
    const char *actual_expression,
    const char *file,
    uint32_t line
);

bool inferenceos_test_assert_memory_equal_impl(
    const void *expected,
    const void *actual,
    size_t size,
    const char *expected_expression,
    const char *actual_expression,
    const char *file,
    uint32_t line
);

bool inferenceos_test_assert_string_equal_impl(
    const char *expected,
    const char *actual,
    const char *expected_expression,
    const char *actual_expression,
    const char *file,
    uint32_t line
);

#define INFERENCEOS_TEST_CASE(function_name) { #function_name, (function_name) }

#define INFERENCEOS_TEST_ASSERT(expression)                                    \
    do {                                                                        \
        if (!inferenceos_test_assert_true_impl(                                 \
                (expression), #expression, __FILE__, (uint32_t)__LINE__)) {      \
            return;                                                             \
        }                                                                       \
    } while (0)

#define INFERENCEOS_TEST_ASSERT_FALSE(expression)                              \
    INFERENCEOS_TEST_ASSERT(!(expression))

#define INFERENCEOS_TEST_ASSERT_U64_EQUAL(expected, actual)                    \
    do {                                                                        \
        if (!inferenceos_test_assert_u64_equal_impl(                            \
                (uint64_t)(expected), (uint64_t)(actual),                       \
                #expected, #actual, __FILE__, (uint32_t)__LINE__)) {             \
            return;                                                             \
        }                                                                       \
    } while (0)

#define INFERENCEOS_TEST_ASSERT_I64_EQUAL(expected, actual)                    \
    do {                                                                        \
        if (!inferenceos_test_assert_i64_equal_impl(                            \
                (int64_t)(expected), (int64_t)(actual),                         \
                #expected, #actual, __FILE__, (uint32_t)__LINE__)) {             \
            return;                                                             \
        }                                                                       \
    } while (0)

#define INFERENCEOS_TEST_ASSERT_SIZE_EQUAL(expected, actual)                   \
    INFERENCEOS_TEST_ASSERT_U64_EQUAL((size_t)(expected), (size_t)(actual))

#define INFERENCEOS_TEST_ASSERT_POINTER_EQUAL(expected, actual)                \
    do {                                                                        \
        if (!inferenceos_test_assert_pointer_equal_impl(                        \
                (const void *)(expected), (const void *)(actual),               \
                #expected, #actual, __FILE__, (uint32_t)__LINE__)) {             \
            return;                                                             \
        }                                                                       \
    } while (0)

#define INFERENCEOS_TEST_ASSERT_NULL(actual)                                   \
    INFERENCEOS_TEST_ASSERT_POINTER_EQUAL(NULL, (actual))

#define INFERENCEOS_TEST_ASSERT_NOT_NULL(actual)                               \
    INFERENCEOS_TEST_ASSERT((actual) != NULL)

#define INFERENCEOS_TEST_ASSERT_MEMORY_EQUAL(expected, actual, size)           \
    do {                                                                        \
        if (!inferenceos_test_assert_memory_equal_impl(                         \
                (expected), (actual), (size_t)(size),                           \
                #expected, #actual, __FILE__, (uint32_t)__LINE__)) {             \
            return;                                                             \
        }                                                                       \
    } while (0)

#define INFERENCEOS_TEST_ASSERT_STRING_EQUAL(expected, actual)                 \
    do {                                                                        \
        if (!inferenceos_test_assert_string_equal_impl(                         \
                (expected), (actual), #expected, #actual,                       \
                __FILE__, (uint32_t)__LINE__)) {                                \
            return;                                                             \
        }                                                                       \
    } while (0)

#endif
