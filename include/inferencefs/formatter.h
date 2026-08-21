#ifndef INFERENCEFS_FORMATTER_H
#define INFERENCEFS_FORMATTER_H

#include <inferenceos/block_device.h>

typedef struct inferencefs_geometry {
    inferenceos_u32 total_sectors;
    inferenceos_u32 sectors_per_fat;
    inferenceos_u32 data_start_lba;
    inferenceos_u32 data_cluster_count;
} inferencefs_geometry;

/* Solve version-1 geometry without accessing the device. */
inferenceos_result inferencefs_geometry_solve(
    inferenceos_u32 logical_sector_size,
    inferenceos_u64 sector_count,
    inferencefs_geometry *geometry
);

/* Format the entire device. label must contain exactly 11 canonical,
 * space-padded bytes. The caller supplies the deterministic volume serial. */
inferenceos_result inferencefs_format_device(
    const inferenceos_block_device *device,
    inferenceos_u32 volume_serial,
    const inferenceos_u8 label[11],
    inferencefs_geometry *geometry
);

#endif
