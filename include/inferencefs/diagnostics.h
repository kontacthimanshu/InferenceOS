#ifndef INFERENCEFS_DIAGNOSTICS_H
#define INFERENCEFS_DIAGNOSTICS_H

#include <inferencefs/format.h>
#include <inferenceos/base.h>
#include <inferenceos/vfs.h>

#define INFERENCEFS_DIAGNOSTIC_SIGNATURE_SIZE 8U
#define INFERENCEFS_DIAGNOSTIC_VISIBLE_EXTENSION_CAPACITY 4U
#define INFERENCEFS_DIAGNOSTIC_CHAIN_PAGE_CAPACITY 64U

typedef enum inferencefs_diagnostic_validation {
    INFERENCEFS_DIAGNOSTIC_VALIDATION_NOT_APPLICABLE = 0,
    INFERENCEFS_DIAGNOSTIC_VALIDATION_VALID = 1,
    INFERENCEFS_DIAGNOSTIC_VALIDATION_INVALID = 2,
    INFERENCEFS_DIAGNOSTIC_VALIDATION_UNAVAILABLE = 3
} inferencefs_diagnostic_validation;

typedef enum inferencefs_diagnostic_provenance {
    INFERENCEFS_DIAGNOSTIC_PROVENANCE_NONE = 0,
    INFERENCEFS_DIAGNOSTIC_PROVENANCE_OUT_OF_BOUNDS = 1,
    INFERENCEFS_DIAGNOSTIC_PROVENANCE_SUPERBLOCK = 2,
    INFERENCEFS_DIAGNOSTIC_PROVENANCE_PRIMARY_RECORD = 3,
    INFERENCEFS_DIAGNOSTIC_PROVENANCE_COMPANION_MISSING = 4,
    INFERENCEFS_DIAGNOSTIC_PROVENANCE_COMPANION_ORPHANED = 5,
    INFERENCEFS_DIAGNOSTIC_PROVENANCE_COMPANION_DUPLICATE = 6,
    INFERENCEFS_DIAGNOSTIC_PROVENANCE_COMPANION_VERSION = 7,
    INFERENCEFS_DIAGNOSTIC_PROVENANCE_COMPANION_ALGORITHM = 8,
    INFERENCEFS_DIAGNOSTIC_PROVENANCE_COMPANION_FLAGS = 9,
    INFERENCEFS_DIAGNOSTIC_PROVENANCE_COMPANION_RESERVED = 10,
    INFERENCEFS_DIAGNOSTIC_PROVENANCE_COMPANION_NAME_CHECKSUM = 11,
    INFERENCEFS_DIAGNOSTIC_PROVENANCE_COMPANION_EXTENSION_HASH = 12,
    INFERENCEFS_DIAGNOSTIC_PROVENANCE_COMPANION_CRC = 13,
    INFERENCEFS_DIAGNOSTIC_PROVENANCE_FAT_VALUE = 14,
    INFERENCEFS_DIAGNOSTIC_PROVENANCE_FAT_LOOP = 15,
    INFERENCEFS_DIAGNOSTIC_PROVENANCE_FAT_OUT_OF_RANGE = 16,
    INFERENCEFS_DIAGNOSTIC_PROVENANCE_FAT_BAD_CLUSTER = 17,
    INFERENCEFS_DIAGNOSTIC_PROVENANCE_FAT_CROSS_LINK = 18,
    INFERENCEFS_DIAGNOSTIC_PROVENANCE_CHAIN_SIZE_MISMATCH = 19
} inferencefs_diagnostic_provenance;

typedef enum inferencefs_diagnostic_chain_terminal {
    INFERENCEFS_DIAGNOSTIC_CHAIN_TERMINAL_NONE = 0,
    INFERENCEFS_DIAGNOSTIC_CHAIN_TERMINAL_EMPTY = 1,
    INFERENCEFS_DIAGNOSTIC_CHAIN_TERMINAL_END_OF_CHAIN = 2,
    INFERENCEFS_DIAGNOSTIC_CHAIN_TERMINAL_CORRUPT = 3
} inferencefs_diagnostic_chain_terminal;

