#include <inferenceos/runtime.h>

size_t strlen(const char *string)
{
    size_t length = 0;

    while (string[length] != '\0') {
        ++length;
    }

    return length;
}

size_t strnlen(const char *string, size_t maximum_length)
{
    size_t length = 0;

    while (length < maximum_length && string[length] != '\0') {
        ++length;
    }

    return length;
}

int strcmp(const char *left, const char *right)
{
    size_t index = 0;

    while (left[index] != '\0' && left[index] == right[index]) {
        ++index;
    }

    return (int)(unsigned char)left[index] - (int)(unsigned char)right[index];
}

int strncmp(const char *left, const char *right, size_t count)
{
    for (size_t index = 0; index < count; ++index) {
        const unsigned char left_character = (unsigned char)left[index];
        const unsigned char right_character = (unsigned char)right[index];

        if (left_character != right_character) {
            return (int)left_character - (int)right_character;
        }

        if (left_character == (unsigned char)'\0') {
            return 0;
        }
    }

    return 0;
}

char *strchr(const char *string, int character)
{
    const unsigned char target = (unsigned char)character;

    for (;;) {
        const unsigned char current = (unsigned char)*string;

        if (current == target) {
            return (char *)string;
        }

        if (current == (unsigned char)'\0') {
            return NULL;
        }

        ++string;
    }
}

char *strrchr(const char *string, int character)
{
    const unsigned char target = (unsigned char)character;
    const char *last_match = NULL;

    for (;;) {
        const unsigned char current = (unsigned char)*string;

        if (current == target) {
            last_match = string;
        }

        if (current == (unsigned char)'\0') {
            return (char *)last_match;
        }

        ++string;
    }
}
