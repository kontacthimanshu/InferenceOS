#ifndef INFERENCEFS_FORMAT_H
#define INFERENCEFS_FORMAT_H

#include <inferencefs/name.h>
#include <inferenceos/base.h>

#define INFERENCEFS_FORMAT_VERSION UINT16_C(1)
#define INFERENCEFS_SUPERBLOCK_SIZE 512U
#define INFERENCEFS_SUPERBLOCK_HEADER_SIZE UINT16_C(64)
#define INFERENCEFS_SUPERBLOCK_CRC_LENGTH 64U
#define INFERENCEFS_LOGICAL_SECTOR_SIZE UINT16_C(512)
#define INFERENCEFS_SECTORS_PER_CLUSTER 8U
#define INFERENCEFS_FAT_COUNT 1U
#define INFERENCEFS_RESERVED_SECTORS UINT16_C(2)
#define INFERENCEFS_DIRECTORY_RECORD_SIZE UINT16_C(32)
#define INFERENCEFS_COMPANION_RECORD_SIZE UINT16_C(32)
#define INFERENCEFS_ROOT_CLUSTER UINT32_C(2)
#define INFERENCEFS_HASH_ALGORITHM_FNV1A32 1U
#define INFERENCEFS_COMPANION_RECORD_VERSION 1U
#define INFERENCEFS_PRIMARY_RECORD_VERSION UINT16_C(1)
#define INFERENCEFS_SUPERBLOCK_TRAILER_SIGNATURE UINT16_C(0xAA55)

#define INFERENCEFS_PRIMARY_ATTRIBUTE_DIRECTORY 0x10U
#define INFERENCEFS_PRIMARY_ATTRIBUTE_REGULAR_FILE 0x20U
#define INFERENCEFS_DIRECTORY_SLOT_END 0x00U
#define INFERENCEFS_DIRECTORY_SLOT_DELETED 0xE5U

#define INFERENCEFS_COMPANION_RECORD_TYPE 0xF1U
#define INFERENCEFS_COMPANION_FLAG_COMMITTED 0x01U
#define INFERENCEFS_COMPANION_SUPPORTED_FLAGS 0x01U

/* Multi-byte fields are byte arrays so callers must use explicit little-endian
 * accessors rather than native loads or stores. */
typedef struct INFERENCEOS_PACKED inferencefs_superblock_disk {
    inferenceos_u8 magic[8];
    inferenceos_u8 format_version_le[2];
    inferenceos_u8 header_size_le[2];
    inferenceos_u8 bytes_per_sector_le[2];
    inferenceos_u8 sectors_per_cluster;
    inferenceos_u8 fat_count;
    inferenceos_u8 reserved_sectors_le[2];
    inferenceos_u8 directory_entry_size_le[2];
    inferenceos_u8 hash_entry_size_le[2];
    inferenceos_u8 flags_le[2];
    inferenceos_u8 total_sectors_le[4];
    inferenceos_u8 sectors_per_fat_le[4];
    inferenceos_u8 root_cluster_le[4];
    inferenceos_u8 volume_serial_le[4];
    inferenceos_u8 volume_label[11];
    inferenceos_u8 hash_algorithm_id;
    inferenceos_u8 companion_record_version_le[2];
    inferenceos_u8 primary_record_version_le[2];
    inferenceos_u8 superblock_crc32_le[4];
    inferenceos_u8 reserved_header[4];
    inferenceos_u8 reserved[446];
    inferenceos_u8 trailer_signature_le[2];
} inferencefs_superblock_disk;

typedef struct INFERENCEOS_PACKED inferencefs_primary_record_disk {
    inferenceos_u8 name[INFERENCEFS_SHORT_NAME_SIZE];
    inferenceos_u8 attributes;
    inferenceos_u8 reserved;
    inferenceos_u8 create_tenth;
    inferenceos_u8 create_time_le[2];
    inferenceos_u8 create_date_le[2];
    inferenceos_u8 access_date_le[2];
    inferenceos_u8 first_cluster_high_le[2];
    inferenceos_u8 write_time_le[2];
    inferenceos_u8 write_date_le[2];
    inferenceos_u8 first_cluster_low_le[2];
    inferenceos_u8 file_size_le[4];
} inferencefs_primary_record_disk;

typedef struct INFERENCEOS_PACKED inferencefs_companion_record_disk {
    inferenceos_u8 record_type;
    inferenceos_u8 record_version;
    inferenceos_u8 hash_algorithm_id;
    inferenceos_u8 flags;
    inferenceos_u8 extension_length;
    inferenceos_u8 primary_name_checksum;
    inferenceos_u8 reserved0[2];
    inferenceos_u8 extension_hash_le[4];
    inferenceos_u8 record_crc32_le[4];
    inferenceos_u8 reserved1[16];
} inferencefs_companion_record_disk;

#define INFERENCEFS_ASSERT_OFFSET(type, member, expected)                         \
    INFERENCEOS_STATIC_ASSERT(                                                    \
        INFERENCEOS_OFFSETOF(type, member) == (expected),                         \
        #type "." #member " has an invalid on-disk offset"                       \
    )

