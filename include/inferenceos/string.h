#ifndef INFERENCEOS_STRING_H
#define INFERENCEOS_STRING_H

#include <inferenceos/base.h>
#include <inferenceos/result.h>

/* Determine a string length only when a terminator occurs within capacity. */
inferenceos_result inferenceos_string_length(
    const char *text,
    inferenceos_size capacity,
    inferenceos_size *length
);

/* Copy a bounded, terminated source including its terminator. The destination
 * is not modified when validation or capacity checks fail. */
inferenceos_result inferenceos_string_copy(
    char *destination,
    inferenceos_size destination_capacity,
    const char *source,
    inferenceos_size source_capacity,
    inferenceos_size *copied_length
);

/* Compare two bounded, terminated strings without reading past either bound. */
inferenceos_result inferenceos_string_equal(
    const char *left,
    inferenceos_size left_capacity,
    const char *right,
    inferenceos_size right_capacity,
    bool *equal
);

char inferenceos_ascii_to_upper(char value);
bool inferenceos_ascii_is_printable(char value);

#endif
