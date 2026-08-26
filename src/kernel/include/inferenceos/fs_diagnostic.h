#ifndef INFERENCEOS_FS_DIAGNOSTIC_H
#define INFERENCEOS_FS_DIAGNOSTIC_H

#include <inferenceos/fs/directory.h>
#include <inferenceos/fs/mount.h>
#include <inferenceos/fs/records.h>
#include <inferenceos/process.h>

enum {
    IOS_FS_DIAGNOSTIC_ABI_VERSION = 1,
    IOS_FS_DIAGNOSTIC_CHAIN_CAPACITY = 128
};

enum ios_fs_diagnostic_scope {
    IOS_FS_DIAGNOSTIC_SCOPE_FILESYSTEM = UINT32_C(1) << 0,
    IOS_FS_DIAGNOSTIC_SCOPE_RECORDS = UINT32_C(1) << 1,
    IOS_FS_DIAGNOSTIC_SCOPE_ALLOCATION = UINT32_C(1) << 2,
    IOS_FS_DIAGNOSTIC_SCOPE_REGISTRY = UINT32_C(1) << 3
};

#define IOS_FS_DIAGNOSTIC_SCOPE_ALL UINT32_C(0xf)

enum ios_fs_diagnostic_query {
    IOS_FS_DIAGNOSTIC_QUERY_FILESYSTEM = 1,
    IOS_FS_DIAGNOSTIC_QUERY_FILE = 2,
    IOS_FS_DIAGNOSTIC_QUERY_HASH = 3,
    IOS_FS_DIAGNOSTIC_QUERY_FAT = 4
};

enum ios_fs_diagnostic_registry_health {
    IOS_FS_DIAGNOSTIC_REGISTRY_DISABLED,
    IOS_FS_DIAGNOSTIC_REGISTRY_HEALTHY,
    IOS_FS_DIAGNOSTIC_REGISTRY_INVALID
};

struct ios_fs_diagnostic_authority {
    ios_u64 owner_process_id;
    ios_u64 owner_application_identity;
    ios_u32 scope;
};

struct ios_fs_diagnostic_request {
    ios_u16 size;
    ios_u16 version;
    enum ios_fs_diagnostic_query query;
    ios_handle authority;
    ios_u64 object_identity;
    ios_u32 maximum_chain_entries;
    ios_u32 reserved;
};

struct ios_fs_diagnostic_filesystem_info {
    ios_u8 identity[8];
    ios_u16 format_version;
    ios_u16 primary_record_size;
    ios_u16 companion_record_size;
    ios_u8 hash_algorithm_id;
    ios_u8 sectors_per_cluster;
    ios_u64 volume_capacity_bytes;
    ios_u64 usable_bytes;
    ios_u64 data_start_sector;
    ios_u32 fat_sectors;
    ios_u32 cluster_count;
    enum ios_mount_state mount_state;
    enum ios_fs_diagnostic_registry_health registry_health;
    ios_u32 registry_active_type_count;
    bool free_space_known;
    ios_u64 free_bytes;
};

struct ios_fs_diagnostic_file_info {
    ios_u8 canonical_name[IOS_FS_NAME_SIZE];
    enum ios_fs_directory_entry_kind object_type;
    ios_u8 attributes;
    ios_u32 size;
    ios_u32 first_cluster;
    ios_u64 primary_record_location;
    ios_u64 companion_record_location;
};

struct ios_fs_diagnostic_hash_info {
    ios_u8 extension[IOS_FS_EXTENSION_SIZE];
    ios_u8 extension_length;
    ios_u8 hash_algorithm_id;
    ios_u8 stored_hash[IOS_FS_HASH_TEXT_SIZE];
    ios_u8 recomputed_hash[IOS_FS_HASH_TEXT_SIZE];
    ios_u8 record_version;
    bool committed;
    bool association_checksum_valid;
    bool crc_valid;
    ios_status validation_status;
};

struct ios_fs_diagnostic_fat_info {
    ios_u32 clusters[IOS_FS_DIAGNOSTIC_CHAIN_CAPACITY];
    ios_u32 cluster_count;
    bool end_of_chain;
};

struct ios_fs_diagnostic_reply {
    ios_u16 size;
    ios_u16 version;
    enum ios_fs_diagnostic_query query;
    union {
        struct ios_fs_diagnostic_filesystem_info filesystem;
        struct ios_fs_diagnostic_file_info file;
        struct ios_fs_diagnostic_hash_info hash;
        struct ios_fs_diagnostic_fat_info fat;
    } value;
};

struct ios_fs_diagnostic_source {
    struct ios_fs_primary_disk primary;
    struct ios_fs_companion_disk companion;
    ios_u64 primary_record_location;
    ios_u64 companion_record_location;
    const ios_u32 *fat;
    ios_size fat_entry_count;
    bool has_primary;
    bool has_companion;
    bool free_space_known;
    ios_u64 free_bytes;
    enum ios_fs_diagnostic_registry_health registry_health;
    ios_u32 registry_active_type_count;
};

typedef ios_status (*ios_fs_diagnostic_snapshot_provider)(
    void *context,
    enum ios_fs_diagnostic_query query,
    ios_u64 object_identity,
    struct ios_fs_diagnostic_source *source
);

/*
 * Allocation-free output sink suitable for serial consoles and panic paths.
 * Returning false stops formatting immediately and is reported as IOS_E_IO.
 */
typedef bool (*ios_fs_diagnostic_character_sink)(void *context, char character);

struct ios_fs_diagnostic_service {
    struct ios_fs_mount *mount;
    ios_fs_diagnostic_snapshot_provider snapshot;
    void *snapshot_context;
};

ios_status ios_fs_diagnostic_service_initialize(
    struct ios_fs_diagnostic_service *service,
    struct ios_fs_mount *mount,
    ios_fs_diagnostic_snapshot_provider snapshot,
    void *snapshot_context
);
ios_status ios_fs_diagnostic_dispatch(
    struct ios_fs_diagnostic_service *service,
    const struct ios_process *caller,
    const struct ios_fs_diagnostic_request *request,
    struct ios_fs_diagnostic_reply *reply
);
ios_status ios_fs_diagnostic_format(
    const struct ios_fs_diagnostic_reply *reply,
    ios_fs_diagnostic_character_sink sink,
    void *sink_context
);

#endif
