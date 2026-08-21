#ifndef INFERENCEOS_CRC32_H
#define INFERENCEOS_CRC32_H

#include <inferenceos/base.h>

#define INFERENCEOS_CRC32_INITIAL UINT32_C(0xFFFFFFFF)
#define INFERENCEOS_CRC32_REFLECTED_POLYNOMIAL UINT32_C(0xEDB88320)
#define INFERENCEOS_CRC32_FINAL_XOR UINT32_C(0xFFFFFFFF)

/* The incremental state is the CRC value before the final XOR. data may be
 * NULL only when length is zero. */
inferenceos_u32 inferenceos_crc32_begin(void);
inferenceos_u32 inferenceos_crc32_update(
    inferenceos_u32 state,
    const void *data,
    inferenceos_size length
);
inferenceos_u32 inferenceos_crc32_finish(inferenceos_u32 state);
inferenceos_u32 inferenceos_crc32(const void *data, inferenceos_size length);

#endif
