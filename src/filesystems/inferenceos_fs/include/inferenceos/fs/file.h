#ifndef INFERENCEOS_FS_FILE_H
#define INFERENCEOS_FS_FILE_H

#include <inferenceos/fs/fat.h>
#include <inferenceos/fs/records.h>

struct ios_fs_cluster_operations {
    ios_status (*read)(void *context, ios_u32 cluster, void *buffer);
    ios_status (*write)(void *context, ios_u32 cluster, const void *buffer);
    ios_status (*zero)(void *context, ios_u32 cluster);
};

struct ios_fs_file_store {
    ios_u32 *fat;
    ios_size fat_entry_count;
    ios_u32 cluster_bytes;
    void *io_context;
    struct ios_fs_cluster_operations operations;
    bool writable;
};

enum ios_fs_seek_origin {
    IOS_FS_SEEK_SET,
    IOS_FS_SEEK_CURRENT,
    IOS_FS_SEEK_END
};

struct ios_fs_file {
    struct ios_fs_file_store *store;
    struct ios_fs_primary primary;
    ios_u32 position;
    ios_u32 *chain_workspace;
    ios_size chain_capacity;
    ios_u8 *cluster_scratch;
    ios_size scratch_size;
    bool writable;
    bool open;
};

ios_status ios_fs_file_open(
    struct ios_fs_file *file,
    struct ios_fs_file_store *store,
    const struct ios_fs_companion_disk *companion,
    const struct ios_fs_primary_disk *primary,
    bool writable,
    ios_u32 *chain_workspace,
    ios_size chain_capacity,
    ios_u8 *cluster_scratch,
    ios_size scratch_size
);
ios_status ios_fs_file_close(struct ios_fs_file *file);
ios_status ios_fs_file_read(
    struct ios_fs_file *file, void *buffer, ios_size length, ios_size *transferred
);
ios_status ios_fs_file_write(
    struct ios_fs_file *file, const void *buffer, ios_size length, ios_size *transferred
);
ios_status ios_fs_file_seek(
    struct ios_fs_file *file, enum ios_fs_seek_origin origin, ios_i64 offset, ios_u32 *position
);
ios_status ios_fs_file_append(
    struct ios_fs_file *file, const void *buffer, ios_size length, ios_size *transferred
);
ios_status ios_fs_file_metadata(
    const struct ios_fs_file *file, struct ios_fs_primary *primary
);

#endif
