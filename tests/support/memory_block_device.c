#include "memory_block_device.h"

#include <inferenceos/memory.h>

static inferenceos_block_outcome block_outcome(
    inferenceos_result result,
    inferenceos_block_error error,
    inferenceos_u32 completed
)
{
    const inferenceos_block_outcome outcome = {
        .result = result,
        .error = error,
        .driver_detail = 0U,
        .sectors_completed = completed
    };
    return outcome;
}

static inferenceos_block_outcome success(inferenceos_u32 completed)
{
    return block_outcome(
        INFERENCEOS_RESULT_OK,
        INFERENCEOS_BLOCK_ERROR_NONE,
        completed
    );
}

static void begin_operation(
    inferenceos_memory_block_device *device,
    inferenceos_memory_block_operation operation,
    inferenceos_u64 *operation_ordinal
)
{
    const inferenceos_size operation_index = (inferenceos_size)operation;

    ++device->request_count;
    ++device->operation_counts[operation_index];
    *operation_ordinal = device->operation_counts[operation_index];
}

static void record_operation(
    inferenceos_memory_block_device *device,
    inferenceos_memory_block_operation operation,
    inferenceos_u64 operation_ordinal,
    inferenceos_u64 start_lba,
    inferenceos_u32 sector_count,
    inferenceos_block_outcome outcome
)
{
    if (device->log_count == INFERENCEOS_MEMORY_BLOCK_LOG_CAPACITY) {
        ++device->dropped_log_count;
        return;
    }
    device->log[device->log_count] = (inferenceos_memory_block_log_entry) {
        .request_ordinal = device->request_count,
        .operation_ordinal = operation_ordinal,
        .operation = operation,
        .start_lba = start_lba,
        .sector_count = sector_count,
        .outcome = outcome
    };
    ++device->log_count;
}

static bool ranges_intersect(
    inferenceos_u64 left_start,
    inferenceos_u64 left_count,
    inferenceos_u64 right_start,
    inferenceos_u64 right_count
)
{
    inferenceos_u64 left_end;
    inferenceos_u64 right_end;

    if (left_count == 0U || right_count == 0U) {
        return true;
    }
    if (!inferenceos_checked_add_u64(left_start, left_count, &left_end)
        || !inferenceos_checked_add_u64(right_start, right_count, &right_end)) {
        return false;
    }
    return left_start < right_end && right_start < left_end;
}

static inferenceos_memory_block_failure *matching_failure(
    inferenceos_memory_block_device *device,
    inferenceos_memory_block_operation operation,
    inferenceos_u64 operation_ordinal,
    inferenceos_u64 start_lba,
    inferenceos_u32 sector_count
)
{
    for (inferenceos_size index = 0U; index < device->failure_count; ++index) {
        inferenceos_memory_block_failure *failure = &device->failures[index];

        if (failure->remaining_triggers == 0U
            || (failure->operation != INFERENCEOS_MEMORY_BLOCK_OPERATION_ANY
                && failure->operation != operation)
            || (failure->operation_ordinal != 0U
                && failure->operation_ordinal != operation_ordinal)) {
            continue;
        }
        if ((operation == INFERENCEOS_MEMORY_BLOCK_OPERATION_READ
                || operation == INFERENCEOS_MEMORY_BLOCK_OPERATION_WRITE)
            && !ranges_intersect(
                start_lba,
                sector_count,
                failure->first_lba,
                failure->sector_count
            )) {
            continue;
        }
        --failure->remaining_triggers;
        return failure;
    }
    return NULL;
}

