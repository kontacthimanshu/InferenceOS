#ifndef INFERENCEOS_TEST_MEMORY_BLOCK_DEVICE_H
#define INFERENCEOS_TEST_MEMORY_BLOCK_DEVICE_H

#include <inferenceos/block_device.h>

#define INFERENCEOS_MEMORY_BLOCK_LOG_CAPACITY 1024U
#define INFERENCEOS_MEMORY_BLOCK_FAILURE_CAPACITY 32U

typedef enum inferenceos_memory_block_operation {
    INFERENCEOS_MEMORY_BLOCK_OPERATION_ANY = 0,
    INFERENCEOS_MEMORY_BLOCK_OPERATION_READ = 1,
    INFERENCEOS_MEMORY_BLOCK_OPERATION_WRITE = 2,
    INFERENCEOS_MEMORY_BLOCK_OPERATION_FLUSH = 3,
    INFERENCEOS_MEMORY_BLOCK_OPERATION_QUERY = 4
} inferenceos_memory_block_operation;

typedef struct inferenceos_memory_block_log_entry {
    inferenceos_u64 request_ordinal;
    inferenceos_u64 operation_ordinal;
    inferenceos_memory_block_operation operation;
    inferenceos_u64 start_lba;
    inferenceos_u32 sector_count;
    inferenceos_block_outcome outcome;
} inferenceos_memory_block_log_entry;

typedef struct inferenceos_memory_block_failure {
    inferenceos_memory_block_operation operation;
    /* Zero matches every ordinal of the selected operation. */
    inferenceos_u64 operation_ordinal;
    /* A zero sector count matches every LBA. Flush/query ignore this range. */
    inferenceos_u64 first_lba;
    inferenceos_u64 sector_count;
    inferenceos_block_outcome outcome;
    inferenceos_u32 remaining_triggers;
} inferenceos_memory_block_failure;

typedef struct inferenceos_memory_block_device {
    inferenceos_block_device device;
    inferenceos_u8 *storage;
    inferenceos_size storage_size;
    const char *identifier;
    inferenceos_u32 logical_sector_size;
    inferenceos_u64 sector_count;
    inferenceos_block_status status;
    bool flush_supported;
    inferenceos_u64 request_count;
    inferenceos_u64 operation_counts[5];
    inferenceos_memory_block_log_entry log[INFERENCEOS_MEMORY_BLOCK_LOG_CAPACITY];
    inferenceos_size log_count;
    inferenceos_size dropped_log_count;
    inferenceos_memory_block_failure
        failures[INFERENCEOS_MEMORY_BLOCK_FAILURE_CAPACITY];
    inferenceos_size failure_count;
} inferenceos_memory_block_device;

inferenceos_result inferenceos_memory_block_device_initialize(
    inferenceos_memory_block_device *memory_device,
    void *storage,
    inferenceos_size storage_size,
    inferenceos_u32 logical_sector_size,
    const char *identifier
);

const inferenceos_block_device *inferenceos_memory_block_device_interface(
    const inferenceos_memory_block_device *memory_device
);

void inferenceos_memory_block_device_reset_log(
    inferenceos_memory_block_device *memory_device
);

void inferenceos_memory_block_device_clear_failures(
    inferenceos_memory_block_device *memory_device
);

inferenceos_result inferenceos_memory_block_device_add_failure(
    inferenceos_memory_block_device *memory_device,
    inferenceos_memory_block_failure failure
);

inferenceos_result inferenceos_memory_block_device_set_status(
    inferenceos_memory_block_device *memory_device,
    inferenceos_block_status status
);

void inferenceos_memory_block_device_set_flush_supported(
    inferenceos_memory_block_device *memory_device,
    bool supported
);

#endif
