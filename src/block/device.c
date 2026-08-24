#include <inferenceos/block.h>

#include <inferenceos/runtime.h>

static ios_status validate_range(
    const struct ios_block_device *device, ios_u64 first_sector, ios_size sector_count,
    const void *buffer)
{
    if (device == NULL || buffer == NULL || sector_count == 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (device->status == IOS_BLOCK_DEVICE_OFFLINE || device->status == IOS_BLOCK_DEVICE_FAILED) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    if (first_sector >= device->sector_count || sector_count > device->sector_count - first_sector) {
        return IOS_ERROR(IOS_E_OUT_OF_RANGE);
    }
    return IOS_OK;
}

ios_status block_device_initialize(
    struct ios_block_device *device, void *context,
    const struct ios_block_device_operations *operations, ios_u32 logical_sector_size,
    ios_u64 sector_count, enum ios_block_device_status status)
{
    if (device == NULL || operations == NULL || operations->read == NULL
        || operations->write == NULL || operations->flush == NULL || logical_sector_size == 0
        || sector_count == 0 || status == IOS_BLOCK_DEVICE_OFFLINE) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *device = (struct ios_block_device){
        context, *operations, sector_count, logical_sector_size, status
    };
    return IOS_OK;
}

ios_status block_device_read(
    struct ios_block_device *device, ios_u64 first_sector, ios_size sector_count, void *buffer)
{
    ios_status status = validate_range(device, first_sector, sector_count, buffer);
    return IOS_FAILED(status) ? status
                              : device->operations.read(device->context, first_sector,
                                                        sector_count, buffer);
}

ios_status block_device_write(
    struct ios_block_device *device, ios_u64 first_sector, ios_size sector_count,
    const void *buffer)
{
    ios_status status = validate_range(device, first_sector, sector_count, buffer);
    if (IOS_FAILED(status)) return status;
    if (device->status == IOS_BLOCK_DEVICE_READ_ONLY) return IOS_ERROR(IOS_E_READ_ONLY);
    return device->operations.write(device->context, first_sector, sector_count, buffer);
}

ios_status block_device_flush(struct ios_block_device *device)
{
    if (device == NULL || device->status == IOS_BLOCK_DEVICE_OFFLINE
        || device->status == IOS_BLOCK_DEVICE_FAILED) return IOS_ERROR(IOS_E_INVALID_STATE);
    return device->operations.flush(device->context);
}

ios_u64 block_device_capacity_bytes(const struct ios_block_device *device)
{
    if (device == NULL || device->logical_sector_size == 0
        || device->sector_count > UINT64_MAX / device->logical_sector_size) return 0;
    return device->sector_count * device->logical_sector_size;
}

enum ios_block_device_status block_device_get_status(const struct ios_block_device *device)
{
    return device == NULL ? IOS_BLOCK_DEVICE_OFFLINE : device->status;
}
