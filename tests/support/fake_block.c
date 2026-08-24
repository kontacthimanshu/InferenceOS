#include <inferenceos/fake_block.h>

#include <stdlib.h>
#include <string.h>

struct ios_fake_block_entry {
    ios_u64 sector;
    ios_u8 bytes[IOS_FAKE_BLOCK_SECTOR_SIZE];
    bool used;
};

static ios_status validate_range(
    const struct ios_fake_block *device,
    ios_u64 first_sector,
    ios_size sector_count,
    const void *buffer
)
{
    if (device == NULL || !device->initialized || buffer == NULL || sector_count == 0
        || first_sector >= device->sector_count
        || sector_count > device->sector_count - first_sector) {
        return IOS_ERROR(IOS_E_OUT_OF_RANGE);
    }
    return IOS_OK;
}

static struct ios_fake_block_entry *find_entry(
    struct ios_fake_block *device,
    ios_u64 sector
)
{
    for (ios_size index = 0; index < device->entry_capacity; ++index) {
        if (device->entries[index].used && device->entries[index].sector == sector) {
            return &device->entries[index];
        }
    }
    return NULL;
}

static struct ios_fake_block_entry *materialize(
    struct ios_fake_block *device,
    ios_u64 sector
)
{
    struct ios_fake_block_entry *entry = find_entry(device, sector);
    if (entry != NULL) {
        return entry;
    }
    for (ios_size index = 0; index < device->entry_capacity; ++index) {
        if (!device->entries[index].used) {
            entry = &device->entries[index];
            memset(entry, 0, sizeof(*entry));
            entry->used = true;
            entry->sector = sector;
            ++device->materialized_count;
            return entry;
        }
    }
    return NULL;
}

ios_status fake_block_initialize(
    struct ios_fake_block *device,
    ios_u64 sector_count,
    ios_size maximum_materialized_sectors,
    struct ios_fault_injector *faults
)
{
    if (device == NULL || sector_count == 0 || maximum_materialized_sectors == 0
        || maximum_materialized_sectors > SIZE_MAX / sizeof(struct ios_fake_block_entry)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    memset(device, 0, sizeof(*device));
    device->entries = calloc(maximum_materialized_sectors, sizeof(*device->entries));
    if (device->entries == NULL) {
        return IOS_ERROR(IOS_E_NO_MEMORY);
    }
    device->sector_count = sector_count;
    device->entry_capacity = maximum_materialized_sectors;
    device->faults = faults;
    device->initialized = true;
    return IOS_OK;
}

void fake_block_destroy(struct ios_fake_block *device)
{
    if (device != NULL) {
        free(device->entries);
        memset(device, 0, sizeof(*device));
    }
}

ios_status fake_block_read(
    struct ios_fake_block *device,
    ios_u64 first_sector,
    ios_size sector_count,
    void *buffer
)
{
    ios_status status = validate_range(device, first_sector, sector_count, buffer);
    ios_u8 *output = buffer;
    if (IOS_FAILED(status)) {
        return status;
    }
    ++device->read_operations;
    status = fault_injector_check(device->faults, IOS_FAULT_BLOCK_READ);
    if (IOS_FAILED(status)) {
        return status;
    }
    for (ios_size index = 0; index < sector_count; ++index) {
        const struct ios_fake_block_entry *entry = find_entry(device, first_sector + index);
        if (entry == NULL) {
            memset(output + index * IOS_FAKE_BLOCK_SECTOR_SIZE, 0, IOS_FAKE_BLOCK_SECTOR_SIZE);
        } else {
            memcpy(output + index * IOS_FAKE_BLOCK_SECTOR_SIZE,
                   entry->bytes, IOS_FAKE_BLOCK_SECTOR_SIZE);
        }
    }
    return IOS_OK;
}

ios_status fake_block_write(
    struct ios_fake_block *device,
    ios_u64 first_sector,
    ios_size sector_count,
    const void *buffer
)
{
    ios_status status = validate_range(device, first_sector, sector_count, buffer);
    const ios_u8 *input = buffer;
    ios_size needed_entries = 0;
    if (IOS_FAILED(status)) {
        return status;
    }
    ++device->write_operations;
    status = fault_injector_check(device->faults, IOS_FAULT_BLOCK_WRITE);
    if (IOS_FAILED(status)) {
        return status;
    }
    for (ios_size index = 0; index < sector_count; ++index) {
        if (find_entry(device, first_sector + index) == NULL) {
            ++needed_entries;
        }
    }
    if (needed_entries > device->entry_capacity - device->materialized_count) {
        return IOS_ERROR(IOS_E_NO_SPACE);
    }
    for (ios_size index = 0; index < sector_count; ++index) {
        struct ios_fake_block_entry *entry = materialize(device, first_sector + index);
        if (entry == NULL) {
            return IOS_ERROR(IOS_E_INVALID_STATE);
        }
        memcpy(entry->bytes, input + index * IOS_FAKE_BLOCK_SECTOR_SIZE,
               IOS_FAKE_BLOCK_SECTOR_SIZE);
    }
    return IOS_OK;
}

ios_status fake_block_flush(struct ios_fake_block *device)
{
    ios_status status;
    if (device == NULL || !device->initialized) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    ++device->flush_operations;
    status = fault_injector_check(device->faults, IOS_FAULT_BLOCK_FLUSH);
    if (IOS_SUCCEEDED(status)) {
        ++device->durable_generation;
    }
    return status;
}

ios_status fake_block_corrupt(
    struct ios_fake_block *device,
    ios_u64 sector,
    ios_size byte_offset,
    ios_u8 xor_mask
)
{
    struct ios_fake_block_entry *entry;
    if (device == NULL || !device->initialized || sector >= device->sector_count
        || byte_offset >= IOS_FAKE_BLOCK_SECTOR_SIZE || xor_mask == 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    entry = materialize(device, sector);
    if (entry == NULL) {
        return IOS_ERROR(IOS_E_NO_SPACE);
    }
    entry->bytes[byte_offset] ^= xor_mask;
    return IOS_OK;
}

ios_u64 fake_block_capacity_bytes(const struct ios_fake_block *device)
{
    return device == NULL || device->sector_count > UINT64_MAX / IOS_FAKE_BLOCK_SECTOR_SIZE
        ? 0 : device->sector_count * IOS_FAKE_BLOCK_SECTOR_SIZE;
}
