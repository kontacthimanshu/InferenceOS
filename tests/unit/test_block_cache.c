#include "../support/memory_block_device.h"
#include "../support/test_assert.h"

#include <inferenceos/block_cache.h>
#include <inferenceos/memory.h>

#define TEST_SECTOR_COUNT 128U

static inferenceos_u8 test_storage[
    TEST_SECTOR_COUNT * INFERENCEOS_BLOCK_CACHE_SECTOR_SIZE
];
static inferenceos_memory_block_device test_memory_device;
static inferenceos_block_cache test_cache;

static void setup_cache(void)
{
    (void)memset(test_storage, 0, sizeof(test_storage));
    for (inferenceos_size lba = 0U; lba < TEST_SECTOR_COUNT; ++lba) {
        test_storage[lba * INFERENCEOS_BLOCK_CACHE_SECTOR_SIZE]
            = (inferenceos_u8)lba;
    }
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_OK,
        inferenceos_memory_block_device_initialize(
            &test_memory_device,
            test_storage,
            sizeof(test_storage),
            INFERENCEOS_BLOCK_CACHE_SECTOR_SIZE,
            "memory-test-device"
        ));
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_OK,
        inferenceos_block_cache_initialize(&test_cache));
}

static const inferenceos_block_device *test_device(void)
{
    return inferenceos_memory_block_device_interface(&test_memory_device);
}

static void test_cache_hit_avoids_device_io(void)
{
    inferenceos_block_cache_entry *first;
    inferenceos_block_cache_entry *second;
    inferenceos_block_cache_outcome outcome;

    setup_cache();
    outcome = inferenceos_block_cache_acquire(
        &test_cache, test_device(), 7U, &first);
    INFERENCEOS_TEST_ASSERT(inferenceos_block_cache_outcome_is_success(outcome));
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(7U, first->data[0]);
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_OK,
        inferenceos_block_cache_release(&test_cache, first));

    inferenceos_memory_block_device_reset_log(&test_memory_device);
    outcome = inferenceos_block_cache_acquire(
        &test_cache, test_device(), 7U, &second);
    INFERENCEOS_TEST_ASSERT(inferenceos_block_cache_outcome_is_success(outcome));
    INFERENCEOS_TEST_ASSERT_POINTER_EQUAL(first, second);
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(1U, second->pin_count);
    INFERENCEOS_TEST_ASSERT_SIZE_EQUAL(0U, test_memory_device.log_count);
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_OK,
        inferenceos_block_cache_release(&test_cache, second));
}

static void test_dirty_eviction_writes_before_replacement_read(void)
{
    inferenceos_block_cache_entry *entry;
    inferenceos_block_cache_outcome outcome;

    setup_cache();
    for (inferenceos_u64 lba = 0U;
         lba < INFERENCEOS_BLOCK_CACHE_CAPACITY;
         ++lba) {
        outcome = inferenceos_block_cache_acquire(
            &test_cache, test_device(), lba, &entry);
        INFERENCEOS_TEST_ASSERT(
            inferenceos_block_cache_outcome_is_success(outcome));
        if (lba == 0U) {
            entry->data[0] = 0xA5U;
            INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_OK,
                inferenceos_block_cache_mark_dirty(&test_cache, entry));
        }
        INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_OK,
            inferenceos_block_cache_release(&test_cache, entry));
    }

    inferenceos_memory_block_device_reset_log(&test_memory_device);
    outcome = inferenceos_block_cache_acquire(
        &test_cache, test_device(), INFERENCEOS_BLOCK_CACHE_CAPACITY, &entry);
    INFERENCEOS_TEST_ASSERT(inferenceos_block_cache_outcome_is_success(outcome));
    INFERENCEOS_TEST_ASSERT_SIZE_EQUAL(3U, test_memory_device.log_count);
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(
        INFERENCEOS_MEMORY_BLOCK_OPERATION_QUERY,
        test_memory_device.log[0].operation);
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(
        INFERENCEOS_MEMORY_BLOCK_OPERATION_WRITE,
        test_memory_device.log[1].operation);
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(0U,
        test_memory_device.log[1].start_lba);
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(
        INFERENCEOS_MEMORY_BLOCK_OPERATION_READ,
        test_memory_device.log[2].operation);
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(INFERENCEOS_BLOCK_CACHE_CAPACITY,
        test_memory_device.log[2].start_lba);
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(0xA5U, test_storage[0]);
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_OK,
        inferenceos_block_cache_release(&test_cache, entry));
}

