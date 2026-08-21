#ifndef INFERENCEFS_NAME_H
#define INFERENCEFS_NAME_H

#include <inferenceos/base.h>
#include <inferenceos/result.h>

#define INFERENCEFS_SHORT_NAME_BASE_SIZE 8U
#define INFERENCEFS_SHORT_NAME_EXTENSION_SIZE 3U
#define INFERENCEFS_SHORT_NAME_SIZE 11U
#define INFERENCEFS_SHORT_NAME_PADDING ((inferenceos_u8)0x20U)

bool inferencefs_name_character_is_supported(char character);

/* Convert one user-visible filename component to its uppercase, space-padded
 * 8.3 representation. The output is unchanged when validation fails. */
inferenceos_result inferencefs_name_canonicalize(
    const char *name,
    inferenceos_size name_length,
    inferenceos_u8 short_name[INFERENCEFS_SHORT_NAME_SIZE]
);

bool inferencefs_name_equal(
    const inferenceos_u8 left[INFERENCEFS_SHORT_NAME_SIZE],
    const inferenceos_u8 right[INFERENCEFS_SHORT_NAME_SIZE]
);

/* Extract the canonical extension by removing only trailing space padding. */
inferenceos_result inferencefs_name_extension(
    const inferenceos_u8 short_name[INFERENCEFS_SHORT_NAME_SIZE],
    inferenceos_u8 extension[INFERENCEFS_SHORT_NAME_EXTENSION_SIZE],
    inferenceos_u8 *extension_length
);

inferenceos_u8 inferencefs_primary_name_checksum(
    const inferenceos_u8 short_name[INFERENCEFS_SHORT_NAME_SIZE]
);

#endif
