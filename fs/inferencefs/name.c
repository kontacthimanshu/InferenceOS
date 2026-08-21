#include <inferencefs/name.h>

#include <inferenceos/memory.h>
#include <inferenceos/string.h>

bool inferencefs_name_character_is_supported(char character)
{
    const char uppercase = inferenceos_ascii_to_upper(character);

    return (uppercase >= 'A' && uppercase <= 'Z')
        || (uppercase >= '0' && uppercase <= '9')
        || uppercase == '_'
        || uppercase == '-';
}

inferenceos_result inferencefs_name_canonicalize(
    const char *name,
    inferenceos_size name_length,
    inferenceos_u8 short_name[INFERENCEFS_SHORT_NAME_SIZE]
)
{
    inferenceos_u8 canonical[INFERENCEFS_SHORT_NAME_SIZE];
    inferenceos_size base_length = 0U;
    inferenceos_size extension_length = 0U;
    bool saw_separator = false;

    if (name == NULL || short_name == NULL) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    if (name_length == 0U || name_length > 12U) {
        return INFERENCEOS_RESULT_OUT_OF_RANGE;
    }

    (void)memset(canonical, INFERENCEFS_SHORT_NAME_PADDING, sizeof(canonical));

    for (inferenceos_size index = 0U; index < name_length; ++index) {
        const char character = name[index];

        if (character == '.') {
            if (saw_separator || base_length == 0U) {
                return INFERENCEOS_RESULT_INVALID_ARGUMENT;
            }
            saw_separator = true;
            continue;
        }
        if (!inferencefs_name_character_is_supported(character)) {
            return INFERENCEOS_RESULT_INVALID_ARGUMENT;
        }

        if (!saw_separator) {
            if (base_length == INFERENCEFS_SHORT_NAME_BASE_SIZE) {
                return INFERENCEOS_RESULT_OUT_OF_RANGE;
            }
            canonical[base_length] =
                (inferenceos_u8)inferenceos_ascii_to_upper(character);
            ++base_length;
        } else {
            if (extension_length == INFERENCEFS_SHORT_NAME_EXTENSION_SIZE) {
                return INFERENCEOS_RESULT_OUT_OF_RANGE;
            }
            canonical[INFERENCEFS_SHORT_NAME_BASE_SIZE + extension_length] =
                (inferenceos_u8)inferenceos_ascii_to_upper(character);
            ++extension_length;
        }
    }

    if (saw_separator && extension_length == 0U) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }

    (void)memcpy(short_name, canonical, sizeof(canonical));
    return INFERENCEOS_RESULT_OK;
}

bool inferencefs_name_equal(
    const inferenceos_u8 left[INFERENCEFS_SHORT_NAME_SIZE],
    const inferenceos_u8 right[INFERENCEFS_SHORT_NAME_SIZE]
)
{
    if (left == NULL || right == NULL) {
        return false;
    }
    return memcmp(left, right, INFERENCEFS_SHORT_NAME_SIZE) == 0;
}

inferenceos_result inferencefs_name_extension(
    const inferenceos_u8 short_name[INFERENCEFS_SHORT_NAME_SIZE],
    inferenceos_u8 extension[INFERENCEFS_SHORT_NAME_EXTENSION_SIZE],
    inferenceos_u8 *extension_length
)
{
    inferenceos_size length = INFERENCEFS_SHORT_NAME_EXTENSION_SIZE;

    if (short_name == NULL || extension == NULL || extension_length == NULL) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }

    while (length > 0U
        && short_name[INFERENCEFS_SHORT_NAME_BASE_SIZE + length - 1U]
            == INFERENCEFS_SHORT_NAME_PADDING) {
        --length;
    }

    for (inferenceos_size index = 0U; index < length; ++index) {
        extension[index] = short_name[INFERENCEFS_SHORT_NAME_BASE_SIZE + index];
    }
    for (inferenceos_size index = length;
         index < INFERENCEFS_SHORT_NAME_EXTENSION_SIZE;
         ++index) {
        extension[index] = INFERENCEFS_SHORT_NAME_PADDING;
    }
    *extension_length = (inferenceos_u8)length;
    return INFERENCEOS_RESULT_OK;
}

inferenceos_u8 inferencefs_primary_name_checksum(
    const inferenceos_u8 short_name[INFERENCEFS_SHORT_NAME_SIZE]
)
{
    inferenceos_u8 sum = 0U;

    if (short_name == NULL) {
        return 0U;
    }

    for (inferenceos_size index = 0U;
         index < INFERENCEFS_SHORT_NAME_SIZE;
         ++index) {
        const inferenceos_u8 rotated = (inferenceos_u8)(
            ((sum & 1U) != 0U ? 0x80U : 0U) + (sum >> 1U)
        );
        sum = (inferenceos_u8)(rotated + short_name[index]);
    }
    return sum;
}
