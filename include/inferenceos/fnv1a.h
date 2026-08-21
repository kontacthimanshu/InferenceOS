#ifndef INFERENCEOS_FNV1A_H
#define INFERENCEOS_FNV1A_H

#include <inferenceos/base.h>

#define INFERENCEOS_FNV1A32_OFFSET_BASIS UINT32_C(0x811C9DC5)
#define INFERENCEOS_FNV1A32_PRIME UINT32_C(0x01000193)

/* data may be NULL only when length is zero. */
inferenceos_u32 inferenceos_fnv1a32_begin(void);
inferenceos_u32 inferenceos_fnv1a32_update(
    inferenceos_u32 state,
    const void *data,
    inferenceos_size length
);
inferenceos_u32 inferenceos_fnv1a32(const void *data, inferenceos_size length);

#endif
