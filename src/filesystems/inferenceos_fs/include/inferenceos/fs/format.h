#ifndef INFERENCEOS_FS_FORMAT_H
#define INFERENCEOS_FS_FORMAT_H

#include <inferenceos/errors.h>

struct ios_block_device;

enum {
    IOS_FS_SECTOR_SIZE = 512,
    IOS_FS_SECTORS_PER_CLUSTER = 8,
    IOS_FS_SUPERBLOCK_SECTORS = 2,
    IOS_FS_PRIMARY_RECORD_SIZE = 32,
    IOS_FS_COMPANION_RECORD_SIZE = 32,
    IOS_FS_REGISTRY_SECTORS = 4096,
    IOS_FS_ROOT_CLUSTER = 2,
    IOS_FS_FORMAT_VERSION = 1,
    IOS_FS_SUPERBLOCK_HEADER_SIZE = 80,
    IOS_FS_HASH_FNV1A32 = 1,
    IOS_FS_COMPANION_VERSION = 1,
    IOS_FS_REGISTRY_VERSION = 1,
    IOS_FS_VOLUME_LABEL_SIZE = 11,
    IOS_FS_SUPERBLOCK_TRAILER = 0xaa55
};

#define IOS_FS_MAGIC "INFOSFS1"
#define IOS_FS_MINIMUM_VOLUME_BYTES UINT64_C(50000000000)
#define IOS_FS_MAX_DATA_CLUSTER UINT32_C(0x0fffffef)
#define IOS_FS_FAT_END_OF_CHAIN UINT32_C(0x0fffffff)

/* Byte-array fields make the on-disk layout independent of host alignment and byte order. */
struct ios_fs_superblock_disk {
    ios_u8 magic[8];
    ios_u8 format_version[2];
    ios_u8 header_size[2];
    ios_u8 bytes_per_sector[2];
    ios_u8 sectors_per_cluster;
    ios_u8 fat_count;
    ios_u8 reserved_superblock_sectors[2];
    ios_u8 primary_record_size[2];
    ios_u8 companion_record_size[2];
    ios_u8 flags[2];
    ios_u8 total_sectors[8];
    ios_u8 sectors_per_fat[4];
    ios_u8 root_cluster[4];
    ios_u8 registry_start_sector[8];
    ios_u8 registry_sector_count[4];
    ios_u8 volume_serial[4];
    ios_u8 volume_label[IOS_FS_VOLUME_LABEL_SIZE];
    ios_u8 hash_algorithm_id;
    ios_u8 companion_record_version[2];
    ios_u8 registry_record_version[2];
    ios_u8 crc32[4];
    ios_u8 reserved_header[4];
    ios_u8 reserved[430];
    ios_u8 trailer_signature[2];
};

