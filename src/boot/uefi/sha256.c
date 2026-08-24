#include "loader.h"

#include <inferenceos/runtime.h>

struct sha256_context {
    ios_u32 state[8];
    ios_u64 byte_count;
    ios_u8 block[64];
    ios_size block_size;
};

static const ios_u32 round_constants[64] = {
    UINT32_C(0x428a2f98), UINT32_C(0x71374491), UINT32_C(0xb5c0fbcf), UINT32_C(0xe9b5dba5),
    UINT32_C(0x3956c25b), UINT32_C(0x59f111f1), UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5),
    UINT32_C(0xd807aa98), UINT32_C(0x12835b01), UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
    UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe), UINT32_C(0x9bdc06a7), UINT32_C(0xc19bf174),
    UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786), UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc),
    UINT32_C(0x2de92c6f), UINT32_C(0x4a7484aa), UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da),
    UINT32_C(0x983e5152), UINT32_C(0xa831c66d), UINT32_C(0xb00327c8), UINT32_C(0xbf597fc7),
    UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147), UINT32_C(0x06ca6351), UINT32_C(0x14292967),
    UINT32_C(0x27b70a85), UINT32_C(0x2e1b2138), UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
    UINT32_C(0x650a7354), UINT32_C(0x766a0abb), UINT32_C(0x81c2c92e), UINT32_C(0x92722c85),
    UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b), UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3),
    UINT32_C(0xd192e819), UINT32_C(0xd6990624), UINT32_C(0xf40e3585), UINT32_C(0x106aa070),
    UINT32_C(0x19a4c116), UINT32_C(0x1e376c08), UINT32_C(0x2748774c), UINT32_C(0x34b0bcb5),
    UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a), UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3),
    UINT32_C(0x748f82ee), UINT32_C(0x78a5636f), UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
    UINT32_C(0x90befffa), UINT32_C(0xa4506ceb), UINT32_C(0xbef9a3f7), UINT32_C(0xc67178f2)
};

static ios_u32 rotate_right(ios_u32 value, ios_u32 count)
{
    return (value >> count) | (value << (32U - count));
}

static void transform(struct sha256_context *context, const ios_u8 block[64])
{
    ios_u32 words[64];
    ios_u32 working[8];
    for (ios_size index = 0; index < 16; ++index) {
        words[index] = ((ios_u32)block[index * 4] << 24)
            | ((ios_u32)block[index * 4 + 1] << 16)
            | ((ios_u32)block[index * 4 + 2] << 8)
            | block[index * 4 + 3];
    }
    for (ios_size index = 16; index < 64; ++index) {
        const ios_u32 s0 = rotate_right(words[index - 15], 7)
            ^ rotate_right(words[index - 15], 18) ^ (words[index - 15] >> 3);
        const ios_u32 s1 = rotate_right(words[index - 2], 17)
            ^ rotate_right(words[index - 2], 19) ^ (words[index - 2] >> 10);
        words[index] = words[index - 16] + s0 + words[index - 7] + s1;
    }
    memcpy(working, context->state, sizeof(working));
    for (ios_size index = 0; index < 64; ++index) {
        const ios_u32 sum1 = rotate_right(working[4], 6) ^ rotate_right(working[4], 11)
            ^ rotate_right(working[4], 25);
        const ios_u32 choice = (working[4] & working[5]) ^ (~working[4] & working[6]);
        const ios_u32 first = working[7] + sum1 + choice + round_constants[index] + words[index];
        const ios_u32 sum0 = rotate_right(*working, 2) ^ rotate_right(*working, 13)
            ^ rotate_right(*working, 22);
        const ios_u32 majority = (*working & working[1]) ^ (*working & working[2])
            ^ (working[1] & working[2]);
        const ios_u32 second = sum0 + majority;
        for (ios_size cursor = 7; cursor > 0; --cursor) { working[cursor] = working[cursor - 1]; }
        working[4] += first;
        *working = first + second;
    }
    for (ios_size index = 0; index < 8; ++index) { context->state[index] += working[index]; }
}

void ios_sha256(const void *data, ios_size size, ios_u8 digest[32])
{
    const ios_u8 *bytes = data;
    struct sha256_context context = {
        .state = { UINT32_C(0x6a09e667), UINT32_C(0xbb67ae85), UINT32_C(0x3c6ef372),
            UINT32_C(0xa54ff53a), UINT32_C(0x510e527f), UINT32_C(0x9b05688c),
            UINT32_C(0x1f83d9ab), UINT32_C(0x5be0cd19) }
    };
    context.byte_count = size;
    while (size != 0) {
        const ios_size available = sizeof(context.block) - context.block_size;
        const ios_size amount = size < available ? size : available;
        memcpy(context.block + context.block_size, bytes, amount);
        context.block_size += amount; bytes += amount; size -= amount;
        if (context.block_size == sizeof(context.block)) {
            transform(&context, context.block); context.block_size = 0;
        }
    }
    context.block[context.block_size++] = UINT8_C(0x80);
    if (context.block_size > 56) {
        memset(context.block + context.block_size, 0, 64 - context.block_size);
        transform(&context, context.block); context.block_size = 0;
    }
    memset(context.block + context.block_size, 0, 56 - context.block_size);
    const ios_u64 bit_count = context.byte_count * 8U;
    for (ios_size index = 0; index < 8; ++index) {
        context.block[63 - index] = (ios_u8)(bit_count >> (index * 8U));
    }
    transform(&context, context.block);
    for (ios_size index = 0; index < 8; ++index) {
        digest[index * 4] = (ios_u8)(context.state[index] >> 24);
        digest[index * 4 + 1] = (ios_u8)(context.state[index] >> 16);
        digest[index * 4 + 2] = (ios_u8)(context.state[index] >> 8);
        digest[index * 4 + 3] = (ios_u8)context.state[index];
    }
}
