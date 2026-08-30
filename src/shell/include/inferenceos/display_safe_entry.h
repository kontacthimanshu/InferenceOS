#ifndef INFERENCEOS_DISPLAY_SAFE_ENTRY_H
#define INFERENCEOS_DISPLAY_SAFE_ENTRY_H

#include <inferenceos/errors.h>

enum {
    IOS_DISPLAY_SAFE_ENTRY_VERSION = 1,
    IOS_DISPLAY_SAFE_NAME_CAPACITY = 64
};

enum ios_display_safe_object_kind {
    IOS_DISPLAY_SAFE_REGULAR_FILE = 1,
    IOS_DISPLAY_SAFE_DIRECTORY = 2
};

enum ios_display_safe_attribute {
    IOS_DISPLAY_SAFE_ATTRIBUTE_READ_ONLY = UINT32_C(1) << 0
};

enum ios_display_safe_operation {
    IOS_DISPLAY_SAFE_OPERATION_OPEN = UINT64_C(1) << 0,
    IOS_DISPLAY_SAFE_OPERATION_READ = UINT64_C(1) << 1,
    IOS_DISPLAY_SAFE_OPERATION_WRITE = UINT64_C(1) << 2,
    IOS_DISPLAY_SAFE_OPERATION_RENAME = UINT64_C(1) << 3,
    IOS_DISPLAY_SAFE_OPERATION_DELETE = UINT64_C(1) << 4,
    IOS_DISPLAY_SAFE_OPERATION_ENUMERATE = UINT64_C(1) << 5
};

struct ios_display_safe_entry {
    ios_u16 size;
    ios_u16 version;
    ios_u32 generic_attributes;
    ios_u64 object_handle;
    ios_u64 type_icon_capability;
    ios_u64 byte_size;
    ios_u64 allowed_operations;
    ios_u8 object_kind;
    ios_u8 display_name_length;
    ios_u8 reserved[6];
    char display_name[IOS_DISPLAY_SAFE_NAME_CAPACITY];
};

struct ios_display_safe_source_entry {
    const char *base_name;
    ios_u64 object_handle;
    ios_u64 type_icon_capability;
    ios_u64 byte_size;
    ios_u64 allowed_operations;
    ios_u32 generic_attributes;
    enum ios_display_safe_object_kind object_kind;
};

IOS_STATIC_ASSERT(
    sizeof(struct ios_display_safe_entry) == 112, "display-safe entry wire size"
);
IOS_STATIC_ASSERT(
    offsetof(struct ios_display_safe_entry, object_handle) == 8,
    "display-safe object handle offset"
);
IOS_STATIC_ASSERT(
    offsetof(struct ios_display_safe_entry, type_icon_capability) == 16,
    "display-safe type capability offset"
);
IOS_STATIC_ASSERT(
    offsetof(struct ios_display_safe_entry, display_name) == 48,
    "display-safe name offset"
);

ios_status ios_display_safe_entry_convert(
    const struct ios_display_safe_source_entry *source,
    struct ios_display_safe_entry *entry
);
ios_status ios_display_safe_entries_disambiguate(
    struct ios_display_safe_entry *entries,
    ios_size count,
    ios_size *rank_workspace,
    ios_size workspace_count
);
ios_status ios_display_safe_entries_validate_final(
    const struct ios_display_safe_entry *entries,
    ios_size count
);

#endif