static void test_flush_uses_entry_order_then_device_flush(void)
{
    const inferenceos_u64 lbas[3] = { 3U, 1U, 2U };
    inferenceos_block_cache_entry *entry;

    setup_cache();
    for (inferenceos_size index = 0U; index < INFERENCEOS_ARRAY_COUNT(lbas);
         ++index) {
        inferenceos_block_cache_outcome outcome =
            inferenceos_block_cache_acquire(
                &test_cache, test_device(), lbas[index], &entry);
        INFERENCEOS_TEST_ASSERT(
            inferenceos_block_cache_outcome_is_success(outcome));
        entry->data[0] = (inferenceos_u8)(0xA0U + index);
        INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_OK,
            inferenceos_block_cache_mark_dirty(&test_cache, entry));
        INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_OK,
            inferenceos_block_cache_release(&test_cache, entry));
    }

    inferenceos_memory_block_device_reset_log(&test_memory_device);
    INFERENCEOS_TEST_ASSERT(inferenceos_block_cache_outcome_is_success(
        inferenceos_block_cache_flush(&test_cache, test_device())));
    INFERENCEOS_TEST_ASSERT_SIZE_EQUAL(4U, test_memory_device.log_count);
    for (inferenceos_size index = 0U; index < 3U; ++index) {
        INFERENCEOS_TEST_ASSERT_I64_EQUAL(
            INFERENCEOS_MEMORY_BLOCK_OPERATION_WRITE,
            test_memory_device.log[index].operation);
        INFERENCEOS_TEST_ASSERT_U64_EQUAL(lbas[index],
            test_memory_device.log[index].start_lba);
    }
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(
        INFERENCEOS_MEMORY_BLOCK_OPERATION_FLUSH,
        test_memory_device.log[3].operation);
}

static void test_out_of_range_miss_never_reads(void)
{
    inferenceos_block_cache_entry *entry = NULL;
    inferenceos_block_cache_outcome outcome;

    setup_cache();
    inferenceos_memory_block_device_reset_log(&test_memory_device);
    outcome = inferenceos_block_cache_acquire(
        &test_cache, test_device(), TEST_SECTOR_COUNT, &entry);
    INFERENCEOS_TEST_ASSERT_FALSE(
        inferenceos_block_cache_outcome_is_success(outcome));
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_OUT_OF_RANGE,
        outcome.result);
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(
        INFERENCEOS_BLOCK_CACHE_ERROR_OUT_OF_RANGE, outcome.error);
    INFERENCEOS_TEST_ASSERT_NULL(entry);
    INFERENCEOS_TEST_ASSERT_SIZE_EQUAL(1U, test_memory_device.log_count);
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(
        INFERENCEOS_MEMORY_BLOCK_OPERATION_QUERY,
        test_memory_device.log[0].operation);
}

static void test_failed_eviction_retains_dirty_entry(void)
{
    const inferenceos_memory_block_failure failure = {
        .operation = INFERENCEOS_MEMORY_BLOCK_OPERATION_WRITE,
        .operation_ordinal = 1U,
        .first_lba = 0U,
        .sector_count = 1U,
        .outcome = {
            .result = INFERENCEOS_RESULT_IO_ERROR,
            .error = INFERENCEOS_BLOCK_ERROR_DEVICE,
            .driver_detail = 0x55U,
            .sectors_completed = 0U
        },
        .remaining_triggers = 1U
    };
    inferenceos_block_cache_entry *entry;
    inferenceos_block_cache_entry *dirty_entry = NULL;
    inferenceos_block_cache_outcome outcome;

    setup_cache();
    for (inferenceos_u64 lba = 0U;
         lba < INFERENCEOS_BLOCK_CACHE_CAPACITY;
         ++lba) {
        outcome = inferenceos_block_cache_acquire(
            &test_cache, test_device(), lba, &entry);
        INFERENCEOS_TEST_ASSERT(
            inferenceos_block_cache_outcome_is_success(outcome));
        if (lba == 0U) {
            dirty_entry = entry;
            entry->data[0] = 0xCCU;
            INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_OK,
                inferenceos_block_cache_mark_dirty(&test_cache, entry));
        }
        INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_OK,
            inferenceos_block_cache_release(&test_cache, entry));
    }
    inferenceos_memory_block_device_reset_log(&test_memory_device);
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_OK,
        inferenceos_memory_block_device_add_failure(
            &test_memory_device, failure));

    outcome = inferenceos_block_cache_acquire(
        &test_cache, test_device(), INFERENCEOS_BLOCK_CACHE_CAPACITY, &entry);
    INFERENCEOS_TEST_ASSERT_FALSE(
        inferenceos_block_cache_outcome_is_success(outcome));
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(
        INFERENCEOS_BLOCK_CACHE_ERROR_WRITEBACK_FAILED, outcome.error);
    INFERENCEOS_TEST_ASSERT_NULL(entry);
    INFERENCEOS_TEST_ASSERT_NOT_NULL(dirty_entry);
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(
        INFERENCEOS_BLOCK_CACHE_ENTRY_WRITEBACK_FAILED, dirty_entry->state);
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(0U, dirty_entry->lba);
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(0xCCU, dirty_entry->data[0]);
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(0U, test_storage[0]);
    INFERENCEOS_TEST_ASSERT_SIZE_EQUAL(2U, test_memory_device.log_count);
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(
        INFERENCEOS_MEMORY_BLOCK_OPERATION_WRITE,
        test_memory_device.log[1].operation);
}

