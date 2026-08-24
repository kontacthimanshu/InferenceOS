#ifndef INFERENCEOS_FS_SUBDIRECTORY_H
#define INFERENCEOS_FS_SUBDIRECTORY_H

#include <inferenceos/fs/directory.h>
#include <inferenceos/fs/file.h>

struct ios_fs_directory_store {
    ios_u32 *fat;
    ios_size fat_entry_count;
    ios_u32 cluster_bytes;
    void *io_context;
    struct ios_fs_cluster_operations operations;
    bool writable;
};

ios_status ios_fs_subdirectory_initialize(
    struct ios_fs_directory_store *store,
    ios_u32 cluster,
    ios_u32 parent_cluster,
    ios_u8 *scratch,
    ios_size scratch_size
);
ios_status ios_fs_subdirectory_grow(
    struct ios_fs_directory_store *store,
    ios_u32 first_cluster,
    ios_u32 *chain_workspace,
    ios_size chain_capacity,
    ios_u32 *new_cluster
);
ios_status ios_fs_subdirectory_enumerate(
    struct ios_fs_directory_store *store,
    ios_u32 first_cluster,
    ios_u32 *chain_workspace,
    ios_size chain_capacity,
    ios_u8 *scratch,
    ios_size scratch_size,
    struct ios_fs_directory_entry *entries,
    ios_size entry_capacity,
    ios_size *entry_count
);
ios_status ios_fs_subdirectory_remove(
    struct ios_fs_directory_store *store,
    ios_u32 first_cluster,
    ios_u32 parent_cluster,
    ios_u32 *chain_workspace,
    ios_size chain_capacity,
    ios_u8 *scratch,
    ios_size scratch_size
);

#endif