static inferenceos_block_outcome validate_device_state(
    const inferenceos_memory_block_device *device,
    bool writing
)
{
    switch (device->status) {
    case INFERENCEOS_BLOCK_STATUS_READY:
        return success(0U);
    case INFERENCEOS_BLOCK_STATUS_READ_ONLY:
        return writing
            ? block_outcome(
                INFERENCEOS_RESULT_READ_ONLY,
                INFERENCEOS_BLOCK_ERROR_READ_ONLY,
                0U
            )
            : success(0U);
    case INFERENCEOS_BLOCK_STATUS_ABSENT:
        return block_outcome(
            INFERENCEOS_RESULT_NOT_READY,
            INFERENCEOS_BLOCK_ERROR_ABSENT,
            0U
        );
    case INFERENCEOS_BLOCK_STATUS_BUSY:
        return block_outcome(
            INFERENCEOS_RESULT_BUSY,
            INFERENCEOS_BLOCK_ERROR_NOT_READY,
            0U
        );
    case INFERENCEOS_BLOCK_STATUS_FAILED:
    default:
        return block_outcome(
            INFERENCEOS_RESULT_IO_ERROR,
            INFERENCEOS_BLOCK_ERROR_DEVICE,
            0U
        );
    }
}

static inferenceos_block_outcome validate_range(
    const inferenceos_memory_block_device *device,
    inferenceos_u64 start_lba,
    inferenceos_u32 sector_count,
    inferenceos_size *byte_offset,
    inferenceos_size *byte_count
)
{
    inferenceos_u64 end_lba;
    inferenceos_u64 offset;
    inferenceos_u64 count;

    if (sector_count == 0U || byte_offset == NULL || byte_count == NULL) {
        return block_outcome(
            INFERENCEOS_RESULT_INVALID_ARGUMENT,
            INFERENCEOS_BLOCK_ERROR_INVALID_ARGUMENT,
            0U
        );
    }
    if (!inferenceos_checked_add_u64(start_lba, sector_count, &end_lba)
        || !inferenceos_checked_mul_u64(
            start_lba,
            device->logical_sector_size,
            &offset
        )
        || !inferenceos_checked_mul_u64(
            sector_count,
            device->logical_sector_size,
            &count
        )) {
        return block_outcome(
            INFERENCEOS_RESULT_OVERFLOW,
            INFERENCEOS_BLOCK_ERROR_ARITHMETIC_OVERFLOW,
            0U
        );
    }
    if (end_lba > device->sector_count
        || offset > SIZE_MAX
        || count > SIZE_MAX) {
        return block_outcome(
            INFERENCEOS_RESULT_OUT_OF_RANGE,
            INFERENCEOS_BLOCK_ERROR_OUT_OF_RANGE,
            0U
        );
    }
    *byte_offset = (inferenceos_size)offset;
    *byte_count = (inferenceos_size)count;
    return success(0U);
}

static inferenceos_u32 injected_completion(
    const inferenceos_memory_block_failure *failure,
    inferenceos_u32 requested
)
{
    return failure->outcome.sectors_completed < requested
        ? failure->outcome.sectors_completed
        : requested;
}

static inferenceos_block_outcome memory_read(
    void *context,
    inferenceos_u64 start_lba,
    inferenceos_u32 sector_count,
    void *destination
)
{
    inferenceos_memory_block_device *device = context;
    inferenceos_u64 ordinal;
    inferenceos_block_outcome outcome;
    inferenceos_size offset = 0U;
    inferenceos_size count = 0U;
    inferenceos_memory_block_failure *failure;

    begin_operation(device, INFERENCEOS_MEMORY_BLOCK_OPERATION_READ, &ordinal);
    if (destination == NULL) {
        outcome = block_outcome(INFERENCEOS_RESULT_INVALID_ARGUMENT,
            INFERENCEOS_BLOCK_ERROR_INVALID_ARGUMENT, 0U);
    } else {
        outcome = validate_device_state(device, false);
    }
    if (inferenceos_block_outcome_is_success(outcome)) {
        outcome = validate_range(device, start_lba, sector_count, &offset, &count);
    }
    if (!inferenceos_block_outcome_is_success(outcome)) {
        record_operation(device, INFERENCEOS_MEMORY_BLOCK_OPERATION_READ,
            ordinal, start_lba, sector_count, outcome);
        return outcome;
    }

    failure = matching_failure(device, INFERENCEOS_MEMORY_BLOCK_OPERATION_READ,
        ordinal, start_lba, sector_count);
    if (failure != NULL) {
        const inferenceos_u32 completed = injected_completion(failure, sector_count);
        const inferenceos_size completed_bytes =
            (inferenceos_size)completed * device->logical_sector_size;
        (void)memcpy(destination, device->storage + offset, completed_bytes);
        outcome = failure->outcome;
        outcome.sectors_completed = completed;
    } else {
        (void)memcpy(destination, device->storage + offset, count);
        outcome = success(sector_count);
    }
    record_operation(device, INFERENCEOS_MEMORY_BLOCK_OPERATION_READ,
        ordinal, start_lba, sector_count, outcome);
    return outcome;
}