typedef struct inferencefs_diagnostic_record_location {
    inferenceos_u64 lba;
    inferenceos_u32 directory_cluster;
    inferenceos_u32 slot_index;
    inferenceos_u16 byte_offset_in_sector;
    bool present;
} inferencefs_diagnostic_record_location;

typedef struct inferencefs_diagnostic_fsinfo {
    inferenceos_u8 signature[INFERENCEFS_DIAGNOSTIC_SIGNATURE_SIZE];
    inferenceos_u16 format_version;
    inferenceos_u64 total_sectors;
    inferenceos_u32 cluster_size;
    inferenceos_u32 fat_size_sectors;
    inferenceos_u32 root_cluster;
    inferenceos_u32 total_data_clusters;
    inferenceos_u32 free_cluster_count;
    inferenceos_u16 primary_record_size;
    inferenceos_u16 companion_record_size;
    inferenceos_u8 hash_algorithm_id;
    inferenceos_vfs_mount_state mount_state;
    inferencefs_diagnostic_validation validation;
    inferencefs_diagnostic_provenance provenance;
} inferencefs_diagnostic_fsinfo;

typedef struct inferencefs_diagnostic_fileinfo {
    inferenceos_u8 canonical_name[INFERENCEFS_SHORT_NAME_SIZE];
    inferenceos_vfs_node_type object_type;
    inferenceos_u32 attributes;
    inferenceos_u64 size;
    inferenceos_u32 first_cluster;
    inferencefs_diagnostic_record_location primary_record;
    inferencefs_diagnostic_record_location companion_record;
    inferencefs_diagnostic_validation validation;
    inferencefs_diagnostic_provenance provenance;
} inferencefs_diagnostic_fileinfo;

typedef struct inferencefs_diagnostic_hashinfo {
    char visible_extension[INFERENCEFS_DIAGNOSTIC_VISIBLE_EXTENSION_CAPACITY];
    inferenceos_u8 canonical_extension[INFERENCEFS_SHORT_NAME_EXTENSION_SIZE];
    inferenceos_u8 extension_length;
    inferenceos_u8 hash_algorithm_id;
    inferenceos_u32 stored_extension_hash;
    inferenceos_u32 recomputed_extension_hash;
    inferenceos_u8 record_version;
    bool committed;
    inferencefs_diagnostic_validation primary_name_checksum;
    inferencefs_diagnostic_validation companion_crc;
    inferencefs_diagnostic_validation validation;
    inferencefs_diagnostic_provenance provenance;
} inferencefs_diagnostic_hashinfo;

typedef struct inferencefs_diagnostic_fatinfo {
    inferenceos_u32 clusters[INFERENCEFS_DIAGNOSTIC_CHAIN_PAGE_CAPACITY];
    inferenceos_size cluster_count;
    inferenceos_u64 first_chain_index;
    inferenceos_u64 next_chain_index;
    bool has_more;
    inferencefs_diagnostic_chain_terminal terminal;
    inferenceos_u32 terminal_fat_value;
    inferencefs_diagnostic_validation validation;
    inferencefs_diagnostic_provenance provenance;
} inferencefs_diagnostic_fatinfo;

/* This extension contains queries only. It cannot replace or bypass VFS
 * mutation operations. Implementations must zero each complete response before
 * populating it and use only bounds-validated filesystem data. */
typedef struct inferencefs_diagnostic_operations {
    inferenceos_vfs_status (*fsinfo)(
        void *diagnostic_context,
        inferencefs_diagnostic_fsinfo *response
    );
    inferenceos_vfs_status (*fileinfo)(
        void *diagnostic_context,
        inferenceos_vfs_path path,
        inferencefs_diagnostic_fileinfo *response
    );
    inferenceos_vfs_status (*hashinfo)(
        void *diagnostic_context,
        inferenceos_vfs_path path,
        inferencefs_diagnostic_hashinfo *response
    );
    inferenceos_vfs_status (*fatinfo)(
        void *diagnostic_context,
        inferenceos_vfs_path path,
        inferenceos_u64 first_chain_index,
        inferencefs_diagnostic_fatinfo *response
    );
} inferencefs_diagnostic_operations;

typedef struct inferencefs_diagnostics {
    const inferencefs_diagnostic_operations *operations;
    void *diagnostic_context;
} inferencefs_diagnostics;

#endif
