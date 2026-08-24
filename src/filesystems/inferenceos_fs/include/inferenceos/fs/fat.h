#ifndef INFERENCEOS_FS_FAT_H
#define INFERENCEOS_FS_FAT_H

#include <inferenceos/fs/format.h>

#define IOS_FS_FAT_FREE UINT32_C(0x00000000)
#define IOS_FS_FAT_BAD UINT32_C(0x0ffffff7)
#define IOS_FS_FAT_RESERVED_FIRST UINT32_C(0x0ffffff0)
#define IOS_FS_FAT_RESERVED_LAST UINT32_C(0x0ffffff6)
#define IOS_FS_FAT_EOC_FIRST UINT32_C(0x0ffffff8)
#define IOS_FS_FAT_VALUE_MASK UINT32_C(0x0fffffff)
#define IOS_FS_FAT_UPPER_MASK UINT32_C(0xf0000000)

ios_status ios_fs_fat_entry_encode(ios_u32 value, ios_u8 disk[4]);
ios_status ios_fs_fat_entry_decode(const ios_u8 disk[4], ios_u32 *value);

ios_status ios_fs_fat_traverse(
    const ios_u32 *fat,
    ios_size entry_count,
    ios_u32 start,
    ios_u32 *clusters,
    ios_size cluster_capacity,
    ios_size *cluster_count
);
ios_status ios_fs_fat_allocate(
    ios_u32 *fat,
    ios_size entry_count,
    ios_size requested,
    ios_u32 *clusters,
    ios_size cluster_capacity,
    ios_size *cluster_count
);
ios_status ios_fs_fat_free(
    ios_u32 *fat,
    ios_size entry_count,
    ios_u32 start,
    ios_u32 *workspace,
    ios_size workspace_capacity
);
ios_status ios_fs_fat_validate_ownership(
    const ios_u32 *fat,
    ios_size entry_count,
    const ios_u32 *starts,
    ios_size owner_count,
    ios_u32 *owner_map,
    ios_size owner_map_count
);
ios_status ios_fs_fat_validate_file_capacity(
    const ios_u32 *fat,
    ios_size entry_count,
    ios_u32 start,
    ios_u32 file_size,
    ios_u32 cluster_bytes
);

#endif
