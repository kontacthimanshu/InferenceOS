#ifndef INFERENCEOS_BASE_H
#define INFERENCEOS_BASE_H

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <inferenceos/compiler.h>

typedef uint8_t ios_u8;
typedef uint16_t ios_u16;
typedef uint32_t ios_u32;
typedef uint64_t ios_u64;

typedef int8_t ios_i8;
typedef int16_t ios_i16;
typedef int32_t ios_i32;
typedef int64_t ios_i64;

typedef uintptr_t ios_uptr;
typedef intptr_t ios_iptr;
typedef size_t ios_size;
typedef ptrdiff_t ios_ptrdiff;

_Static_assert(CHAR_BIT == 8, "InferenceOS requires 8-bit bytes");
_Static_assert(sizeof(ios_u8) == 1, "ios_u8 must be 8 bits");
_Static_assert(sizeof(ios_u16) == 2, "ios_u16 must be 16 bits");
_Static_assert(sizeof(ios_u32) == 4, "ios_u32 must be 32 bits");
_Static_assert(sizeof(ios_u64) == 8, "ios_u64 must be 64 bits");
_Static_assert(sizeof(ios_i8) == 1, "ios_i8 must be 8 bits");
_Static_assert(sizeof(ios_i16) == 2, "ios_i16 must be 16 bits");
_Static_assert(sizeof(ios_i32) == 4, "ios_i32 must be 32 bits");
_Static_assert(sizeof(ios_i64) == 8, "ios_i64 must be 64 bits");
_Static_assert(sizeof(ios_uptr) == 8, "InferenceOS requires 64-bit pointers");

#define IOS_ARRAY_COUNT(array) (sizeof(array) / sizeof(*(array)))
#define IOS_STATIC_ASSERT(condition, message) _Static_assert((condition), message)

/* Implemented by the kernel panic path in T014. Assertions are always active. */
_Noreturn void ios_assertion_failed(
    const char *expression,
    const char *source_file,
    ios_u32 source_line
);

#define IOS_ASSERT(condition)                                                     \
    do {                                                                          \
        if (!(condition)) {                                                       \
            ios_assertion_failed(#condition, __FILE__, (ios_u32)__LINE__);        \
        }                                                                         \
    } while (0)

#endif
