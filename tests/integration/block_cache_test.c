#include <inferenceos/test.h>

#include <inferenceos/block.h>
#include <inferenceos/fake_block.h>

#include <string.h>

enum { TEST_CACHE_ENTRIES = 4 };

static ios_status backend_read(
    void *context, ios_u64 first_sector, ios_size sector_count, void *buffer)
{
    return fake_block_read(context, first_sector, sector_count, buffer);
}
static ios_status backend_write(
    void *context, ios_u64 first_sector, ios_size sector_count, const void *buffer)
{
    return fake_block_write(context, first_sector, sector_count, buffer);
}
static ios_status backend_flush(void *context) { return fake_block_flush(context); }

static ios_status initialize_device(
    struct ios_block_device *device, struct ios_fake_block *backend,
    enum ios_block_device_status status)
{
    const struct ios_block_device_operations operations = {
        backend_read, backend_write, backend_flush
    };
    return block_device_initialize(device, backend, &operations, IOS_BLOCK_SECTOR_SIZE,
                                   backend->sector_count, status);
}

static struct ios_block_cache_entry *entry_for(struct ios_block_cache *cache, ios_u64 sector)
{
    for (ios_size index = 0; index < cache->entry_count; ++index) {
        if (cache->entries[index].state != IOS_BLOCK_CACHE_EMPTY
            && cache->entries[index].sector == sector) return &cache->entries[index];
    }
    return NULL;
}

static void test_generic_device_reports_geometry_status_and_bounds(void)
{
    struct ios_fault_injector faults;
    struct ios_fake_block backend;
    struct ios_block_device device;
    ios_u8 bytes[IOS_BLOCK_SECTOR_SIZE] = { 0 };
    fault_injector_initialize(&faults);
    IOS_TEST_ASSERT_STATUS(fake_block_initialize(&backend, 100, 4, &faults), IOS_OK);
    IOS_TEST_ASSERT_STATUS(initialize_device(&device, &backend, IOS_BLOCK_DEVICE_READY), IOS_OK);
    IOS_TEST_ASSERT(block_device_get_status(&device) == IOS_BLOCK_DEVICE_READY);
    IOS_TEST_ASSERT(block_device_capacity_bytes(&device) == 100 * IOS_BLOCK_SECTOR_SIZE);
    IOS_TEST_ASSERT_STATUS(block_device_read(&device, 100, 1, bytes), IOS_ERROR(IOS_E_OUT_OF_RANGE));
    IOS_TEST_ASSERT_STATUS(block_device_write(&device, 99, 2, bytes), IOS_ERROR(IOS_E_OUT_OF_RANGE));
    IOS_TEST_ASSERT_STATUS(initialize_device(&device, &backend, IOS_BLOCK_DEVICE_READ_ONLY), IOS_OK);
    IOS_TEST_ASSERT_STATUS(block_device_write(&device, 0, 1, bytes), IOS_ERROR(IOS_E_READ_ONLY));
    IOS_TEST_ASSERT_STATUS(block_device_read(&device, 0, 1, bytes), IOS_OK);
    fake_block_destroy(&backend);
}

static void test_cache_defers_writes_and_barrier_respects_generation(void)
{
    struct ios_fault_injector faults;
    struct ios_fake_block backend;
    struct ios_block_device device;
    struct ios_block_cache cache;
    struct ios_block_cache_entry entries[TEST_CACHE_ENTRIES];
    ios_u8 first[IOS_BLOCK_SECTOR_SIZE];
    ios_u8 second[IOS_BLOCK_SECTOR_SIZE];
    ios_u8 observed[IOS_BLOCK_SECTOR_SIZE];
    ios_u64 generation;
    memset(first, 0x31, sizeof(first));
    memset(second, 0x52, sizeof(second));
    fault_injector_initialize(&faults);
    IOS_TEST_ASSERT_STATUS(fake_block_initialize(&backend, 100, 8, &faults), IOS_OK);
    IOS_TEST_ASSERT_STATUS(initialize_device(&device, &backend, IOS_BLOCK_DEVICE_READY), IOS_OK);
    IOS_TEST_ASSERT_STATUS(block_cache_initialize(&cache, &device, entries, TEST_CACHE_ENTRIES), IOS_OK);
    IOS_TEST_ASSERT_STATUS(block_cache_write(&cache, 10, first, 1), IOS_OK);
    IOS_TEST_ASSERT(backend.write_operations == 0);
    IOS_TEST_ASSERT_STATUS(block_cache_advance_generation(&cache, &generation), IOS_OK);
    IOS_TEST_ASSERT(generation == 2);
    IOS_TEST_ASSERT_STATUS(block_cache_write(&cache, 11, second, generation), IOS_OK);
    IOS_TEST_ASSERT_STATUS(block_cache_barrier(&cache, 1), IOS_OK);
    IOS_TEST_ASSERT(backend.write_operations == 1 && backend.flush_operations == 1);
    IOS_TEST_ASSERT(entry_for(&cache, 10)->state == IOS_BLOCK_CACHE_CLEAN);
    IOS_TEST_ASSERT(entry_for(&cache, 11)->state == IOS_BLOCK_CACHE_DIRTY);
    IOS_TEST_ASSERT_STATUS(fake_block_read(&backend, 10, 1, observed), IOS_OK);
    IOS_TEST_ASSERT(memcmp(observed, first, sizeof(first)) == 0);
    IOS_TEST_ASSERT_STATUS(block_cache_read(&cache, 11, observed), IOS_OK);
    IOS_TEST_ASSERT(memcmp(observed, second, sizeof(second)) == 0);
    IOS_TEST_ASSERT_STATUS(block_cache_barrier(&cache, 2), IOS_OK);
    IOS_TEST_ASSERT(entry_for(&cache, 11)->state == IOS_BLOCK_CACHE_CLEAN);
    fake_block_destroy(&backend);
}