static inferenceos_block_outcome memory_write(
    void *context,
    inferenceos_u64 start_lba,
    inferenceos_u32 sector_count,
    const void *source
)
{
    inferenceos_memory_block_device *device = context;
    inferenceos_u64 ordinal;
    inferenceos_block_outcome outcome;
    inferenceos_size offset = 0U;
    inferenceos_size count = 0U;
    inferenceos_memory_block_failure *failure;

    begin_operation(device, INFERENCEOS_MEMORY_BLOCK_OPERATION_WRITE, &ordinal);
    if (source == NULL) {
        outcome = block_outcome(INFERENCEOS_RESULT_INVALID_ARGUMENT,
            INFERENCEOS_BLOCK_ERROR_INVALID_ARGUMENT, 0U);
    } else {
        outcome = validate_device_state(device, true);
    }
    if (inferenceos_block_outcome_is_success(outcome)) {
        outcome = validate_range(device, start_lba, sector_count, &offset, &count);
    }
    if (!inferenceos_block_outcome_is_success(outcome)) {
        record_operation(device, INFERENCEOS_MEMORY_BLOCK_OPERATION_WRITE,
            ordinal, start_lba, sector_count, outcome);
        return outcome;
    }

    failure = matching_failure(device, INFERENCEOS_MEMORY_BLOCK_OPERATION_WRITE,
        ordinal, start_lba, sector_count);
    if (failure != NULL) {
        const inferenceos_u32 completed = injected_completion(failure, sector_count);
        const inferenceos_size completed_bytes =
            (inferenceos_size)completed * device->logical_sector_size;
        (void)memcpy(device->storage + offset, source, completed_bytes);
        outcome = failure->outcome;
        outcome.sectors_completed = completed;
    } else {
        (void)memcpy(device->storage + offset, source, count);
        outcome = success(sector_count);
    }
    record_operation(device, INFERENCEOS_MEMORY_BLOCK_OPERATION_WRITE,
        ordinal, start_lba, sector_count, outcome);
    return outcome;
}

static inferenceos_block_outcome memory_flush(void *context)
{
    inferenceos_memory_block_device *device = context;
    inferenceos_u64 ordinal;
    inferenceos_block_outcome outcome;
    inferenceos_memory_block_failure *failure;

    begin_operation(device, INFERENCEOS_MEMORY_BLOCK_OPERATION_FLUSH, &ordinal);
    outcome = validate_device_state(device, false);
    if (inferenceos_block_outcome_is_success(outcome) && !device->flush_supported) {
        outcome = block_outcome(INFERENCEOS_RESULT_UNSUPPORTED,
            INFERENCEOS_BLOCK_ERROR_UNSUPPORTED_FLUSH, 0U);
    }
    if (inferenceos_block_outcome_is_success(outcome)) {
        failure = matching_failure(device,
            INFERENCEOS_MEMORY_BLOCK_OPERATION_FLUSH, ordinal, 0U, 0U);
        outcome = failure == NULL ? success(0U) : failure->outcome;
    }
    record_operation(device, INFERENCEOS_MEMORY_BLOCK_OPERATION_FLUSH,
        ordinal, 0U, 0U, outcome);
    return outcome;
}

