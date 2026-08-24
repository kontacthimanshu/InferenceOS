#ifndef INFERENCEOS_FS_VALIDATOR_H
#define INFERENCEOS_FS_VALIDATOR_H

#include <inferenceos/block.h>
#include <inferenceos/fs/fat.h>

enum ios_fs_allocation_owner_kind {
    IOS_FS_OWNER_DIRECTORY,
    IOS_FS_OWNER_REGULAR_FILE
};

struct ios_fs_allocation_owner {
    ios_u32 first_cluster;
    ios_u32 file_size;
    enum ios_fs_allocation_owner_kind kind;
};

ios_status ios_fs_validate_owners(
    const ios_u32 *fat,
    ios_size entry_count,
    const struct ios_fs_allocation_owner *owners,
    ios_size owner_count,
    ios_u32 *owner_map,
    ios_size owner_map_count
);

ios_status ios_fs_validate_root_chain(
    struct ios_block_device *device,
    const struct ios_fs_geometry *geometry,
    ios_size *cluster_count
);

#endif