static void test_failed_write_or_flush_remains_dirty_and_retryable(void)
{
    struct ios_fault_injector faults;
    struct ios_fake_block backend;
    struct ios_block_device device;
    struct ios_block_cache cache;
    struct ios_block_cache_entry entries[TEST_CACHE_ENTRIES];
    ios_u8 bytes[IOS_BLOCK_SECTOR_SIZE] = { 0xa5 };
    fault_injector_initialize(&faults);
    IOS_TEST_ASSERT_STATUS(fake_block_initialize(&backend, 100, 8, &faults), IOS_OK);
    IOS_TEST_ASSERT_STATUS(initialize_device(&device, &backend, IOS_BLOCK_DEVICE_READY), IOS_OK);
    IOS_TEST_ASSERT_STATUS(block_cache_initialize(&cache, &device, entries, TEST_CACHE_ENTRIES), IOS_OK);
    IOS_TEST_ASSERT_STATUS(block_cache_write(&cache, 20, bytes, 1), IOS_OK);
    IOS_TEST_ASSERT_STATUS(fault_injector_fail_once(
        &faults, IOS_FAULT_BLOCK_WRITE, 1, IOS_ERROR(IOS_E_IO)), IOS_OK);
    IOS_TEST_ASSERT_STATUS(block_cache_barrier(&cache, 1), IOS_ERROR(IOS_E_IO));
    IOS_TEST_ASSERT(entry_for(&cache, 20)->state == IOS_BLOCK_CACHE_ERROR);
    IOS_TEST_ASSERT(backend.flush_operations == 0);
    IOS_TEST_ASSERT_STATUS(fault_injector_fail_once(
        &faults, IOS_FAULT_BLOCK_FLUSH, 1, IOS_ERROR(IOS_E_IO)), IOS_OK);
    IOS_TEST_ASSERT_STATUS(block_cache_barrier(&cache, 1), IOS_ERROR(IOS_E_IO));
    IOS_TEST_ASSERT(entry_for(&cache, 20)->state == IOS_BLOCK_CACHE_ERROR);
    IOS_TEST_ASSERT_STATUS(block_cache_barrier(&cache, 1), IOS_OK);
    IOS_TEST_ASSERT(entry_for(&cache, 20)->state == IOS_BLOCK_CACHE_CLEAN);
    IOS_TEST_ASSERT(backend.durable_generation == 1);
    fake_block_destroy(&backend);
}

static void test_pinning_prevents_eviction_until_unpinned(void)
{
    struct ios_fault_injector faults;
    struct ios_fake_block backend;
    struct ios_block_device device;
    struct ios_block_cache cache;
    struct ios_block_cache_entry entry;
    ios_u8 bytes[IOS_BLOCK_SECTOR_SIZE];
    fault_injector_initialize(&faults);
    IOS_TEST_ASSERT_STATUS(fake_block_initialize(&backend, 100, 4, &faults), IOS_OK);
    IOS_TEST_ASSERT_STATUS(initialize_device(&device, &backend, IOS_BLOCK_DEVICE_READY), IOS_OK);
    IOS_TEST_ASSERT_STATUS(block_cache_initialize(&cache, &device, &entry, 1), IOS_OK);
    IOS_TEST_ASSERT_STATUS(block_cache_pin(&cache, 1), IOS_OK);
    IOS_TEST_ASSERT_STATUS(block_cache_read(&cache, 2, bytes), IOS_ERROR(IOS_E_WOULD_BLOCK));
    IOS_TEST_ASSERT_STATUS(block_cache_unpin(&cache, 1), IOS_OK);
    IOS_TEST_ASSERT_STATUS(block_cache_read(&cache, 2, bytes), IOS_OK);
    IOS_TEST_ASSERT(entry.sector == 2 && entry.state == IOS_BLOCK_CACHE_CLEAN);
    fake_block_destroy(&backend);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_generic_device_reports_geometry_status_and_bounds),
    IOS_TEST_CASE(test_cache_defers_writes_and_barrier_respects_generation),
    IOS_TEST_CASE(test_failed_write_or_flush_remains_dirty_and_retryable),
    IOS_TEST_CASE(test_pinning_prevents_eviction_until_unpinned)
};
const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
