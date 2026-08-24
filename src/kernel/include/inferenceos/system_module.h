#ifndef INFERENCEOS_SYSTEM_MODULE_H
#define INFERENCEOS_SYSTEM_MODULE_H

#include <inferenceos/base.h>
#include <inferenceos/errors.h>

enum {
    IOS_SYSTEM_MODULE_ABI_VERSION = 1,
    IOS_SYSTEM_MODULE_DIGEST_SIZE = 32,
    IOS_SYSTEM_MODULE_MAX_COUNT = 32
};

enum ios_system_module_role {
    IOS_MODULE_ROLE_SHELL = 1,
    IOS_MODULE_ROLE_GUI_DESKTOP = 2,
    IOS_MODULE_ROLE_GUI_TERMINAL = 3,
    IOS_MODULE_ROLE_FILE_EXPLORER = 4,
    IOS_MODULE_ROLE_TEST_APPLICATION = 5
};

enum ios_system_module_flags {
    IOS_SYSTEM_MODULE_REQUIRED = UINT32_C(1) << 0
};

struct ios_system_module_descriptor {
    ios_u32 structure_size;
    ios_u32 structure_version;
    ios_u64 application_identity;
    ios_u32 role;
    ios_u32 flags;
    ios_uptr image_address;
    ios_u64 image_size;
    ios_u32 entry_abi_version;
    ios_u32 reserved;
    ios_u8 digest[IOS_SYSTEM_MODULE_DIGEST_SIZE];
};

struct ios_system_module_range {
    ios_uptr base;
    ios_u64 byte_count;
};

typedef ios_status (*ios_system_module_digest_verifier)(
    const void *image,
    ios_size image_size,
    const ios_u8 expected_digest[IOS_SYSTEM_MODULE_DIGEST_SIZE]
);

ios_status system_module_descriptor_validate(
    const struct ios_system_module_descriptor *descriptor,
    const struct ios_system_module_range *forbidden_ranges,
    ios_size forbidden_range_count,
    ios_system_module_digest_verifier verify_digest
);
ios_status system_module_set_validate(
    const struct ios_system_module_descriptor *descriptors,
    ios_size descriptor_count,
    const struct ios_system_module_range *forbidden_ranges,
    ios_size forbidden_range_count,
    ios_system_module_digest_verifier verify_digest
);
ios_u32 system_module_startup_rank(ios_u32 role);

#endif
