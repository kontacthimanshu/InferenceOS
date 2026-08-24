#include <inferenceos/fault_injector.h>
#include <inferenceos/fs/transaction.h>
#include <inferenceos/test.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define COMMIT_ARRAY_COUNT(values) (sizeof(values) / sizeof((values)[0]))

enum {
    IMAGE_SIZE = 2048,
    COMPANION_OFFSET = 0,
    PRIMARY_OFFSET = 32,
    FAT_OFFSET = 512,
    DATA_OFFSET = 1024,
    RECORD_SIZE = 32,
    CONTENT_SIZE = 15,
    TRACE_CAPACITY = 16
};

enum commit_stage {
    STAGE_UNCOMMITTED_COMPANION,
    STAGE_FENCE_FLUSH,
    STAGE_CONTENT,
    STAGE_CONTENT_FLUSH,
    STAGE_ALLOCATION,
    STAGE_ALLOCATION_FLUSH,
    STAGE_PRIMARY,
    STAGE_COMMITTED_COMPANION,
    STAGE_FINAL_FLUSH
};

enum persisted_state {
    PERSISTED_EMPTY,
    PERSISTED_INCOMPLETE,
    PERSISTED_HEALTHY
};

struct commit_image {
    uint8_t volatile_bytes[IMAGE_SIZE];
    uint8_t durable_bytes[IMAGE_SIZE];
};

struct commit_trace {
    enum commit_stage stages[TRACE_CAPACITY];
    size_t count;
};

struct commit_harness {
    struct commit_image image;
    struct ios_fault_injector faults;
    struct commit_trace trace;
    size_t barrier_count;
};

static void put_u32_le(uint8_t *bytes, uint32_t value)
{
    for (size_t index = 0; index < 4; ++index) bytes[index] = (uint8_t)(value >> (index * 8U));
}

static uint32_t get_u32_le(const uint8_t *bytes)
{
    uint32_t value = 0;
    for (size_t index = 0; index < 4; ++index) value |= (uint32_t)bytes[index] << (index * 8U);
    return value;
}

static uint32_t crc32_iso_hdlc(const uint8_t *bytes, size_t length)
{
    uint32_t crc = UINT32_C(0xffffffff);
    for (size_t index = 0; index < length; ++index) {
        crc ^= bytes[index];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? UINT32_C(0xedb88320) : 0U);
        }
    }
    return crc ^ UINT32_C(0xffffffff);
}

static void trace_stage(struct commit_trace *trace, enum commit_stage stage)
{
    if (trace->count < TRACE_CAPACITY) trace->stages[trace->count++] = stage;
}

static ios_status persist_bytes(
    struct commit_harness *harness,
    enum ios_fault_operation semantic_operation,
    enum commit_stage stage,
    size_t offset,
    const uint8_t *bytes,
    size_t length
)
{
    ios_status status;
    trace_stage(&harness->trace, stage);
    status = fault_injector_check(&harness->faults, semantic_operation);
    if (IOS_FAILED(status)) return status;
    status = fault_injector_check(&harness->faults, IOS_FAULT_BLOCK_WRITE);
    if (IOS_FAILED(status)) return status;
    memcpy(harness->image.volatile_bytes + offset, bytes, length);
    return IOS_OK;
}

static ios_status persist_barrier(struct commit_harness *harness, enum commit_stage stage)
{
    ios_status status;
    trace_stage(&harness->trace, stage);
    status = fault_injector_check(&harness->faults, IOS_FAULT_BLOCK_FLUSH);
    if (IOS_FAILED(status)) return status;
    memcpy(
        harness->image.durable_bytes,
        harness->image.volatile_bytes,
        sizeof(harness->image.durable_bytes)
    );
    return IOS_OK;
}

static ios_status transaction_persist_content(void *context, const void *bytes, ios_size length)
{
    return persist_bytes(context, IOS_FAULT_PERSIST_CONTENT, STAGE_CONTENT,
                         DATA_OFFSET, bytes, length);
}

static ios_status transaction_persist_allocation(void *context)
{
    uint8_t fat_entry[4];
    put_u32_le(fat_entry, UINT32_C(0x0fffffff));
    return persist_bytes(context, IOS_FAULT_PERSIST_ALLOCATION, STAGE_ALLOCATION,
                         FAT_OFFSET + 3 * 4, fat_entry, sizeof(fat_entry));
}

static ios_status transaction_persist_primary(
    void *context, const struct ios_fs_primary_disk *primary
)
{
    return persist_bytes(context, IOS_FAULT_PERSIST_PRIMARY, STAGE_PRIMARY,
                         PRIMARY_OFFSET, (const uint8_t *)primary, sizeof(*primary));
}

static ios_status transaction_persist_companion(
    void *context, const struct ios_fs_companion_disk *companion
)
{
    const enum commit_stage stage =
        (companion->flags & IOS_FS_COMPANION_FLAG_COMMITTED) != 0
            ? STAGE_COMMITTED_COMPANION : STAGE_UNCOMMITTED_COMPANION;
    return persist_bytes(context, IOS_FAULT_PERSIST_COMPANION, stage,
                         COMPANION_OFFSET, (const uint8_t *)companion, sizeof(*companion));
}

