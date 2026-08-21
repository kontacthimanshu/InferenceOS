#ifndef INFERENCEOS_MEMORY_H
#define INFERENCEOS_MEMORY_H

#include <inferenceos/base.h>

/* Freestanding implementations with the ISO C signatures expected by the
 * compiler. Their pointer validity and overlap contracts are the standard C
 * contracts; only memmove permits overlapping ranges. */
void *memcpy(void *restrict destination, const void *restrict source, size_t count);
void *memmove(void *destination, const void *source, size_t count);
void *memset(void *destination, int value, size_t count);
int memcmp(const void *left, const void *right, size_t count);

#endif
