#ifndef INFERENCEOS_FS_FILE_SERVICE_H
#define INFERENCEOS_FS_FILE_SERVICE_H

#include <inferenceos/fs/mount.h>
#include <inferenceos/fs/sync.h>
#include <inferenceos/fs/directory.h>

enum {
    IOS_FS_FILE_SERVICE_MAX_CLUSTERS = 256,
    IOS_FS_FILE_SERVICE_CLUSTER_BYTES = IOS_FS_SECTOR_SIZE * IOS_FS_SECTORS_PER_CLUSTER,
    IOS_FS_FILE_SERVICE_DIRECTORY_SLOTS =
        IOS_FS_FILE_SERVICE_CLUSTER_BYTES / IOS_FS_PRIMARY_RECORD_SIZE,
    IOS_FS_FILE_SERVICE_MAX_DIRTY_FAT_SECTORS =
        IOS_FS_FILE_SERVICE_MAX_CLUSTERS * 2 + 2
};

struct ios_fs_file_service {
    struct ios_fs_mount *mount;
    struct ios_fs_sync *sync;
    ios_u32 *fat;
    ios_size fat_entry_count;
    ios_size fat_storage_size;
    ios_u32 chain[IOS_FS_FILE_SERVICE_MAX_CLUSTERS];
    ios_u32 old_chain[IOS_FS_FILE_SERVICE_MAX_CLUSTERS];
    ios_u32 dirty_fat_sectors[IOS_FS_FILE_SERVICE_MAX_DIRTY_FAT_SECTORS];
    ios_size dirty_fat_sector_count;
    ios_u8 directory[IOS_FS_FILE_SERVICE_CLUSTER_BYTES];
    ios_u8 cluster[IOS_FS_FILE_SERVICE_CLUSTER_BYTES];
    struct ios_fs_directory_entry
        directory_entries[IOS_FS_FILE_SERVICE_DIRECTORY_SLOTS];
    ios_u16 companion_slot;
    ios_u16 primary_slot;
    bool initialized;
};

ios_status ios_fs_file_service_initialize(
    struct ios_fs_file_service *service,
    struct ios_fs_mount *mount,
    struct ios_fs_sync *sync,
    ios_u32 *fat_storage,
    ios_size fat_storage_size
);
ios_status ios_fs_file_service_create(
    struct ios_fs_file_service *service, const char *name
);
ios_status ios_fs_file_service_replace(
    struct ios_fs_file_service *service,
    const char *name,
    const void *bytes,
    ios_size length
);
ios_status ios_fs_file_service_append(
    struct ios_fs_file_service *service,
    const char *name,
    const void *bytes,
    ios_size length
);
ios_status ios_fs_file_service_read(
    struct ios_fs_file_service *service,
    const char *name,
    ios_u32 offset,
    void *buffer,
    ios_size capacity,
    ios_size *transferred,
    bool *complete
);
ios_status ios_fs_file_service_rename(
    struct ios_fs_file_service *service,
    const char *source,
    const char *destination
);
ios_status ios_fs_file_service_remove(
    struct ios_fs_file_service *service, const char *name
);
ios_status ios_fs_file_service_replace_object(
    void *context,
    ios_u64 object_identity,
    const void *bytes,
    ios_size length
);
ios_status ios_fs_file_service_append_object(
    void *context,
    ios_u64 object_identity,
    const void *bytes,
    ios_size length
);
ios_status ios_fs_file_service_read_object(
    void *context,
    ios_u64 object_identity,
    ios_u64 offset,
    void *buffer,
    ios_size capacity,
    ios_size *transferred,
    bool *complete
);
ios_status ios_fs_file_service_rename_object(
    void *context,
    ios_u64 object_identity,
    ios_u64 destination_parent_identity,
    const char *destination_base,
    ios_size destination_base_length
);
ios_status ios_fs_file_service_remove_object(
    void *context,
    ios_u64 object_identity
);

/* VFS callbacks installed on the mounted InferenceOS-FS volume. */
ios_status ios_fs_file_service_enumerate(
    void *context,
    ios_u64 directory_identity,
    ios_u64 continuation,
    struct ios_vfs_directory_entry *entries,
    ios_size capacity,
    ios_size *entry_count,
    ios_u64 *next_continuation
);
ios_status ios_fs_file_service_lookup(
    void *context,
    ios_u64 directory_identity,
    const char *component,
    ios_size component_length,
    struct ios_vfs_object *object
);
ios_status ios_fs_file_service_create_directory(
    void *context,
    ios_u64 parent_identity,
    const char *component,
    ios_size component_length,
    struct ios_vfs_object *object
);
ios_status ios_fs_file_service_create_file(
    void *context,
    ios_u64 parent_identity,
    const char *component,
    ios_size component_length,
    struct ios_vfs_object *object
);
ios_status ios_fs_file_service_remove_directory(
    void *context,
    ios_u64 parent_identity,
    const char *component,
    ios_size component_length
);
ios_status ios_fs_file_service_vfs_rename(
    void *context,
    ios_u64 source_parent_identity,
    const char *source_component,
    ios_size source_component_length,
    ios_u64 destination_parent_identity,
    const char *destination_component,
    ios_size destination_component_length
);
ios_status ios_fs_record_pair_matches_extension(
    const struct ios_fs_primary_disk *primary,
    const struct ios_fs_companion_disk *companion,
    const ios_u8 canonical_extension[IOS_FS_EXTENSION_SIZE],
    ios_size extension_length,
    const ios_u8 extension_hash[IOS_FS_HASH_TEXT_SIZE],
    bool *matches
);
ios_status ios_fs_file_service_search_extension(
    void *context,
    const char *extension,
    ios_size extension_length,
    struct ios_vfs_search_result *entries,
    ios_size capacity,
    ios_size *entry_count,
    bool *truncated
);

ios_status ios_fs_file_service_get_record(
    struct ios_fs_file_service *service,
    ios_u64 object_identity,
    struct ios_fs_primary_disk *primary,
    struct ios_fs_companion_disk *companion,
    ios_u64 *primary_record_location,
    ios_u64 *companion_record_location,
    bool *has_companion
);
ios_u64 ios_fs_file_service_free_bytes(const struct ios_fs_file_service *service);

#endif
