#ifndef INFERENCEOS_DEVICE_REGISTRY_H
#define INFERENCEOS_DEVICE_REGISTRY_H

#include <inferenceos/block_device.h>

#define INFERENCEOS_DEVICE_REGISTRY_CAPACITY 1U

/* Probe the one documented PIIX primary-slave data disk. An absent optional
 * data disk is a successful empty registry; other probe failures propagate. */
inferenceos_result inferenceos_device_registry_initialize(void);

inferenceos_size inferenceos_device_registry_count(void);

inferenceos_result inferenceos_device_registry_get(
    inferenceos_size index,
    const inferenceos_block_device **device
);

inferenceos_result inferenceos_device_registry_data_disk(
    const inferenceos_block_device **device
);

#endif
