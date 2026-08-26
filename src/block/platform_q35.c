#include <inferenceos/block.h>

#include <inferenceos/drivers/virtio_blk.h>

ios_status block_platform_initialize_primary(struct ios_block_device *device)
{
    return virtio_blk_pci_initialize(device);
}
