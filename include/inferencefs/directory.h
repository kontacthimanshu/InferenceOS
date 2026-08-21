#ifndef INFERENCEFS_DIRECTORY_H
#define INFERENCEFS_DIRECTORY_H

#include <inferencefs/format.h>
#include <inferenceos/result.h>

typedef enum inferencefs_directory_slot_kind {
    INFERENCEFS_DIRECTORY_SLOT_KIND_END = 0,
    INFERENCEFS_DIRECTORY_SLOT_KIND_DELETED = 1,
    INFERENCEFS_DIRECTORY_SLOT_KIND_COMPANION = 2,
    INFERENCEFS_DIRECTORY_SLOT_KIND_REGULAR = 3,
    INFERENCEFS_DIRECTORY_SLOT_KIND_DIRECTORY = 4,
    INFERENCEFS_DIRECTORY_SLOT_KIND_UNSUPPORTED = 5,
    INFERENCEFS_DIRECTORY_SLOT_KIND_CORRUPT = 6
} inferencefs_directory_slot_kind;

typedef struct inferencefs_primary_record {
    inferenceos_u8 name[INFERENCEFS_SHORT_NAME_SIZE];
    inferenceos_u8 attributes;
    inferenceos_u32 first_cluster;
    inferenceos_u32 file_size;
} inferencefs_primary_record;

inferencefs_directory_slot_kind inferencefs_directory_classify_slot(
    const void *slot
);
inferenceos_result inferencefs_directory_decode_primary(
    const inferencefs_primary_record_disk *disk,
    inferencefs_primary_record *record
);

#endif