static ios_status transaction_unused_deleted_pair(void *context)
{
    (void)context;
    return IOS_ERROR(IOS_E_NOT_SUPPORTED);
}

static ios_status transaction_unused_release(void *context, ios_u32 first_cluster)
{
    (void)context;
    (void)first_cluster;
    return IOS_ERROR(IOS_E_NOT_SUPPORTED);
}

static ios_status transaction_barrier(void *context)
{
    static const enum commit_stage stages[] = {
        STAGE_FENCE_FLUSH, STAGE_CONTENT_FLUSH, STAGE_ALLOCATION_FLUSH, STAGE_FINAL_FLUSH
    };
    struct commit_harness *harness = context;
    if (harness->barrier_count >= COMMIT_ARRAY_COUNT(stages)) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    return persist_barrier(harness, stages[harness->barrier_count++]);
}

static ios_status durable_create_report(struct commit_harness *harness)
{
    static const uint8_t content[CONTENT_SIZE] = {
        'p', 'e', 'r', 's', 'i', 's', 't', 'e', 'n', 't', ' ', 'd', 'a', 't', 'a'
    };
    static const struct ios_fs_transaction_operations operations = {
        transaction_persist_content, transaction_persist_allocation,
        transaction_persist_primary, transaction_persist_companion,
        transaction_unused_deleted_pair, transaction_unused_release, transaction_barrier
    };
    struct ios_fs_primary primary = {
        { 'R', 'E', 'P', 'O', 'R', 'T', ' ', ' ', 'T', 'X', 'T' },
        IOS_FS_ATTRIBUTE_REGULAR, 3, CONTENT_SIZE
    };
    struct ios_fs_transaction transaction;
    ios_status status = ios_fs_transaction_initialize(
        &transaction, harness, &operations, true
    );
    return IOS_FAILED(status) ? status
                              : ios_fs_transaction_create(
                                    &transaction, &primary, content, sizeof(content)
                                );
}

static bool all_zero(const uint8_t *bytes, size_t length)
{
    for (size_t index = 0; index < length; ++index) {
        if (bytes[index] != 0) return false;
    }
    return true;
}

static bool companion_crc_valid(const uint8_t record[RECORD_SIZE])
{
    uint8_t copy[RECORD_SIZE];
    const uint32_t stored = get_u32_le(record + 0x10);
    memcpy(copy, record, sizeof(copy));
    memset(copy + 0x10, 0, 4);
    return crc32_iso_hdlc(copy, sizeof(copy)) == stored;
}

static enum persisted_state inspect_durable_image(const struct commit_image *image)
{
    static const uint8_t name[11] = {
        'R', 'E', 'P', 'O', 'R', 'T', ' ', ' ', 'T', 'X', 'T'
    };
    static const uint8_t hash_text[8] = { 'E', '7', '7', '1', 'F', '0', '4', 'F' };
    static const uint8_t content[CONTENT_SIZE] = {
        'p', 'e', 'r', 's', 'i', 's', 't', 'e', 'n', 't', ' ', 'd', 'a', 't', 'a'
    };
    const uint8_t *companion = image->durable_bytes + COMPANION_OFFSET;
    const uint8_t *primary = image->durable_bytes + PRIMARY_OFFSET;

    if (all_zero(companion, RECORD_SIZE) && all_zero(primary, RECORD_SIZE)) {
        return PERSISTED_EMPTY;
    }
    if (*companion != 0xf1 || companion[1] != 1 || companion[2] != 1
        || companion[3] != 1 || companion[4] != 3 || companion[5] != 0xa3
        || memcmp(companion + 8, hash_text, sizeof(hash_text)) != 0
        || !companion_crc_valid(companion)
        || memcmp(primary, name, sizeof(name)) != 0 || primary[0x0b] != 0x20
        || primary[0x1a] != 3 || get_u32_le(primary + 0x1c) != CONTENT_SIZE
        || get_u32_le(image->durable_bytes + FAT_OFFSET + 3 * 4) != UINT32_C(0x0fffffff)
        || memcmp(image->durable_bytes + DATA_OFFSET, content, sizeof(content)) != 0) {
        return PERSISTED_INCOMPLETE;
    }
    return PERSISTED_HEALTHY;
}

static void initialize_harness(struct commit_harness *harness)
{
    memset(harness, 0, sizeof(*harness));
    fault_injector_initialize(&harness->faults);
}

