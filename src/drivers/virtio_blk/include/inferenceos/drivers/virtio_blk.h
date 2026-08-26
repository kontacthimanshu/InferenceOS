#ifndef INFERENCEOS_DRIVERS_VIRTIO_BLK_H
#define INFERENCEOS_DRIVERS_VIRTIO_BLK_H

#include <inferenceos/arch/pci.h>
#include <inferenceos/block.h>

enum {
    IOS_VIRTIO_PCI_VENDOR_ID = 0x1af4,
    IOS_VIRTIO_BLK_MODERN_DEVICE_ID = 0x1042,
    IOS_VIRTIO_BLK_SECTOR_SIZE = 512,
    IOS_VIRTIO_BLK_QUEUE_LIMIT = 128,
    IOS_VIRTIO_BLK_REQUEST_READ = 0,
    IOS_VIRTIO_BLK_REQUEST_WRITE = 1,
    IOS_VIRTIO_BLK_REQUEST_FLUSH = 4,
    IOS_VIRTIO_BLK_STATUS_OK = 0,
    IOS_VIRTIO_BLK_STATUS_IO_ERROR = 1,
    IOS_VIRTIO_BLK_STATUS_UNSUPPORTED = 2,
    IOS_VIRTIO_BLK_PCI_REQUEST_LIMIT = 64,
    IOS_VIRTIO_BLK_PCI_TIMEOUT_SPINS = 100000
};

#define IOS_VIRTIO_F_VERSION_1 (UINT64_C(1) << 32)
#define IOS_VIRTIO_BLK_F_FLUSH (UINT64_C(1) << 9)

enum ios_virtio_blk_request_state {
    IOS_VIRTIO_BLK_REQUEST_FREE,
    IOS_VIRTIO_BLK_REQUEST_AVAILABLE,
    IOS_VIRTIO_BLK_REQUEST_USED
};

struct ios_virtio_blk_request {
    enum ios_virtio_blk_request_state state;
    ios_u32 type;
    ios_u64 sector;
    void *buffer;
    ios_size byte_count;
    ios_u32 generation;
    ios_status result;
};

struct ios_virtio_blk_device;

struct ios_virtio_blk_transport {
    void *context;
    ios_status (*reset)(void *context, const struct ios_pci_function *function);
    ios_status (*read_features)(void *context, ios_u64 *features);
    ios_status (*write_features)(void *context, ios_u64 features);
    ios_status (*setup_queue)(void *context, ios_u16 queue_index, ios_u16 queue_size);
    ios_status (*set_driver_ok)(void *context);
    ios_status (*read_capacity)(void *context, ios_u64 *sector_count);
    ios_status (*publish)(void *context, ios_u16 request_id,
                          const struct ios_virtio_blk_request *request);
    ios_status (*service)(void *context, struct ios_virtio_blk_device *device);
};

struct ios_virtio_blk_pci_platform {
    struct ios_pci_config_access pci;
    ios_u8 (*mmio_read8)(void *context, ios_uptr address);
    ios_u16 (*mmio_read16)(void *context, ios_uptr address);
    ios_u32 (*mmio_read32)(void *context, ios_uptr address);
    void (*mmio_write8)(void *context, ios_uptr address, ios_u8 value);
    void (*mmio_write16)(void *context, ios_uptr address, ios_u16 value);
    void (*mmio_write32)(void *context, ios_uptr address, ios_u32 value);
    void (*memory_barrier)(void *context);
    void (*relax)(void *context);
    ios_status (*dma_address)(
        void *context,
        const void *buffer,
        ios_size byte_count,
        ios_uptr *physical_address
    );
    void *context;
};

struct ios_virtio_blk_device {
    struct ios_pci_function pci;
    struct ios_virtio_blk_transport transport;
    struct ios_virtio_blk_request *requests;
    ios_u64 accepted_features;
    ios_u64 sector_count;
    ios_u16 queue_capacity;
    ios_u16 outstanding;
    bool ready;
};

ios_status virtio_blk_initialize(
    struct ios_virtio_blk_device *device,
    const struct ios_pci_function *functions,
    ios_size function_count,
    const struct ios_virtio_blk_transport *transport,
    struct ios_virtio_blk_request *request_storage,
    ios_u16 queue_capacity
);
ios_status virtio_blk_submit(
    struct ios_virtio_blk_device *device,
    ios_u32 type,
    ios_u64 sector,
    void *buffer,
    ios_size byte_count,
    ios_u32 generation,
    ios_u16 *request_id
);
ios_status virtio_blk_complete(
    struct ios_virtio_blk_device *device, ios_u16 request_id, ios_u8 device_status
);
ios_status virtio_blk_poll(
    struct ios_virtio_blk_device *device, ios_u16 request_id, ios_u32 *generation
);
void virtio_blk_fail(struct ios_virtio_blk_device *device, ios_status status);
ios_u64 virtio_blk_capacity_bytes(const struct ios_virtio_blk_device *device);
ios_status virtio_blk_pci_initialize(struct ios_block_device *block_device);
const char *virtio_blk_pci_last_stage(void);
ios_status virtio_blk_pci_initialize_with_platform(
    struct ios_block_device *block_device,
    const struct ios_virtio_blk_pci_platform *platform
);

#endif
