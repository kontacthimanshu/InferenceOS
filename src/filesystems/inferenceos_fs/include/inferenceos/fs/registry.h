#ifndef INFERENCEOS_FS_REGISTRY_H
#define INFERENCEOS_FS_REGISTRY_H

#include <inferenceos/fs/records.h>

enum {
    IOS_FS_REGISTRY_RECORD_TYPE = 0xe1,
    IOS_FS_REGISTRY_FLAG_ACTIVE = 0x01,
    IOS_FS_REGISTRY_DEFAULT_ENABLED = 0
};

struct ios_fs_registry_record_disk {
    ios_u8 record_type;
    ios_u8 record_version;
    ios_u8 flags;
    ios_u8 extension_length;
    ios_u8 hash_algorithm_id;
    ios_u8 reserved0[3];
    ios_u8 canonical_extension[IOS_FS_EXTENSION_SIZE];
    ios_u8 reserved1;
    ios_u8 extension_hash_text[IOS_FS_HASH_TEXT_SIZE];
    ios_u8 last_directory_cluster[4];
    ios_u8 last_directory_slot[2];
    ios_u8 update_generation[2];
    ios_u8 crc32[4];
};

IOS_STATIC_ASSERT(
    sizeof(struct ios_fs_registry_record_disk) == 32,
    "registry record size"
);
IOS_STATIC_ASSERT(
    offsetof(struct ios_fs_registry_record_disk, record_type) == 0x00,
    "registry record type offset"
);
IOS_STATIC_ASSERT(
    offsetof(struct ios_fs_registry_record_disk, record_version) == 0x01,
    "registry record version offset"
);
IOS_STATIC_ASSERT(
    offsetof(struct ios_fs_registry_record_disk, flags) == 0x02,
    "registry flags offset"
);
IOS_STATIC_ASSERT(
    offsetof(struct ios_fs_registry_record_disk, extension_length) == 0x03,
    "registry extension length offset"
);
IOS_STATIC_ASSERT(
    offsetof(struct ios_fs_registry_record_disk, hash_algorithm_id) == 0x04,
    "registry hash algorithm offset"
);
IOS_STATIC_ASSERT(
    offsetof(struct ios_fs_registry_record_disk, canonical_extension) == 0x08,
    "registry canonical extension offset"
);
IOS_STATIC_ASSERT(
    offsetof(struct ios_fs_registry_record_disk, extension_hash_text) == 0x0c,
    "registry extension hash offset"
);
IOS_STATIC_ASSERT(
    offsetof(struct ios_fs_registry_record_disk, last_directory_cluster) == 0x14,
    "registry directory cluster offset"
);
IOS_STATIC_ASSERT(
    offsetof(struct ios_fs_registry_record_disk, last_directory_slot) == 0x18,
    "registry directory slot offset"
);
IOS_STATIC_ASSERT(
    offsetof(struct ios_fs_registry_record_disk, update_generation) == 0x1a,
    "registry generation offset"
);
IOS_STATIC_ASSERT(
    offsetof(struct ios_fs_registry_record_disk, crc32) == 0x1c,
    "registry CRC offset"
);

struct ios_fs_registry_record {
    bool active;
    ios_u8 extension_length;
    ios_u8 canonical_extension[IOS_FS_EXTENSION_SIZE];
    ios_u8 extension_hash_text[IOS_FS_HASH_TEXT_SIZE];
    ios_u32 last_directory_cluster;
    ios_u16 last_directory_slot;
    ios_u16 update_generation;
};

enum ios_fs_registry_health {
    IOS_FS_REGISTRY_DISABLED,
    IOS_FS_REGISTRY_HEALTHY,
    IOS_FS_REGISTRY_STALE,
    IOS_FS_REGISTRY_FULL,
    IOS_FS_REGISTRY_CORRUPT,
    IOS_FS_REGISTRY_REBUILDING
};

struct ios_fs_registry_source_entry {
    struct ios_fs_companion_disk companion;
    struct ios_fs_primary_disk primary;
    ios_u32 directory_cluster;
    ios_u16 primary_slot;
};

struct ios_fs_registry {
    struct ios_fs_registry_record_disk *records;
    ios_size capacity;
    bool enabled;
    enum ios_fs_registry_health health;
};

ios_status ios_fs_registry_record_encode(
    const struct ios_fs_registry_record *value,
    struct ios_fs_registry_record_disk *disk
);
ios_status ios_fs_registry_record_decode(
    const struct ios_fs_registry_record_disk *disk,
    struct ios_fs_registry_record *value
);

ios_status ios_fs_registry_initialize(
    struct ios_fs_registry *registry,
    struct ios_fs_registry_record_disk *records,
    ios_size capacity,
    bool enabled
);
bool ios_fs_registry_enabled(const struct ios_fs_registry *registry);
enum ios_fs_registry_health ios_fs_registry_health(
    const struct ios_fs_registry *registry
);
ios_size ios_fs_registry_active_count(const struct ios_fs_registry *registry);
ios_status ios_fs_registry_find(
    struct ios_fs_registry *registry,
    const ios_u8 *canonical_extension,
    ios_size extension_length,
    ios_size *record_index,
    struct ios_fs_registry_record *record
);
ios_status ios_fs_registry_refresh(
    struct ios_fs_registry *registry,
    const struct ios_fs_companion_disk *companion,
    const struct ios_fs_primary_disk *primary,
    ios_u32 directory_cluster,
    ios_u16 primary_slot,
    ios_size *record_index
);
ios_status ios_fs_registry_rebuild(
    struct ios_fs_registry *registry,
    const struct ios_fs_registry_source_entry *entries,
    ios_size entry_count
);
ios_status ios_fs_registry_lookup(
    struct ios_fs_registry *registry,
    const struct ios_fs_registry_source_entry *entries,
    ios_size entry_count,
    const ios_u8 *canonical_extension,
    ios_size extension_length,
    ios_size *matches,
    ios_size match_capacity,
    ios_size *match_count
);

#endif