INFERENCEOS_STATIC_ASSERT(
    sizeof(inferencefs_superblock_disk) == INFERENCEFS_SUPERBLOCK_SIZE,
    "inferencefs_superblock_disk must occupy one logical sector"
);
INFERENCEFS_ASSERT_OFFSET(inferencefs_superblock_disk, magic, 0x000U);
INFERENCEFS_ASSERT_OFFSET(inferencefs_superblock_disk, format_version_le, 0x008U);
INFERENCEFS_ASSERT_OFFSET(inferencefs_superblock_disk, header_size_le, 0x00AU);
INFERENCEFS_ASSERT_OFFSET(inferencefs_superblock_disk, bytes_per_sector_le, 0x00CU);
INFERENCEFS_ASSERT_OFFSET(inferencefs_superblock_disk, sectors_per_cluster, 0x00EU);
INFERENCEFS_ASSERT_OFFSET(inferencefs_superblock_disk, fat_count, 0x00FU);
INFERENCEFS_ASSERT_OFFSET(inferencefs_superblock_disk, reserved_sectors_le, 0x010U);
INFERENCEFS_ASSERT_OFFSET(inferencefs_superblock_disk, directory_entry_size_le, 0x012U);
INFERENCEFS_ASSERT_OFFSET(inferencefs_superblock_disk, hash_entry_size_le, 0x014U);
INFERENCEFS_ASSERT_OFFSET(inferencefs_superblock_disk, flags_le, 0x016U);
INFERENCEFS_ASSERT_OFFSET(inferencefs_superblock_disk, total_sectors_le, 0x018U);
INFERENCEFS_ASSERT_OFFSET(inferencefs_superblock_disk, sectors_per_fat_le, 0x01CU);
INFERENCEFS_ASSERT_OFFSET(inferencefs_superblock_disk, root_cluster_le, 0x020U);
INFERENCEFS_ASSERT_OFFSET(inferencefs_superblock_disk, volume_serial_le, 0x024U);
INFERENCEFS_ASSERT_OFFSET(inferencefs_superblock_disk, volume_label, 0x028U);
INFERENCEFS_ASSERT_OFFSET(inferencefs_superblock_disk, hash_algorithm_id, 0x033U);
INFERENCEFS_ASSERT_OFFSET(inferencefs_superblock_disk, companion_record_version_le, 0x034U);
INFERENCEFS_ASSERT_OFFSET(inferencefs_superblock_disk, primary_record_version_le, 0x036U);
INFERENCEFS_ASSERT_OFFSET(inferencefs_superblock_disk, superblock_crc32_le, 0x038U);
INFERENCEFS_ASSERT_OFFSET(inferencefs_superblock_disk, reserved_header, 0x03CU);
INFERENCEFS_ASSERT_OFFSET(inferencefs_superblock_disk, reserved, 0x040U);
INFERENCEFS_ASSERT_OFFSET(inferencefs_superblock_disk, trailer_signature_le, 0x1FEU);

INFERENCEOS_STATIC_ASSERT(
    sizeof(inferencefs_primary_record_disk) == INFERENCEFS_DIRECTORY_RECORD_SIZE,
    "inferencefs_primary_record_disk must occupy one directory slot"
);
INFERENCEFS_ASSERT_OFFSET(inferencefs_primary_record_disk, name, 0x00U);
INFERENCEFS_ASSERT_OFFSET(inferencefs_primary_record_disk, attributes, 0x0BU);
INFERENCEFS_ASSERT_OFFSET(inferencefs_primary_record_disk, reserved, 0x0CU);
INFERENCEFS_ASSERT_OFFSET(inferencefs_primary_record_disk, create_tenth, 0x0DU);
INFERENCEFS_ASSERT_OFFSET(inferencefs_primary_record_disk, create_time_le, 0x0EU);
INFERENCEFS_ASSERT_OFFSET(inferencefs_primary_record_disk, create_date_le, 0x10U);
INFERENCEFS_ASSERT_OFFSET(inferencefs_primary_record_disk, access_date_le, 0x12U);
INFERENCEFS_ASSERT_OFFSET(inferencefs_primary_record_disk, first_cluster_high_le, 0x14U);
INFERENCEFS_ASSERT_OFFSET(inferencefs_primary_record_disk, write_time_le, 0x16U);
INFERENCEFS_ASSERT_OFFSET(inferencefs_primary_record_disk, write_date_le, 0x18U);
INFERENCEFS_ASSERT_OFFSET(inferencefs_primary_record_disk, first_cluster_low_le, 0x1AU);
INFERENCEFS_ASSERT_OFFSET(inferencefs_primary_record_disk, file_size_le, 0x1CU);

INFERENCEOS_STATIC_ASSERT(
    sizeof(inferencefs_companion_record_disk) == INFERENCEFS_COMPANION_RECORD_SIZE,
    "inferencefs_companion_record_disk must occupy one directory slot"
);
INFERENCEFS_ASSERT_OFFSET(inferencefs_companion_record_disk, record_type, 0x00U);
INFERENCEFS_ASSERT_OFFSET(inferencefs_companion_record_disk, record_version, 0x01U);
INFERENCEFS_ASSERT_OFFSET(inferencefs_companion_record_disk, hash_algorithm_id, 0x02U);
INFERENCEFS_ASSERT_OFFSET(inferencefs_companion_record_disk, flags, 0x03U);
INFERENCEFS_ASSERT_OFFSET(inferencefs_companion_record_disk, extension_length, 0x04U);
INFERENCEFS_ASSERT_OFFSET(inferencefs_companion_record_disk, primary_name_checksum, 0x05U);
INFERENCEFS_ASSERT_OFFSET(inferencefs_companion_record_disk, reserved0, 0x06U);
INFERENCEFS_ASSERT_OFFSET(inferencefs_companion_record_disk, extension_hash_le, 0x08U);
INFERENCEFS_ASSERT_OFFSET(inferencefs_companion_record_disk, record_crc32_le, 0x0CU);
INFERENCEFS_ASSERT_OFFSET(inferencefs_companion_record_disk, reserved1, 0x10U);

#undef INFERENCEFS_ASSERT_OFFSET

#endif
