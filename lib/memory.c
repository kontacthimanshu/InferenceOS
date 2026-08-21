#include <inferenceos/memory.h>

void *memcpy(void *restrict destination, const void *restrict source, size_t count)
{
    unsigned char *destination_bytes = destination;
    const unsigned char *source_bytes = source;

    for (size_t index = 0U; index < count; ++index) {
        destination_bytes[index] = source_bytes[index];
    }
    return destination;
}

void *memmove(void *destination, const void *source, size_t count)
{
    unsigned char *destination_bytes = destination;
    const unsigned char *source_bytes = source;
    const uintptr_t destination_address = (uintptr_t)destination;
    const uintptr_t source_address = (uintptr_t)source;

    if (destination_address == source_address || count == 0U) {
        return destination;
    }

    if (destination_address < source_address) {
        for (size_t index = 0U; index < count; ++index) {
            destination_bytes[index] = source_bytes[index];
        }
    } else {
        for (size_t index = count; index > 0U; --index) {
            destination_bytes[index - 1U] = source_bytes[index - 1U];
        }
    }
    return destination;
}

void *memset(void *destination, int value, size_t count)
{
    unsigned char *destination_bytes = destination;
    const unsigned char byte_value = (unsigned char)value;

    for (size_t index = 0U; index < count; ++index) {
        destination_bytes[index] = byte_value;
    }
    return destination;
}

int memcmp(const void *left, const void *right, size_t count)
{
    const unsigned char *left_bytes = left;
    const unsigned char *right_bytes = right;

    for (size_t index = 0U; index < count; ++index) {
        if (left_bytes[index] < right_bytes[index]) {
            return -1;
        }
        if (left_bytes[index] > right_bytes[index]) {
            return 1;
        }
    }
    return 0;
}