static void test_success_uses_exact_order_and_persists_inspectable_pair(void)
{
    static const enum commit_stage expected[] = {
        STAGE_UNCOMMITTED_COMPANION, STAGE_FENCE_FLUSH,
        STAGE_CONTENT, STAGE_CONTENT_FLUSH,
        STAGE_ALLOCATION, STAGE_ALLOCATION_FLUSH,
        STAGE_PRIMARY, STAGE_COMMITTED_COMPANION, STAGE_FINAL_FLUSH
    };
    struct commit_harness harness;
    initialize_harness(&harness);
    IOS_TEST_ASSERT_STATUS(durable_create_report(&harness), IOS_OK);
    IOS_TEST_ASSERT(harness.trace.count == COMMIT_ARRAY_COUNT(expected));
    IOS_TEST_ASSERT(memcmp(harness.trace.stages, expected, sizeof(expected)) == 0);
    IOS_TEST_ASSERT(inspect_durable_image(&harness.image) == PERSISTED_HEALTHY);
    IOS_TEST_ASSERT(
        harness.trace.stages[6] == STAGE_PRIMARY
        && harness.trace.stages[7] == STAGE_COMMITTED_COMPANION
    );
}

static void test_every_semantic_persistence_failure_is_safe_and_reported(void)
{
    static const struct {
        enum ios_fault_operation operation;
        uint64_t occurrence;
    } failures[] = {
        { IOS_FAULT_PERSIST_COMPANION, 1 },
        { IOS_FAULT_PERSIST_CONTENT, 1 },
        { IOS_FAULT_PERSIST_ALLOCATION, 1 },
        { IOS_FAULT_PERSIST_PRIMARY, 1 },
        { IOS_FAULT_PERSIST_COMPANION, 2 }
    };
    for (size_t index = 0; index < COMMIT_ARRAY_COUNT(failures); ++index) {
        struct commit_harness harness;
        ios_status status;
        initialize_harness(&harness);
        IOS_TEST_ASSERT_STATUS(fault_injector_fail_once(
            &harness.faults, failures[index].operation, failures[index].occurrence,
            IOS_ERROR(IOS_E_IO)
        ), IOS_OK);
        status = durable_create_report(&harness);
        IOS_TEST_ASSERT_STATUS(status, IOS_ERROR(IOS_E_IO));
        IOS_TEST_ASSERT(inspect_durable_image(&harness.image) != PERSISTED_HEALTHY);
    }
}

static void test_every_physical_write_failure_is_safe_and_reported(void)
{
    for (uint64_t occurrence = 1; occurrence <= 5; ++occurrence) {
        struct commit_harness harness;
        initialize_harness(&harness);
        IOS_TEST_ASSERT_STATUS(fault_injector_fail_once(
            &harness.faults, IOS_FAULT_BLOCK_WRITE, occurrence, IOS_ERROR(IOS_E_IO)
        ), IOS_OK);
        IOS_TEST_ASSERT_STATUS(durable_create_report(&harness), IOS_ERROR(IOS_E_IO));
        IOS_TEST_ASSERT(inspect_durable_image(&harness.image) != PERSISTED_HEALTHY);
    }
}

static void test_every_flush_failure_prevents_false_durable_success(void)
{
    for (uint64_t occurrence = 1; occurrence <= 4; ++occurrence) {
        struct commit_harness harness;
        initialize_harness(&harness);
        IOS_TEST_ASSERT_STATUS(fault_injector_fail_once(
            &harness.faults, IOS_FAULT_BLOCK_FLUSH, occurrence, IOS_ERROR(IOS_E_IO)
        ), IOS_OK);
        IOS_TEST_ASSERT_STATUS(durable_create_report(&harness), IOS_ERROR(IOS_E_IO));
        IOS_TEST_ASSERT(inspect_durable_image(&harness.image) != PERSISTED_HEALTHY);
    }
}

static void test_reboot_discards_unflushed_primary_and_committed_companion(void)
{
    struct commit_harness harness;
    initialize_harness(&harness);
    IOS_TEST_ASSERT_STATUS(fault_injector_fail_once(
        &harness.faults, IOS_FAULT_BLOCK_FLUSH, 4, IOS_ERROR(IOS_E_IO)
    ), IOS_OK);
    IOS_TEST_ASSERT_STATUS(durable_create_report(&harness), IOS_ERROR(IOS_E_IO));
    IOS_TEST_ASSERT(inspect_durable_image(&harness.image) == PERSISTED_INCOMPLETE);
    memset(harness.image.volatile_bytes, 0, sizeof(harness.image.volatile_bytes));
    memcpy(
        harness.image.volatile_bytes,
        harness.image.durable_bytes,
        sizeof(harness.image.volatile_bytes)
    );
    IOS_TEST_ASSERT(inspect_durable_image(&harness.image) == PERSISTED_INCOMPLETE);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_success_uses_exact_order_and_persists_inspectable_pair),
    IOS_TEST_CASE(test_every_semantic_persistence_failure_is_safe_and_reported),
    IOS_TEST_CASE(test_every_physical_write_failure_is_safe_and_reported),
    IOS_TEST_CASE(test_every_flush_failure_prevents_false_durable_success),
    IOS_TEST_CASE(test_reboot_discards_unflushed_primary_and_committed_companion)
};
const size_t ios_test_case_count = COMMIT_ARRAY_COUNT(ios_test_cases);