IOS_STATIC_ASSERT(sizeof(struct ios_fs_superblock_disk) == 512, "superblock must be one sector");
IOS_STATIC_ASSERT(offsetof(struct ios_fs_superblock_disk, magic) == 0x000, "magic offset");
IOS_STATIC_ASSERT(offsetof(struct ios_fs_superblock_disk, format_version) == 0x008, "format version offset");
IOS_STATIC_ASSERT(offsetof(struct ios_fs_superblock_disk, header_size) == 0x00a, "header size offset");
IOS_STATIC_ASSERT(offsetof(struct ios_fs_superblock_disk, bytes_per_sector) == 0x00c, "sector size offset");
IOS_STATIC_ASSERT(offsetof(struct ios_fs_superblock_disk, sectors_per_cluster) == 0x00e, "cluster geometry offset");
IOS_STATIC_ASSERT(offsetof(struct ios_fs_superblock_disk, fat_count) == 0x00f, "FAT count offset");
IOS_STATIC_ASSERT(offsetof(struct ios_fs_superblock_disk, reserved_superblock_sectors) == 0x010, "reserved sectors offset");
IOS_STATIC_ASSERT(offsetof(struct ios_fs_superblock_disk, primary_record_size) == 0x012, "primary record offset");
IOS_STATIC_ASSERT(offsetof(struct ios_fs_superblock_disk, companion_record_size) == 0x014, "companion record offset");
IOS_STATIC_ASSERT(offsetof(struct ios_fs_superblock_disk, flags) == 0x016, "flags offset");
IOS_STATIC_ASSERT(offsetof(struct ios_fs_superblock_disk, total_sectors) == 0x018, "total sectors offset");
IOS_STATIC_ASSERT(offsetof(struct ios_fs_superblock_disk, sectors_per_fat) == 0x020, "FAT length offset");
IOS_STATIC_ASSERT(offsetof(struct ios_fs_superblock_disk, root_cluster) == 0x024, "root cluster offset");
IOS_STATIC_ASSERT(offsetof(struct ios_fs_superblock_disk, registry_start_sector) == 0x028, "registry offset");
IOS_STATIC_ASSERT(offsetof(struct ios_fs_superblock_disk, registry_sector_count) == 0x030, "registry length offset");
IOS_STATIC_ASSERT(offsetof(struct ios_fs_superblock_disk, volume_serial) == 0x034, "serial offset");
IOS_STATIC_ASSERT(offsetof(struct ios_fs_superblock_disk, volume_label) == 0x038, "label offset");
IOS_STATIC_ASSERT(offsetof(struct ios_fs_superblock_disk, hash_algorithm_id) == 0x043, "hash id offset");
IOS_STATIC_ASSERT(offsetof(struct ios_fs_superblock_disk, companion_record_version) == 0x044, "companion version offset");
IOS_STATIC_ASSERT(offsetof(struct ios_fs_superblock_disk, registry_record_version) == 0x046, "registry version offset");
IOS_STATIC_ASSERT(offsetof(struct ios_fs_superblock_disk, crc32) == 0x048, "CRC offset");
IOS_STATIC_ASSERT(offsetof(struct ios_fs_superblock_disk, reserved_header) == 0x04c, "reserved header offset");
IOS_STATIC_ASSERT(offsetof(struct ios_fs_superblock_disk, reserved) == 0x050, "reserved offset");
IOS_STATIC_ASSERT(offsetof(struct ios_fs_superblock_disk, trailer_signature) == 0x1fe, "trailer offset");

struct ios_fs_superblock {
    ios_u64 total_sectors;
    ios_u32 sectors_per_fat;
    ios_u64 registry_start_sector;
    ios_u32 volume_serial;
    ios_u16 flags;
    ios_u8 volume_label[IOS_FS_VOLUME_LABEL_SIZE];
};

struct ios_fs_geometry {
    ios_u64 total_sectors;
    ios_u32 fat_sectors;
    ios_u64 registry_start_sector;
    ios_u64 data_start_sector;
    ios_u32 cluster_count;
    ios_u64 usable_bytes;
};

enum ios_fs_superblock_pair_state {
    IOS_FS_SUPERBLOCK_PAIR_READ_WRITE,
    IOS_FS_SUPERBLOCK_PAIR_DIAGNOSTIC,
    IOS_FS_SUPERBLOCK_PAIR_REJECTED
};

ios_u32 ios_fs_crc32_iso_hdlc(const void *bytes, ios_size length);
ios_status ios_fs_superblock_encode(
    const struct ios_fs_superblock *values, struct ios_fs_superblock_disk *disk
);
ios_status ios_fs_superblock_decode(
    const struct ios_fs_superblock_disk *disk, struct ios_fs_superblock *values
);
enum ios_fs_superblock_pair_state ios_fs_superblock_classify_pair(
    const struct ios_fs_superblock_disk *primary,
    const struct ios_fs_superblock_disk *backup
);
ios_status ios_fs_calculate_geometry(
    ios_u32 logical_sector_size, ios_u64 total_sectors, struct ios_fs_geometry *geometry
);
ios_status ios_fs_format(
    struct ios_block_device *device,
    ios_u32 volume_serial,
    const ios_u8 volume_label[IOS_FS_VOLUME_LABEL_SIZE],
    struct ios_fs_geometry *geometry
);

#endif
