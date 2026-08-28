#include <inferenceos/block.h>
#include <inferenceos/drivers/hyperv/vmbus.h>

ios_status vmbus_channel_write(
    struct ios_vmbus *bus, struct ios_vmbus_channel *channel, ios_u16 packet_type,
    ios_u16 flags, ios_u64 transaction_id, const void *payload, ios_size payload_size
)
{
    (void)bus; (void)channel; (void)packet_type; (void)flags;
    (void)transaction_id; (void)payload; (void)payload_size;
    return IOS_ERROR(IOS_E_NOT_SUPPORTED);
}

ios_status vmbus_channel_write_gpa_direct(
    struct ios_vmbus *bus, struct ios_vmbus_channel *channel, ios_u16 flags,
    ios_u64 transaction_id, const void *payload, ios_size payload_size,
    ios_uptr data_physical, ios_size data_size
)
{
    (void)bus; (void)channel; (void)flags; (void)transaction_id;
    (void)payload; (void)payload_size; (void)data_physical; (void)data_size;
    return IOS_ERROR(IOS_E_NOT_SUPPORTED);
}

ios_status vmbus_channel_read(
    struct ios_vmbus_channel *channel, ios_u16 *packet_type, ios_u64 *transaction_id,
    void *payload, ios_size payload_capacity, ios_size *payload_size
)
{
    (void)channel; (void)packet_type; (void)transaction_id;
    (void)payload; (void)payload_capacity; (void)payload_size;
    return IOS_ERROR(IOS_E_WOULD_BLOCK);
}

ios_status block_device_initialize(
    struct ios_block_device *device, void *context,
    const struct ios_block_device_operations *operations, ios_u32 logical_sector_size,
    ios_u64 sector_count, enum ios_block_device_status status
)
{
    (void)device; (void)context; (void)operations; (void)logical_sector_size;
    (void)sector_count; (void)status;
    return IOS_ERROR(IOS_E_NOT_SUPPORTED);
}

void x86_64_cpu_relax(void)
{
}
