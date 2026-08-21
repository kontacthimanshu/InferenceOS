#ifndef INFERENCEOS_BLOCK_DEVICE_H
#define INFERENCEOS_BLOCK_DEVICE_H

#include <inferenceos/base.h>
#include <inferenceos/result.h>

typedef enum inferenceos_block_status {
    INFERENCEOS_BLOCK_STATUS_ABSENT = 0,
    INFERENCEOS_BLOCK_STATUS_READY = 1,
    INFERENCEOS_BLOCK_STATUS_BUSY = 2,
    INFERENCEOS_BLOCK_STATUS_READ_ONLY = 3,
    INFERENCEOS_BLOCK_STATUS_FAILED = 4
} inferenceos_block_status;

typedef enum inferenceos_block_error {
    INFERENCEOS_BLOCK_ERROR_NONE = 0,
    INFERENCEOS_BLOCK_ERROR_INVALID_HANDLE = 1,
    INFERENCEOS_BLOCK_ERROR_INVALID_ARGUMENT = 2,
    INFERENCEOS_BLOCK_ERROR_ARITHMETIC_OVERFLOW = 3,
    INFERENCEOS_BLOCK_ERROR_OUT_OF_RANGE = 4,
    INFERENCEOS_BLOCK_ERROR_ABSENT = 5,
    INFERENCEOS_BLOCK_ERROR_NOT_READY = 6,
    INFERENCEOS_BLOCK_ERROR_READ_ONLY = 7,
    INFERENCEOS_BLOCK_ERROR_TIMEOUT = 8,
    INFERENCEOS_BLOCK_ERROR_CONTROLLER = 9,
    INFERENCEOS_BLOCK_ERROR_DEVICE = 10,
    INFERENCEOS_BLOCK_ERROR_UNSUPPORTED_FLUSH = 11,
    INFERENCEOS_BLOCK_ERROR_PARTIAL_TRANSFER = 12,
    INFERENCEOS_BLOCK_ERROR_INTERNAL = 13
} inferenceos_block_error;

typedef struct inferenceos_block_geometry {
    inferenceos_u32 logical_sector_size;
    inferenceos_u64 sector_count;
} inferenceos_block_geometry;

typedef struct inferenceos_block_info {
    /* Driver-owned, immutable, null-terminated identifier with static or
     * device-long lifetime. */
    const char *identifier;
    inferenceos_block_geometry geometry;
    inferenceos_block_status status;
} inferenceos_block_info;

typedef struct inferenceos_block_outcome {
    inferenceos_result result;
    inferenceos_block_error error;
    /* Transport-specific diagnostic value; generic callers must not use it
     * to determine success. */
    inferenceos_u32 driver_detail;
    inferenceos_u32 sectors_completed;
} inferenceos_block_outcome;

struct inferenceos_block_device;
typedef struct inferenceos_block_device inferenceos_block_device;

typedef inferenceos_block_outcome (*inferenceos_block_read_fn)(
    void *driver_context,
    inferenceos_u64 start_lba,
    inferenceos_u32 sector_count,
    void *destination
);

typedef inferenceos_block_outcome (*inferenceos_block_write_fn)(
    void *driver_context,
    inferenceos_u64 start_lba,
    inferenceos_u32 sector_count,
    const void *source
);

typedef inferenceos_block_outcome (*inferenceos_block_flush_fn)(
    void *driver_context
);

typedef inferenceos_block_outcome (*inferenceos_block_query_fn)(
    void *driver_context,
    inferenceos_block_info *info
);

typedef struct inferenceos_block_operations {
    inferenceos_block_read_fn read;
    inferenceos_block_write_fn write;
    inferenceos_block_flush_fn flush;
    inferenceos_block_query_fn query;
} inferenceos_block_operations;

/* Driver state remains opaque to generic consumers. Drivers provide a stable
 * operations table and context whose lifetimes cover every use of the device. */
