#include <inferenceos/ata_pio.h>
#include <inferenceos/device_registry.h>

#define PIIX_PRIMARY_IO_BASE UINT16_C(0x01F0)
#define PIIX_PRIMARY_CONTROL_BASE UINT16_C(0x03F6)

static inferenceos_ata_pio_device piix_data_disk;
static const inferenceos_block_device *registered_devices[
    INFERENCEOS_DEVICE_REGISTRY_CAPACITY
];
static inferenceos_size registered_count;
static bool registry_initialized;

inferenceos_result inferenceos_device_registry_initialize(void)
{
    inferenceos_block_outcome outcome;

    if (registry_initialized) {
        return INFERENCEOS_RESULT_OK;
    }
    registered_count = 0U;
    registered_devices[0] = NULL;
    outcome = inferenceos_ata_pio_initialize(
        &piix_data_disk,
        PIIX_PRIMARY_IO_BASE,
        PIIX_PRIMARY_CONTROL_BASE,
        INFERENCEOS_ATA_DRIVE_SLAVE,
        INFERENCEOS_ATA_PIO_DEFAULT_POLL_LIMIT
    );
    if (!inferenceos_block_outcome_is_success(outcome)) {
        if (outcome.error == INFERENCEOS_BLOCK_ERROR_ABSENT) {
            registry_initialized = true;
            return INFERENCEOS_RESULT_OK;
        }
        return outcome.result;
    }
    registered_devices[0] = inferenceos_ata_pio_interface(&piix_data_disk);
    if (registered_devices[0] == NULL) {
        return INFERENCEOS_RESULT_INTERNAL_ERROR;
    }
    registered_count = 1U;
    registry_initialized = true;
    return INFERENCEOS_RESULT_OK;
}

inferenceos_size inferenceos_device_registry_count(void)
{
    return registry_initialized ? registered_count : 0U;
}

inferenceos_result inferenceos_device_registry_get(
    inferenceos_size index,
    const inferenceos_block_device **device
)
{
    if (device == NULL) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    if (!registry_initialized) {
        return INFERENCEOS_RESULT_NOT_READY;
    }
    if (index >= registered_count) {
        return INFERENCEOS_RESULT_OUT_OF_RANGE;
    }
    *device = registered_devices[index];
    return INFERENCEOS_RESULT_OK;
}

inferenceos_result inferenceos_device_registry_data_disk(
    const inferenceos_block_device **device
)
{
    if (device == NULL) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    if (!registry_initialized) {
        return INFERENCEOS_RESULT_NOT_READY;
    }
    if (registered_count == 0U) {
        return INFERENCEOS_RESULT_NOT_FOUND;
    }
    *device = registered_devices[0];
    return INFERENCEOS_RESULT_OK;
}
