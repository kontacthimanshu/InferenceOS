#ifndef INFERENCEOS_FS_RECORDS_H
#define INFERENCEOS_FS_RECORDS_H

#include <inferenceos/fs/format.h>

enum {
    IOS_FS_NAME_SIZE = 11,
    IOS_FS_EXTENSION_SIZE = 3,
    IOS_FS_HASH_TEXT_SIZE = 8,
    IOS_FS_ATTRIBUTE_DIRECTORY = 0x10,
    IOS_FS_ATTRIBUTE_REGULAR = 0x20,
    IOS_FS_COMPANION_RECORD_TYPE = 0xf1,
    IOS_FS_COMPANION_FLAG_COMMITTED = 0x01
};

struct ios_fs_primary_disk {
    ios_u8 name[11];
    ios_u8 attributes;
    ios_u8 reserved;
    ios_u8 create_tenth;
    ios_u8 create_time[2];
    ios_u8 create_date[2];
    ios_u8 access_date[2];
    ios_u8 first_cluster_high[2];
    ios_u8 write_time[2];
    ios_u8 write_date[2];
    ios_u8 first_cluster_low[2];
    ios_u8 file_size[4];
};

struct ios_fs_companion_disk {
    ios_u8 record_type;
    ios_u8 record_version;
    ios_u8 hash_algorithm_id;
    ios_u8 flags;
    ios_u8 extension_length;
    ios_u8 primary_name_checksum;
    ios_u8 reserved0[2];
    ios_u8 extension_hash_text[8];
    ios_u8 crc32[4];
    ios_u8 reserved1[12];
};

IOS_STATIC_ASSERT(sizeof(struct ios_fs_primary_disk) == 32, "primary record size");
IOS_STATIC_ASSERT(offsetof(struct ios_fs_primary_disk, name) == 0x00, "primary name offset");
IOS_STATIC_ASSERT(offsetof(struct ios_fs_primary_disk, attributes) == 0x0b, "primary attribute offset");
IOS_STATIC_ASSERT(offsetof(struct ios_fs_primary_disk, first_cluster_high) == 0x14, "primary cluster-high offset");
IOS_STATIC_ASSERT(offsetof(struct ios_fs_primary_disk, first_cluster_low) == 0x1a, "primary cluster-low offset");
IOS_STATIC_ASSERT(offsetof(struct ios_fs_primary_disk, file_size) == 0x1c, "primary size offset");
IOS_STATIC_ASSERT(sizeof(struct ios_fs_companion_disk) == 32, "companion record size");
IOS_STATIC_ASSERT(offsetof(struct ios_fs_companion_disk, extension_hash_text) == 0x08, "companion hash offset");
IOS_STATIC_ASSERT(offsetof(struct ios_fs_companion_disk, crc32) == 0x10, "companion CRC offset");

struct ios_fs_primary {
    ios_u8 name[IOS_FS_NAME_SIZE];
    ios_u8 attributes;
    ios_u32 first_cluster;
    ios_u32 file_size;
};

struct ios_fs_companion {
    bool committed;
    ios_u8 extension_length;
    ios_u8 primary_name_checksum;
    ios_u8 extension_hash_text[IOS_FS_HASH_TEXT_SIZE];
};

ios_status ios_fs_name_canonicalize_83(
    const char *input, ios_u8 output[IOS_FS_NAME_SIZE]
);
ios_status ios_fs_name_extension(
    const ios_u8 name[IOS_FS_NAME_SIZE],
    ios_u8 output[IOS_FS_EXTENSION_SIZE],
    ios_size *length
);
ios_u32 ios_fs_fnv1a32(const void *bytes, ios_size length);
void ios_fs_hash_text(ios_u32 hash, ios_u8 output[IOS_FS_HASH_TEXT_SIZE]);
ios_u8 ios_fs_primary_name_checksum(const ios_u8 name[IOS_FS_NAME_SIZE]);

ios_status ios_fs_primary_encode(
    const struct ios_fs_primary *value, struct ios_fs_primary_disk *disk
);
ios_status ios_fs_primary_decode(
    const struct ios_fs_primary_disk *disk, struct ios_fs_primary *value
);
ios_status ios_fs_companion_encode(
    const ios_u8 primary_name[IOS_FS_NAME_SIZE],
    bool committed,
    struct ios_fs_companion_disk *disk
);
ios_status ios_fs_companion_decode(
    const struct ios_fs_companion_disk *disk, struct ios_fs_companion *value
);
ios_status ios_fs_record_pair_validate(
    const struct ios_fs_companion_disk *companion,
    const struct ios_fs_primary_disk *primary
);
bool ios_fs_primary_types_equal(
    const struct ios_fs_primary *left,
    const struct ios_fs_primary *right,
    const ios_u8 left_hash[IOS_FS_HASH_TEXT_SIZE],
    const ios_u8 right_hash[IOS_FS_HASH_TEXT_SIZE]
);

#endif
