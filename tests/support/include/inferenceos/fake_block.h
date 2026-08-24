#ifndef INFERENCEOS_FAKE_BLOCK_H
#define INFERENCEOS_FAKE_BLOCK_H

#include <inferenceos/fault_injector.h>

enum {
    IOS_FAKE_BLOCK_SECTOR_SIZE = 512
};

struct ios_fake_block_entry;

struct ios_fake_block {
    ios_u64 sector_count;
    ios_size entry_capacity;
    ios_size materialized_count;
    struct ios_fake_block_entry *entries;
    struct ios_fault_injector *faults;
    ios_u64 read_operations;
    ios_u64 write_operations;
    ios_u64 flush_operations;
    ios_u64 durable_generation;
    bool initialized;
};

ios_status fake_block_initialize(
    struct ios_fake_block *device,
    ios_u64 sector_count,
    ios_size maximum_materialized_sectors,
    struct ios_fault_injector *faults
);
void fake_block_destroy(struct ios_fake_block *device);
ios_status fake_block_read(
    struct ios_fake_block *device,
    ios_u64 first_sector,
    ios_size sector_count,
    void *buffer
);
ios_status fake_block_write(
    struct ios_fake_block *device,
    ios_u64 first_sector,
    ios_size sector_count,
    const void *buffer
);
ios_status fake_block_flush(struct ios_fake_block *device);
ios_status fake_block_corrupt(
    struct ios_fake_block *device,
    ios_u64 sector,
    ios_size byte_offset,
    ios_u8 xor_mask
);
ios_u64 fake_block_capacity_bytes(const struct ios_fake_block *device);

#endif
