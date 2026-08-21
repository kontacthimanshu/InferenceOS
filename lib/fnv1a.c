#include <inferenceos/fnv1a.h>

inferenceos_u32 inferenceos_fnv1a32_begin(void)
{
    return INFERENCEOS_FNV1A32_OFFSET_BASIS;
}

inferenceos_u32 inferenceos_fnv1a32_update(
    inferenceos_u32 state,
    const void *data,
    inferenceos_size length
)
{
    const inferenceos_u8 *bytes = data;

    for (inferenceos_size index = 0U; index < length; ++index) {
        state ^= (inferenceos_u32)bytes[index];
        state *= INFERENCEOS_FNV1A32_PRIME;
    }
    return state;
}

inferenceos_u32 inferenceos_fnv1a32(const void *data, inferenceos_size length)
{
    return inferenceos_fnv1a32_update(inferenceos_fnv1a32_begin(), data, length);
}
