#ifndef INFERENCEOS_TYPE_CATALOG_H
#define INFERENCEOS_TYPE_CATALOG_H

#include <inferenceos/base.h>
#include <inferenceos/errors.h>

enum {
    IOS_TYPE_CATALOG_CAPACITY = 64
};

typedef ios_u64 ios_type_icon_capability;

#define IOS_INVALID_TYPE_ICON_CAPABILITY UINT64_C(0)

/* Stable presentation identifiers. They disclose no filesystem type metadata. */
enum ios_presentation_icon {
    IOS_ICON_GENERIC_FILE = 1,
    IOS_ICON_FOLDER = 2,
    IOS_ICON_TEXT = 10,
    IOS_ICON_IMAGE = 11,
    IOS_ICON_APPLICATION = 12
};

enum ios_type_catalog_object_kind {
    IOS_TYPE_CATALOG_REGULAR_FILE = 1,
    IOS_TYPE_CATALOG_DIRECTORY = 2
};

struct ios_type_catalog_slot {
    /* Kernel-private authoritative identity; never copied into an ordinary DTO. */
    ios_u64 internal_type_identity;
    ios_u32 generation;
    enum ios_presentation_icon icon;
    bool occupied;
};

struct ios_type_catalog {
    struct ios_type_catalog_slot slots[IOS_TYPE_CATALOG_CAPACITY];
    ios_u32 generation_seed;
    ios_u32 entry_count;
};

ios_status ios_type_catalog_initialize(
    struct ios_type_catalog *catalog,
    ios_u64 kernel_boot_identity
);
ios_status ios_type_catalog_register(
    struct ios_type_catalog *catalog,
    ios_u64 internal_type_identity,
    enum ios_presentation_icon icon,
    ios_type_icon_capability *capability
);
ios_status ios_type_catalog_resolve_icon(
    const struct ios_type_catalog *catalog,
    ios_type_icon_capability capability,
    enum ios_type_catalog_object_kind object_kind,
    enum ios_presentation_icon *icon
);
ios_status ios_type_catalog_resolve_identity(
    const struct ios_type_catalog *catalog,
    ios_type_icon_capability capability,
    ios_u64 *internal_type_identity
);
ios_status ios_type_catalog_find_capability(
    const struct ios_type_catalog *catalog,
    ios_u64 internal_type_identity,
    ios_type_icon_capability *capability
);
ios_u32 ios_type_catalog_entry_count(const struct ios_type_catalog *catalog);

#endif
