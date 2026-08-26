#ifndef INFERENCEOS_BOOT_H
#define INFERENCEOS_BOOT_H

#include <inferenceos/boot_info.h>
#include <inferenceos/errors.h>
#include <inferenceos/memory.h>

enum {
    IOS_BOOT_MEMORY_DESCRIPTOR_VERSION = 1,
    IOS_BOOT_MEMORY_DESCRIPTOR_MINIMUM_SIZE = 40,
    IOS_BOOT_RESERVATION_MAX_COUNT = IOS_SYSTEM_MODULE_MAX_COUNT + 5
};

ios_status ios_boot_info_validate(const struct ios_boot_info *information);

ios_status ios_boot_build_memory_regions(
    const struct ios_boot_info *information,
    struct ios_physical_memory_region *regions,
    ios_size region_capacity,
    ios_size *region_count
);

ios_status ios_boot_build_reservations(
    const struct ios_boot_info *information,
    ios_uptr kernel_base,
    ios_u64 kernel_size,
    struct ios_physical_reservation *reservations,
    ios_size reservation_capacity,
    ios_size *reservation_count
);

#endif
