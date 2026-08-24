#ifndef INFERENCEOS_FS_DIRECTORY_H
#define INFERENCEOS_FS_DIRECTORY_H

#include <inferenceos/fs/records.h>

enum {
    IOS_FS_DIRECTORY_SLOTS_PER_CLUSTER =
        IOS_FS_SECTOR_SIZE * IOS_FS_SECTORS_PER_CLUSTER / IOS_FS_PRIMARY_RECORD_SIZE
};

enum ios_fs_directory_entry_kind {
    IOS_FS_DIRECTORY_ENTRY_REGULAR,
    IOS_FS_DIRECTORY_ENTRY_DIRECTORY,
    IOS_FS_DIRECTORY_ENTRY_INTERNAL
};

struct ios_fs_directory_entry {
    struct ios_fs_primary primary;
    ios_size primary_slot;
    ios_size companion_slot;
    enum ios_fs_directory_entry_kind kind;
};

ios_status ios_fs_directory_scan(
    const ios_u8 *slots,
    ios_size slot_count,
    ios_size slots_per_cluster,
    struct ios_fs_directory_entry *entries,
    ios_size entry_capacity,
    ios_size *entry_count
);
ios_status ios_fs_directory_find_pair_slots(
    const ios_u8 *slots,
    ios_size slot_count,
    ios_size slots_per_cluster,
    ios_size *companion_slot
);

#endif