static inferenceos_block_outcome memory_query(
    void *context,
    inferenceos_block_info *info
)
{
    inferenceos_memory_block_device *device = context;
    inferenceos_u64 ordinal;
    inferenceos_block_outcome outcome;
    inferenceos_memory_block_failure *failure;

    begin_operation(device, INFERENCEOS_MEMORY_BLOCK_OPERATION_QUERY, &ordinal);
    if (info == NULL) {
        outcome = block_outcome(INFERENCEOS_RESULT_INVALID_ARGUMENT,
            INFERENCEOS_BLOCK_ERROR_INVALID_ARGUMENT, 0U);
        record_operation(device, INFERENCEOS_MEMORY_BLOCK_OPERATION_QUERY,
            ordinal, 0U, 0U, outcome);
        return outcome;
    }
    failure = matching_failure(device, INFERENCEOS_MEMORY_BLOCK_OPERATION_QUERY,
        ordinal, 0U, 0U);
    if (failure != NULL) {
        outcome = failure->outcome;
    } else {
        info->identifier = device->identifier;
        info->geometry.logical_sector_size = device->logical_sector_size;
        info->geometry.sector_count = device->sector_count;
        info->status = device->status;
        outcome = success(0U);
    }
    record_operation(device, INFERENCEOS_MEMORY_BLOCK_OPERATION_QUERY,
        ordinal, 0U, 0U, outcome);
    return outcome;
}

static const inferenceos_block_operations memory_operations = {
    .read = memory_read,
    .write = memory_write,
    .flush = memory_flush,
    .query = memory_query
};

inferenceos_result inferenceos_memory_block_device_initialize(
    inferenceos_memory_block_device *device,
    void *storage,
    inferenceos_size storage_size,
    inferenceos_u32 logical_sector_size,
    const char *identifier
)
{
    if (device == NULL || storage == NULL || storage_size == 0U
        || logical_sector_size == 0U
        || storage_size % logical_sector_size != 0U
        || identifier == NULL) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }

    (void)memset(device, 0, sizeof(*device));
    device->device.operations = &memory_operations;
    device->device.driver_context = device;
    device->storage = storage;
    device->storage_size = storage_size;
    device->identifier = identifier;
    device->logical_sector_size = logical_sector_size;
    device->sector_count = storage_size / logical_sector_size;
    device->status = INFERENCEOS_BLOCK_STATUS_READY;
    device->flush_supported = true;
    return INFERENCEOS_RESULT_OK;
}

const inferenceos_block_device *inferenceos_memory_block_device_interface(
    const inferenceos_memory_block_device *device
)
{
    return device == NULL ? NULL : &device->device;
}

void inferenceos_memory_block_device_reset_log(
    inferenceos_memory_block_device *device
)
{
    if (device == NULL) {
        return;
    }
    device->request_count = 0U;
    (void)memset(device->operation_counts, 0, sizeof(device->operation_counts));
    device->log_count = 0U;
    device->dropped_log_count = 0U;
}

void inferenceos_memory_block_device_clear_failures(
    inferenceos_memory_block_device *device
)
{
    if (device != NULL) {
        device->failure_count = 0U;
    }
}

inferenceos_result inferenceos_memory_block_device_add_failure(
    inferenceos_memory_block_device *device,
    inferenceos_memory_block_failure failure
)
{
    inferenceos_u64 end_lba;

    if (device == NULL
        || failure.operation > INFERENCEOS_MEMORY_BLOCK_OPERATION_QUERY
        || failure.remaining_triggers == 0U
        || inferenceos_block_outcome_is_success(failure.outcome)) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    if (failure.sector_count != 0U
        && !inferenceos_checked_add_u64(
            failure.first_lba,
            failure.sector_count,
            &end_lba
        )) {
        return INFERENCEOS_RESULT_OVERFLOW;
    }
    if (device->failure_count == INFERENCEOS_MEMORY_BLOCK_FAILURE_CAPACITY) {
        return INFERENCEOS_RESULT_NO_SPACE;
    }
    device->failures[device->failure_count] = failure;
    ++device->failure_count;
    return INFERENCEOS_RESULT_OK;
}

inferenceos_result inferenceos_memory_block_device_set_status(
    inferenceos_memory_block_device *device,
    inferenceos_block_status status
)
{
    if (device == NULL || status > INFERENCEOS_BLOCK_STATUS_FAILED) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    device->status = status;
    return INFERENCEOS_RESULT_OK;
}

void inferenceos_memory_block_device_set_flush_supported(
    inferenceos_memory_block_device *device,
    bool supported
)
{
    if (device != NULL) {
        device->flush_supported = supported;
    }
}
