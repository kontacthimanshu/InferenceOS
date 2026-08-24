#include <inferenceos/runtime.h>

#include <stdint.h>

void *memcpy(void *restrict destination, const void *restrict source, size_t count)
{
    unsigned char *destination_bytes = destination;
    const unsigned char *source_bytes = source;

    for (size_t index = 0; index < count; ++index) {
        destination_bytes[index] = source_bytes[index];
    }

    return destination;
}

void *memmove(void *destination, const void *source, size_t count)
{
    unsigned char *destination_bytes = destination;
    const unsigned char *source_bytes = source;

    if (destination_bytes == source_bytes || count == 0) {
        return destination;
    }

    if ((uintptr_t)destination_bytes < (uintptr_t)source_bytes) {
        for (size_t index = 0; index < count; ++index) {
            destination_bytes[index] = source_bytes[index];
        }
    } else {
        for (size_t index = count; index != 0; --index) {
            destination_bytes[index - 1] = source_bytes[index - 1];
        }
    }

    return destination;
}

void *memset(void *destination, int value, size_t count)
{
    unsigned char *destination_bytes = destination;
    const unsigned char byte_value = (unsigned char)value;

    for (size_t index = 0; index < count; ++index) {
        destination_bytes[index] = byte_value;
    }

    return destination;
}

int memcmp(const void *left, const void *right, size_t count)
{
    const unsigned char *left_bytes = left;
    const unsigned char *right_bytes = right;

    for (size_t index = 0; index < count; ++index) {
        if (left_bytes[index] != right_bytes[index]) {
            return (int)left_bytes[index] - (int)right_bytes[index];
        }
    }

    return 0;
}
