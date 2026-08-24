#include <inferenceos/fs/records.h>

ios_u32 ios_fs_fnv1a32(const void *bytes, ios_size length)
{
    const ios_u8 *input = bytes;
    ios_u32 hash = UINT32_C(0x811c9dc5);
    if (bytes == NULL && length != 0) return 0;
    for (ios_size index = 0; index < length; ++index) {
        hash ^= input[index];
        hash *= UINT32_C(0x01000193);
    }
    return hash;
}

void ios_fs_hash_text(ios_u32 hash, ios_u8 output[IOS_FS_HASH_TEXT_SIZE])
{
    static const ios_u8 digits[] = "0123456789ABCDEF";
    if (output == NULL) return;
    for (ios_size index = 0; index < IOS_FS_HASH_TEXT_SIZE; ++index) {
        output[IOS_FS_HASH_TEXT_SIZE - 1 - index] = digits[hash & UINT32_C(0x0f)];
        hash >>= 4;
    }
}
