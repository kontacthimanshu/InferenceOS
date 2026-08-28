#ifndef INFERENCEOS_BLOCK_H
#define INFERENCEOS_BLOCK_H

#include <inferenceos/errors.h>

enum {
    IOS_BLOCK_SECTOR_SIZE = 512,
    IOS_BLOCK_CACHE_MAX_ENTRIES = 256
};

enum ios_block_device_status {
    IOS_BLOCK_DEVICE_OFFLINE,
    IOS_BLOCK_DEVICE_READY,
    IOS_BLOCK_DEVICE_READ_ONLY,
    IOS_BLOCK_DEVICE_FAILED
};

struct ios_block_device_operations {
    ios_status (*read)(void *context, ios_u64 first_sector, ios_size sector_count, void *buffer);
    ios_status (*write)(void *context, ios_u64 first_sector, ios_size sector_count,
                        const void *buffer);
    ios_status (*flush)(void *context);
};

struct ios_block_device {
    void *context;
    struct ios_block_device_operations operations;
    ios_u64 sector_count;
    ios_u32 logical_sector_size;
    enum ios_block_device_status status;
};

ios_status block_device_initialize(
    struct ios_block_device *device,
    void *context,
    const struct ios_block_device_operations *operations,
    ios_u32 logical_sector_size,
    ios_u64 sector_count,
    enum ios_block_device_status status
);
ios_status block_device_read(
    struct ios_block_device *device, ios_u64 first_sector, ios_size sector_count, void *buffer
);
ios_status block_device_write(
    struct ios_block_device *device, ios_u64 first_sector, ios_size sector_count, const void *buffer
);
ios_status block_device_flush(struct ios_block_device *device);
ios_u64 block_device_capacity_bytes(const struct ios_block_device *device);
enum ios_block_device_status block_device_get_status(const struct ios_block_device *device);

/* Platform discovery stays behind the generic block boundary. */
ios_status block_platform_initialize_primary(struct ios_block_device *device);
const char *block_platform_last_stage(void);
void block_platform_set_boot_partition_guid(const ios_u8 guid[16]);
bool block_platform_is_hyperv(void);

enum ios_block_disk_classification {
    IOS_BLOCK_DISK_ELIGIBLE_BLANK,
    IOS_BLOCK_DISK_ELIGIBLE_INFERENCE_FS,
    IOS_BLOCK_DISK_PROTECTED_BOOT,
    IOS_BLOCK_DISK_PROTECTED_PARTITIONED,
    IOS_BLOCK_DISK_PROTECTED_FOREIGN,
    IOS_BLOCK_DISK_REJECTED_IO
};

ios_status block_classify_data_disk(
    struct ios_block_device *device,
    const ios_u8 boot_partition_guid[16],
    enum ios_block_disk_classification *classification
);

enum ios_block_cache_state {
    IOS_BLOCK_CACHE_EMPTY,
    IOS_BLOCK_CACHE_CLEAN,
    IOS_BLOCK_CACHE_DIRTY,
    IOS_BLOCK_CACHE_ERROR
};

struct ios_block_cache_entry {
    ios_u8 bytes[IOS_BLOCK_SECTOR_SIZE];
    ios_u64 sector;
    ios_u64 dirty_generation;
    ios_u64 last_use;
    ios_u32 pin_count;
    enum ios_block_cache_state state;
    bool io_active;
};

struct ios_block_cache {
    struct ios_block_device *device;
    struct ios_block_cache_entry *entries;
    ios_size entry_count;
    ios_u64 current_generation;
    ios_u64 use_clock;
};

ios_status block_cache_initialize(
    struct ios_block_cache *cache,
    struct ios_block_device *device,
    struct ios_block_cache_entry *entry_storage,
    ios_size entry_count
);
ios_status block_cache_read(
    struct ios_block_cache *cache, ios_u64 sector, void *buffer
);
ios_status block_cache_write(
    struct ios_block_cache *cache, ios_u64 sector, const void *buffer, ios_u64 generation
);
ios_status block_cache_pin(struct ios_block_cache *cache, ios_u64 sector);
ios_status block_cache_unpin(struct ios_block_cache *cache, ios_u64 sector);
ios_status block_cache_advance_generation(struct ios_block_cache *cache, ios_u64 *generation);
ios_status block_cache_barrier(struct ios_block_cache *cache, ios_u64 target_generation);
void block_cache_invalidate(struct ios_block_cache *cache);

#endif