struct inferenceos_block_device {
    const inferenceos_block_operations *operations;
    void *driver_context;
};

static inline inferenceos_block_outcome inferenceos_block_failure(
    inferenceos_result result,
    inferenceos_block_error error
)
{
    const inferenceos_block_outcome outcome = {
        .result = result,
        .error = error,
        .driver_detail = 0U,
        .sectors_completed = 0U
    };
    return outcome;
}

/* These helpers are the only transport-neutral entry points used above a
 * driver. A zero sector count is invalid. Read and write implementations must
 * validate the complete LBA range before starting I/O. */
static inline inferenceos_block_outcome inferenceos_block_read(
    const inferenceos_block_device *device,
    inferenceos_u64 start_lba,
    inferenceos_u32 sector_count,
    void *destination
)
{
    if (device == NULL || device->operations == NULL
        || device->operations->read == NULL) {
        return inferenceos_block_failure(
            INFERENCEOS_RESULT_INVALID_ARGUMENT,
            INFERENCEOS_BLOCK_ERROR_INVALID_HANDLE
        );
    }
    if (sector_count == 0U || destination == NULL) {
        return inferenceos_block_failure(
            INFERENCEOS_RESULT_INVALID_ARGUMENT,
            INFERENCEOS_BLOCK_ERROR_INVALID_ARGUMENT
        );
    }
    return device->operations->read(
        device->driver_context,
        start_lba,
        sector_count,
        destination
    );
}

static inline inferenceos_block_outcome inferenceos_block_write(
    const inferenceos_block_device *device,
    inferenceos_u64 start_lba,
    inferenceos_u32 sector_count,
    const void *source
)
{
    if (device == NULL || device->operations == NULL
        || device->operations->write == NULL) {
        return inferenceos_block_failure(
            INFERENCEOS_RESULT_INVALID_ARGUMENT,
            INFERENCEOS_BLOCK_ERROR_INVALID_HANDLE
        );
    }
    if (sector_count == 0U || source == NULL) {
        return inferenceos_block_failure(
            INFERENCEOS_RESULT_INVALID_ARGUMENT,
            INFERENCEOS_BLOCK_ERROR_INVALID_ARGUMENT
        );
    }
    return device->operations->write(
        device->driver_context,
        start_lba,
        sector_count,
        source
    );
}

static inline inferenceos_block_outcome inferenceos_block_flush(
    const inferenceos_block_device *device
)
{
    if (device == NULL || device->operations == NULL) {
        return inferenceos_block_failure(
            INFERENCEOS_RESULT_INVALID_ARGUMENT,
            INFERENCEOS_BLOCK_ERROR_INVALID_HANDLE
        );
    }
    if (device->operations->flush == NULL) {
        return inferenceos_block_failure(
            INFERENCEOS_RESULT_UNSUPPORTED,
            INFERENCEOS_BLOCK_ERROR_UNSUPPORTED_FLUSH
        );
    }
    return device->operations->flush(device->driver_context);
}

static inline inferenceos_block_outcome inferenceos_block_query(
    const inferenceos_block_device *device,
    inferenceos_block_info *info
)
{
    if (device == NULL || device->operations == NULL
        || device->operations->query == NULL) {
        return inferenceos_block_failure(
            INFERENCEOS_RESULT_INVALID_ARGUMENT,
            INFERENCEOS_BLOCK_ERROR_INVALID_HANDLE
        );
    }
    if (info == NULL) {
        return inferenceos_block_failure(
            INFERENCEOS_RESULT_INVALID_ARGUMENT,
            INFERENCEOS_BLOCK_ERROR_INVALID_ARGUMENT
        );
    }
    return device->operations->query(device->driver_context, info);
}

static inline bool inferenceos_block_outcome_is_success(
    inferenceos_block_outcome outcome
)
{
    return inferenceos_result_is_success(outcome.result)
        && outcome.error == INFERENCEOS_BLOCK_ERROR_NONE;
}

#endif
