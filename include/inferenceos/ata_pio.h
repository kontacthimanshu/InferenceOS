#ifndef INFERENCEOS_ATA_PIO_H
#define INFERENCEOS_ATA_PIO_H

#include <inferenceos/block_device.h>

#define INFERENCEOS_ATA_PIO_SECTOR_SIZE UINT32_C(512)
#define INFERENCEOS_ATA_PIO_DEFAULT_POLL_LIMIT UINT32_C(1000000)
#define INFERENCEOS_ATA_PIO_LBA28_SECTOR_LIMIT UINT64_C(0x10000000)

typedef enum inferenceos_ata_drive {
    INFERENCEOS_ATA_DRIVE_MASTER = 0,
    INFERENCEOS_ATA_DRIVE_SLAVE = 1
} inferenceos_ata_drive;

typedef struct inferenceos_ata_pio_device {
    inferenceos_block_device interface;
    inferenceos_u16 io_base;
    inferenceos_u16 control_base;
    inferenceos_ata_drive drive;
    inferenceos_block_status status;
    inferenceos_u64 sector_count;
    inferenceos_u32 poll_limit;
    inferenceos_u32 last_poll_count;
    inferenceos_u32 port_io_count;
    inferenceos_u8 last_status;
    inferenceos_u8 last_error;
    bool lba28_supported;
    bool flush_supported;
    char identifier[41];
} inferenceos_ata_pio_device;

inferenceos_block_outcome inferenceos_ata_pio_initialize(
    inferenceos_ata_pio_device *device,
    inferenceos_u16 io_base,
    inferenceos_u16 control_base,
    inferenceos_ata_drive drive,
    inferenceos_u32 poll_limit
);

const inferenceos_block_device *inferenceos_ata_pio_interface(
    inferenceos_ata_pio_device *device
);

#endif
