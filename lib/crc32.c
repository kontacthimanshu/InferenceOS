#include <inferenceos/crc32.h>

inferenceos_u32 inferenceos_crc32_begin(void)
{
    return INFERENCEOS_CRC32_INITIAL;
}

inferenceos_u32 inferenceos_crc32_update(
    inferenceos_u32 state,
    const void *data,
    inferenceos_size length
)
{
    const inferenceos_u8 *bytes = data;

    for (inferenceos_size index = 0U; index < length; ++index) {
        state ^= (inferenceos_u32)bytes[index];
        for (inferenceos_u32 bit = 0U; bit < 8U; ++bit) {
            const inferenceos_u32 low_bit_mask = 0U - (state & 1U);
            state = (state >> 1U)
                ^ (INFERENCEOS_CRC32_REFLECTED_POLYNOMIAL & low_bit_mask);
        }
    }
    return state;
}

inferenceos_u32 inferenceos_crc32_finish(inferenceos_u32 state)
{
    return state ^ INFERENCEOS_CRC32_FINAL_XOR;
}

inferenceos_u32 inferenceos_crc32(const void *data, inferenceos_size length)
{
    return inferenceos_crc32_finish(
        inferenceos_crc32_update(inferenceos_crc32_begin(), data, length)
    );
}
