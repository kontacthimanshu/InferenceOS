#ifndef INFERENCEOS_FS_REGISTRY_DIAGNOSTICS_H
#define INFERENCEOS_FS_REGISTRY_DIAGNOSTICS_H

#include <inferenceos/fs/registry.h>
#include <inferenceos/fs_diagnostic.h>

enum {
    IOS_FS_REGISTRY_DIAGNOSTIC_ABI_VERSION = 1,
    IOS_FS_REGISTRY_DIAGNOSTIC_REPLY_CAPACITY = 16
};

struct ios_fs_registry_diagnostic_record {
    ios_u32 record_index;
    ios_status validation_status;
    bool active;
    ios_u8 extension_length;
    ios_u8 canonical_extension[IOS_FS_EXTENSION_SIZE];
    ios_u8 extension_hash_text[IOS_FS_HASH_TEXT_SIZE];
    ios_u32 last_directory_cluster;
    ios_u16 last_directory_slot;
    ios_u16 update_generation;
};

struct ios_fs_registry_diagnostic_request {
    ios_u16 size;
    ios_u16 version;
    ios_handle authority;
    ios_u32 first_record;
    ios_u32 maximum_records;
    ios_u32 reserved;
};

struct ios_fs_registry_diagnostic_reply {
    ios_u16 size;
    ios_u16 version;
    ios_u8 enabled;
    enum ios_fs_registry_health health;
    ios_u32 active_type_count;
    ios_u32 record_count;
    struct ios_fs_registry_diagnostic_record
        records[IOS_FS_REGISTRY_DIAGNOSTIC_REPLY_CAPACITY];
};

struct ios_fs_registry_control_request {
    ios_u16 size;
    ios_u16 version;
    ios_handle authority;
    ios_u8 enabled;
    ios_u8 reserved0[3];
    ios_u32 reserved1;
};

struct ios_fs_registry_control_reply {
    ios_u16 size;
    ios_u16 version;
    ios_u8 enabled;
    enum ios_fs_registry_health health;
};

struct ios_fs_registry_diagnostic_service {
    struct ios_fs_registry *registry;
};

ios_status ios_fs_registry_diagnostic_service_initialize(
    struct ios_fs_registry_diagnostic_service *service,
    struct ios_fs_registry *registry
);
ios_status ios_fs_registry_diagnostic_dispatch(
    const struct ios_fs_registry_diagnostic_service *service,
    const struct ios_process *caller,
    const struct ios_fs_registry_diagnostic_request *request,
    struct ios_fs_registry_diagnostic_reply *reply
);
ios_status ios_fs_registry_control_dispatch(
    struct ios_fs_registry_diagnostic_service *service,
    const struct ios_process *caller,
    const struct ios_fs_registry_control_request *request,
    struct ios_fs_registry_control_reply *reply
);

#endif
