#include <inferenceos/block.h>

#include <inferenceos/drivers/virtio_blk.h>

ios_status block_q35_initialize_primary(struct ios_block_device *device)
{
    return virtio_blk_pci_initialize(device);
}

const char *block_q35_last_stage(void)
{
    return virtio_blk_pci_last_stage();
}