static void test_failed_device_flush_is_retryable(void)
{
    const inferenceos_memory_block_failure failure = {
        .operation = INFERENCEOS_MEMORY_BLOCK_OPERATION_FLUSH,
        .operation_ordinal = 1U,
        .first_lba = 0U,
        .sector_count = 0U,
        .outcome = {
            .result = INFERENCEOS_RESULT_TIMEOUT,
            .error = INFERENCEOS_BLOCK_ERROR_TIMEOUT,
            .driver_detail = 0x66U,
            .sectors_completed = 0U
        },
        .remaining_triggers = 1U
    };
    inferenceos_block_cache_entry *entry;
    inferenceos_block_cache_outcome outcome;
    inferenceos_block_cache_status status;

    setup_cache();
    outcome = inferenceos_block_cache_acquire(
        &test_cache, test_device(), 5U, &entry);
    INFERENCEOS_TEST_ASSERT(inferenceos_block_cache_outcome_is_success(outcome));
    entry->data[0] = 0xDDU;
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_OK,
        inferenceos_block_cache_mark_dirty(&test_cache, entry));
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_OK,
        inferenceos_block_cache_release(&test_cache, entry));

    inferenceos_memory_block_device_reset_log(&test_memory_device);
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_OK,
        inferenceos_memory_block_device_add_failure(
            &test_memory_device, failure));
    outcome = inferenceos_block_cache_flush(&test_cache, test_device());
    INFERENCEOS_TEST_ASSERT_FALSE(
        inferenceos_block_cache_outcome_is_success(outcome));
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(
        INFERENCEOS_BLOCK_CACHE_ERROR_DEVICE_FLUSH_FAILED, outcome.error);
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(
        INFERENCEOS_BLOCK_CACHE_FLUSH_FAILED, test_cache.flush_state);
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(
        INFERENCEOS_BLOCK_CACHE_ENTRY_CLEAN, entry->state);
    INFERENCEOS_TEST_ASSERT_U64_EQUAL(0xDDU,
        test_storage[5U * INFERENCEOS_BLOCK_CACHE_SECTOR_SIZE]);

    inferenceos_memory_block_device_clear_failures(&test_memory_device);
    outcome = inferenceos_block_cache_flush(&test_cache, test_device());
    INFERENCEOS_TEST_ASSERT(inferenceos_block_cache_outcome_is_success(outcome));
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(INFERENCEOS_RESULT_OK,
        inferenceos_block_cache_query(&test_cache, &status));
    INFERENCEOS_TEST_ASSERT_I64_EQUAL(
        INFERENCEOS_BLOCK_CACHE_FLUSH_COMPLETE, status.flush_state);
    INFERENCEOS_TEST_ASSERT_SIZE_EQUAL(0U, status.dirty_entries);
}

static const inferenceos_test_case block_cache_cases[] = {
    INFERENCEOS_TEST_CASE(test_cache_hit_avoids_device_io),
    INFERENCEOS_TEST_CASE(test_dirty_eviction_writes_before_replacement_read),
    INFERENCEOS_TEST_CASE(test_flush_uses_entry_order_then_device_flush),
    INFERENCEOS_TEST_CASE(test_out_of_range_miss_never_reads),
    INFERENCEOS_TEST_CASE(test_failed_eviction_retains_dirty_entry),
    INFERENCEOS_TEST_CASE(test_failed_device_flush_is_retryable)
};

const inferenceos_test_suite *inferenceos_test_suite_definition(void)
{
    static const inferenceos_test_suite suite = {
        .name = "block-cache",
        .cases = block_cache_cases,
        .case_count = INFERENCEOS_ARRAY_COUNT(block_cache_cases)
    };
    return &suite;
}
