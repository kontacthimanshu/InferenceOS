#include <inferenceos/fs/format.h>

ios_u32 ios_fs_crc32_iso_hdlc(const void *bytes, ios_size length)
{
    const ios_u8 *input = bytes;
    ios_u32 crc = UINT32_C(0xffffffff);
    if (bytes == NULL && length != 0) return 0;
    for (ios_size index = 0; index < length; ++index) {
        crc ^= input[index];
        for (ios_u32 bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1U) != 0U ? UINT32_C(0xedb88320) : 0U);
        }
    }
    return crc ^ UINT32_C(0xffffffff);
}
