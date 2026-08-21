#ifndef INFERENCEOS_ENDIAN_H
#define INFERENCEOS_ENDIAN_H

#include <inferenceos/base.h>

/* These helpers intentionally access one byte at a time. They are safe for
 * unaligned on-disk fields and do not depend on the compiler's structure
 * packing, integer alignment, or host byte order. The caller must provide a
 * valid buffer containing at least 2, 4, or 8 bytes respectively. */

static inline inferenceos_u16 inferenceos_load_le16(const void *source)
{
    const inferenceos_u8 *bytes = source;
    return (inferenceos_u16)(
        (inferenceos_u16)bytes[0]
        | ((inferenceos_u16)bytes[1] << 8U)
    );
}

static inline inferenceos_u32 inferenceos_load_le32(const void *source)
{
    const inferenceos_u8 *bytes = source;
    return (inferenceos_u32)(
        (inferenceos_u32)bytes[0]
        | ((inferenceos_u32)bytes[1] << 8U)
        | ((inferenceos_u32)bytes[2] << 16U)
        | ((inferenceos_u32)bytes[3] << 24U)
    );
}

static inline inferenceos_u64 inferenceos_load_le64(const void *source)
{
    const inferenceos_u8 *bytes = source;
    return (inferenceos_u64)(
        (inferenceos_u64)bytes[0]
        | ((inferenceos_u64)bytes[1] << 8U)
        | ((inferenceos_u64)bytes[2] << 16U)
        | ((inferenceos_u64)bytes[3] << 24U)
        | ((inferenceos_u64)bytes[4] << 32U)
        | ((inferenceos_u64)bytes[5] << 40U)
        | ((inferenceos_u64)bytes[6] << 48U)
        | ((inferenceos_u64)bytes[7] << 56U)
    );
}

static inline void inferenceos_store_le16(void *destination, inferenceos_u16 value)
{
    inferenceos_u8 *bytes = destination;
    bytes[0] = (inferenceos_u8)value;
    bytes[1] = (inferenceos_u8)(value >> 8U);
}

static inline void inferenceos_store_le32(void *destination, inferenceos_u32 value)
{
    inferenceos_u8 *bytes = destination;
    bytes[0] = (inferenceos_u8)value;
    bytes[1] = (inferenceos_u8)(value >> 8U);
    bytes[2] = (inferenceos_u8)(value >> 16U);
    bytes[3] = (inferenceos_u8)(value >> 24U);
}

static inline void inferenceos_store_le64(void *destination, inferenceos_u64 value)
{
    inferenceos_u8 *bytes = destination;
    bytes[0] = (inferenceos_u8)value;
    bytes[1] = (inferenceos_u8)(value >> 8U);
    bytes[2] = (inferenceos_u8)(value >> 16U);
    bytes[3] = (inferenceos_u8)(value >> 24U);
    bytes[4] = (inferenceos_u8)(value >> 32U);
    bytes[5] = (inferenceos_u8)(value >> 40U);
    bytes[6] = (inferenceos_u8)(value >> 48U);
    bytes[7] = (inferenceos_u8)(value >> 56U);
}

#endif
