#ifndef INFERENCEOS_BASE_H
#define INFERENCEOS_BASE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>

#if !defined(__GNUC__) && !defined(__clang__)
#error "InferenceOS supports only GCC and Clang"
#endif

typedef uint8_t inferenceos_u8;
typedef uint16_t inferenceos_u16;
typedef uint32_t inferenceos_u32;
typedef uint64_t inferenceos_u64;
typedef int8_t inferenceos_i8;
typedef int16_t inferenceos_i16;
typedef int32_t inferenceos_i32;
typedef int64_t inferenceos_i64;
typedef uintptr_t inferenceos_uptr;
typedef intptr_t inferenceos_iptr;
typedef size_t inferenceos_size;
typedef ptrdiff_t inferenceos_ssize;

_Static_assert(CHAR_BIT == 8, "InferenceOS requires 8-bit bytes");
_Static_assert(sizeof(inferenceos_u8) == 1, "u8 must be 1 byte");
_Static_assert(sizeof(inferenceos_u16) == 2, "u16 must be 2 bytes");
_Static_assert(sizeof(inferenceos_u32) == 4, "u32 must be 4 bytes");
_Static_assert(sizeof(inferenceos_u64) == 8, "u64 must be 8 bytes");
_Static_assert(sizeof(inferenceos_uptr) == 8, "InferenceOS requires 64-bit pointers");

#define INFERENCEOS_PACKED __attribute__((packed))
#define INFERENCEOS_ALIGNED(bytes) _Alignas(bytes)
#define INFERENCEOS_SECTION(name) __attribute__((section(name)))
#define INFERENCEOS_NORETURN _Noreturn
#define INFERENCEOS_USED __attribute__((used))

#if defined(__clang__)
#if __has_attribute(retain)
#define INFERENCEOS_RETAIN __attribute__((retain))
#else
#define INFERENCEOS_RETAIN INFERENCEOS_USED
#endif
#elif defined(__GNUC__) && (__GNUC__ >= 11)
#define INFERENCEOS_RETAIN __attribute__((retain))
#else
#define INFERENCEOS_RETAIN INFERENCEOS_USED
#endif

#define INFERENCEOS_STATIC_ASSERT(condition, message) _Static_assert((condition), message)
#define INFERENCEOS_ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))
#define INFERENCEOS_OFFSETOF(type, member) offsetof(type, member)

/* A compiler barrier does not emit a CPU fence. Hardware ordering belongs in
 * a documented architecture wrapper. */
#define INFERENCEOS_COMPILER_BARRIER() __asm__ __volatile__("" ::: "memory")

INFERENCEOS_NORETURN void inferenceos_assert_fail(
    const char *expression,
    const char *file,
    inferenceos_u32 line
);

#define INFERENCEOS_ASSERT(condition)                                              \
    do {                                                                           \
        if (!(condition)) {                                                        \
            inferenceos_assert_fail(#condition, __FILE__, (inferenceos_u32)__LINE__); \
        }                                                                          \
    } while (0)

#if defined(NDEBUG)
#define INFERENCEOS_DEBUG_ASSERT(condition) ((void)sizeof(condition))
#else
#define INFERENCEOS_DEBUG_ASSERT(condition) INFERENCEOS_ASSERT(condition)
#endif

static inline bool inferenceos_is_power_of_two_size(inferenceos_size value)
{
    return value != 0U && (value & (value - 1U)) == 0U;
}

static inline bool inferenceos_checked_add_u32(
    inferenceos_u32 left,
    inferenceos_u32 right,
    inferenceos_u32 *result
)
{
    if (result == NULL || right > UINT32_MAX - left) {
        return false;
    }
    *result = left + right;
    return true;
}

static inline bool inferenceos_checked_mul_u32(
    inferenceos_u32 left,
    inferenceos_u32 right,
    inferenceos_u32 *result
)
{
    if (result == NULL || (left != 0U && right > UINT32_MAX / left)) {
        return false;
    }
    *result = left * right;
    return true;
}

static inline bool inferenceos_checked_add_u64(
    inferenceos_u64 left,
    inferenceos_u64 right,
    inferenceos_u64 *result
)
{
    if (result == NULL || right > UINT64_MAX - left) {
        return false;
    }
    *result = left + right;
    return true;
}

static inline bool inferenceos_checked_sub_u64(
    inferenceos_u64 left,
    inferenceos_u64 right,
    inferenceos_u64 *result
)
{
    if (result == NULL || right > left) {
        return false;
    }
    *result = left - right;
    return true;
}

static inline bool inferenceos_checked_mul_u64(
    inferenceos_u64 left,
    inferenceos_u64 right,
    inferenceos_u64 *result
)
{
    if (result == NULL || (left != 0U && right > UINT64_MAX / left)) {
        return false;
    }
    *result = left * right;
    return true;
}

static inline bool inferenceos_checked_add_size(
    inferenceos_size left,
    inferenceos_size right,
    inferenceos_size *result
)
{
    if (result == NULL || right > SIZE_MAX - left) {
        return false;
    }
    *result = left + right;
    return true;
}

static inline bool inferenceos_checked_mul_size(
    inferenceos_size left,
    inferenceos_size right,
    inferenceos_size *result
)
{
    if (result == NULL || (left != 0U && right > SIZE_MAX / left)) {
        return false;
    }
    *result = left * right;
    return true;
}

static inline bool inferenceos_checked_align_up_size(
    inferenceos_size value,
    inferenceos_size alignment,
    inferenceos_size *result
)
{
    inferenceos_size adjusted;

    if (!inferenceos_is_power_of_two_size(alignment)) {
        return false;
    }
    if (!inferenceos_checked_add_size(value, alignment - 1U, &adjusted)) {
        return false;
    }
    if (result == NULL) {
        return false;
    }
    *result = adjusted & ~(alignment - 1U);
    return true;
}

static inline bool inferenceos_range_within_u64(
    inferenceos_u64 start,
    inferenceos_u64 count,
    inferenceos_u64 limit
)
{
    inferenceos_u64 end;
    return inferenceos_checked_add_u64(start, count, &end) && end <= limit;
}

#endif
