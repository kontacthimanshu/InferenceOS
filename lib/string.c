#include <inferenceos/memory.h>
#include <inferenceos/string.h>

inferenceos_result inferenceos_string_length(
    const char *text,
    inferenceos_size capacity,
    inferenceos_size *length
)
{
    if (text == NULL || length == NULL) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }

    for (inferenceos_size index = 0U; index < capacity; ++index) {
        if (text[index] == '\0') {
            *length = index;
            return INFERENCEOS_RESULT_OK;
        }
    }
    return INFERENCEOS_RESULT_OUT_OF_RANGE;
}

inferenceos_result inferenceos_string_copy(
    char *destination,
    inferenceos_size destination_capacity,
    const char *source,
    inferenceos_size source_capacity,
    inferenceos_size *copied_length
)
{
    inferenceos_size source_length;
    inferenceos_size bytes_to_copy;
    inferenceos_result result;

    if (destination == NULL || source == NULL) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }

    result = inferenceos_string_length(source, source_capacity, &source_length);
    if (!inferenceos_result_is_success(result)) {
        return result;
    }
    if (!inferenceos_checked_add_size(source_length, 1U, &bytes_to_copy)) {
        return INFERENCEOS_RESULT_OVERFLOW;
    }
    if (bytes_to_copy > destination_capacity) {
        return INFERENCEOS_RESULT_NO_SPACE;
    }

    (void)memmove(destination, source, bytes_to_copy);
    if (copied_length != NULL) {
        *copied_length = source_length;
    }
    return INFERENCEOS_RESULT_OK;
}

inferenceos_result inferenceos_string_equal(
    const char *left,
    inferenceos_size left_capacity,
    const char *right,
    inferenceos_size right_capacity,
    bool *equal
)
{
    inferenceos_size left_length;
    inferenceos_size right_length;
    inferenceos_result result;

    if (left == NULL || right == NULL || equal == NULL) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }

    result = inferenceos_string_length(left, left_capacity, &left_length);
    if (!inferenceos_result_is_success(result)) {
        return result;
    }
    result = inferenceos_string_length(right, right_capacity, &right_length);
    if (!inferenceos_result_is_success(result)) {
        return result;
    }

    *equal = left_length == right_length
        && memcmp(left, right, left_length) == 0;
    return INFERENCEOS_RESULT_OK;
}

char inferenceos_ascii_to_upper(char value)
{
    if (value >= 'a' && value <= 'z') {
        return (char)(value - ('a' - 'A'));
    }
    return value;
}

bool inferenceos_ascii_is_printable(char value)
{
    const unsigned char byte = (unsigned char)value;
    return byte >= 0x20U && byte <= 0x7EU;
}
