#include <inferenceos/block.h>

#include <inferenceos/arch/hyperv.h>
#include <inferenceos/runtime.h>

ios_status block_q35_initialize_primary(struct ios_block_device *device);
const char *block_q35_last_stage(void);
ios_status block_hyperv_initialize_primary(
    struct ios_block_device *device, const ios_u8 boot_partition_guid[16]
);
const char *block_hyperv_last_stage(void);

static ios_u8 boot_partition_guid[16];
static bool selected_hyperv;

void block_platform_set_boot_partition_guid(const ios_u8 guid[16])
{
    if (guid == NULL) {
        memset(boot_partition_guid, 0, sizeof(boot_partition_guid));
    } else {
        memcpy(boot_partition_guid, guid, sizeof(boot_partition_guid));
    }
}

bool block_platform_is_hyperv(void)
{
    return x86_64_hyperv_present();
}

ios_status block_platform_initialize_primary(struct ios_block_device *device)
{
    selected_hyperv = x86_64_hyperv_present();
    return selected_hyperv
        ? block_hyperv_initialize_primary(device, boot_partition_guid)
        : block_q35_initialize_primary(device);
}

const char *block_platform_last_stage(void)
{
    return selected_hyperv ? block_hyperv_last_stage() : block_q35_last_stage();
}
