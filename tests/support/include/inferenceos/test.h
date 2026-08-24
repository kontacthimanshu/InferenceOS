#ifndef INFERENCEOS_TEST_H
#define INFERENCEOS_TEST_H

#include <stddef.h>

typedef void (*ios_test_function)(void);

struct ios_test_case {
    const char *name;
    ios_test_function function;
};

extern const struct ios_test_case ios_test_cases[];
extern const size_t ios_test_case_count;

_Noreturn void ios_test_fail(
    const char *expression,
    const char *file,
    unsigned long line
);

#define IOS_TEST_ASSERT(expression)                                               \
    do {                                                                          \
        if (!(expression)) {                                                       \
            ios_test_fail(#expression, __FILE__, (unsigned long)__LINE__);         \
        }                                                                          \
    } while (0)

#define IOS_TEST_ASSERT_STATUS(actual, expected)                                  \
    IOS_TEST_ASSERT((actual) == (expected))

#define IOS_TEST_CASE(function_name) { #function_name, function_name }

#endif
