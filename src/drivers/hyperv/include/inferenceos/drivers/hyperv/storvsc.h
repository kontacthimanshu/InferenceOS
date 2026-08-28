#ifndef INFERENCEOS_DRIVERS_HYPERV_STORVSC_H
#define INFERENCEOS_DRIVERS_HYPERV_STORVSC_H

#include <inferenceos/block.h>
#include <inferenceos/drivers/hyperv/vmbus.h>

enum {
    IOS_STORVSC_MAX_TRANSFER_BYTES = 256 * 1024,
    IOS_STORVSC_SCSI_STATUS_GOOD = 0,
    IOS_STORVSC_SRB_STATUS_SUCCESS = 1,
    IOS_STORVSC_DATA_WRITE = 0,
    IOS_STORVSC_DATA_READ = 1,
    IOS_STORVSC_DATA_UNKNOWN = 2
};

struct ios_storvsc {
    struct ios_vmbus *bus;
    struct ios_vmbus_channel *channel;
    void *bounce;
    ios_uptr bounce_physical;
    ios_size bounce_size;
    ios_u64 sector_count;
    ios_u64 next_transaction_id;
    ios_u32 spin_limit;
    ios_u16 protocol_version;
    ios_u8 lun;
    bool ready;
    const char *last_stage;
};

ios_status storvsc_build_rw10_cdb(
    ios_u8 cdb[16], bool write, ios_u64 first_sector, ios_u16 sector_count
);
ios_status storvsc_parse_capacity10(
    const ios_u8 response[8], ios_u64 *sector_count, ios_u32 *logical_sector_size
);
ios_status storvsc_initialize(
    struct ios_storvsc *driver,
    struct ios_vmbus *bus,
    struct ios_vmbus_channel *channel,
    void *bounce,
    ios_uptr bounce_physical,
    ios_size bounce_size,
    ios_u8 lun,
    ios_u32 spin_limit
);
ios_status storvsc_publish(struct ios_storvsc *driver, struct ios_block_device *device);
ios_status storvsc_select_lun(struct ios_storvsc *driver, ios_u8 lun);
const char *storvsc_last_stage(const struct ios_storvsc *driver);

#endif
