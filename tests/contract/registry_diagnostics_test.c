#include <inferenceos/test.h>

#include <inferenceos/arch/interrupts.h>
#include <inferenceos/fs/registry_diagnostics.h>

#include <string.h>

ios_u64 x86_64_interrupt_save_disable(void) { return 0; }
void x86_64_interrupt_restore(ios_u64 flags) { (void)flags; }

static void make_registry_pair(
    struct ios_fs_companion_disk *companion,
    struct ios_fs_primary_disk *primary
)
{
    const struct ios_fs_primary value = {
        { 'R', 'E', 'P', 'O', 'R', 'T', ' ', ' ', 'T', 'X', 'T' },
        IOS_FS_ATTRIBUTE_REGULAR, 2, 64
    };
    IOS_TEST_ASSERT_STATUS(ios_fs_primary_encode(&value, primary), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_fs_companion_encode(value.name, true, companion), IOS_OK);
}

static void initialize_fixture(
    struct ios_process *process,
    struct ios_fs_registry *registry,
    struct ios_fs_registry_record_disk storage[4],
    struct ios_fs_registry_diagnostic_service *service,
    struct ios_fs_diagnostic_authority *authority,
    ios_handle *authority_handle
)
{
    struct ios_fs_companion_disk companion;
    struct ios_fs_primary_disk primary;
    ios_size record_index;

    memset(process, 0, sizeof(*process));
    memset(storage, 0, sizeof(*storage) * 4);
    process->process_id = 17;
    process->application_identity = UINT64_C(0x435549);
    process->state = IOS_PROCESS_RUNNABLE;
    IOS_TEST_ASSERT_STATUS(handle_table_initialize(&process->handles, process->process_id), IOS_OK);
    *authority = (struct ios_fs_diagnostic_authority){
        process->process_id,
        process->application_identity,
        IOS_FS_DIAGNOSTIC_SCOPE_REGISTRY
    };
    IOS_TEST_ASSERT_STATUS(
        handle_table_insert(
            &process->handles, authority, IOS_OBJECT_DIAGNOSTIC_CAPABILITY,
            IOS_RIGHT_DIAGNOSTIC, NULL, NULL, authority_handle
        ),
        IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_initialize(registry, storage, 4, true), IOS_OK
    );
    make_registry_pair(&companion, &primary);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_refresh(registry, &companion, &primary, 8, 10, &record_index), IOS_OK
    );
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_diagnostic_service_initialize(service, registry), IOS_OK
    );
}

static struct ios_fs_registry_diagnostic_request request_for(ios_handle authority)
{
    const struct ios_fs_registry_diagnostic_request request = {
        sizeof(struct ios_fs_registry_diagnostic_request),
        IOS_FS_REGISTRY_DIAGNOSTIC_ABI_VERSION,
        authority,
        0,
        IOS_FS_REGISTRY_DIAGNOSTIC_REPLY_CAPACITY,
        0
    };
    return request;
}

static struct ios_fs_registry_control_request control_request_for(
    ios_handle authority,
    bool enabled
)
{
    const struct ios_fs_registry_control_request request = {
        .size = sizeof(struct ios_fs_registry_control_request),
        .version = IOS_FS_REGISTRY_DIAGNOSTIC_ABI_VERSION,
        .authority = authority,
        .enabled = enabled
    };
    return request;
}

static void test_authorized_diagnostic_reports_validated_entries_without_mutation(void)
{
    static const ios_u8 expected_hash[IOS_FS_HASH_TEXT_SIZE] = {
        'E', '7', '7', '1', 'F', '0', '4', 'F'
    };
    struct ios_process process;
    struct ios_fs_registry registry;
    struct ios_fs_registry_record_disk storage[4];
    struct ios_fs_registry_record_disk before[4];
    struct ios_fs_registry_diagnostic_service service;
    struct ios_fs_diagnostic_authority authority;
    struct ios_fs_registry_diagnostic_reply reply;
    ios_handle authority_handle;

    initialize_fixture(
        &process, &registry, storage, &service, &authority, &authority_handle
    );
    memcpy(before, storage, sizeof(before));
    const struct ios_fs_registry_diagnostic_request request = request_for(authority_handle);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_diagnostic_dispatch(&service, &process, &request, &reply), IOS_OK
    );
    IOS_TEST_ASSERT(reply.enabled);
    IOS_TEST_ASSERT(reply.health == IOS_FS_REGISTRY_HEALTHY);
    IOS_TEST_ASSERT(reply.active_type_count == 1);
    IOS_TEST_ASSERT(reply.record_count == 1);
    IOS_TEST_ASSERT(reply.records[0].record_index == 0);
    IOS_TEST_ASSERT(reply.records[0].validation_status == IOS_OK);
    IOS_TEST_ASSERT(reply.records[0].active);
    IOS_TEST_ASSERT(reply.records[0].extension_length == 3);
    IOS_TEST_ASSERT(memcmp(reply.records[0].canonical_extension, "TXT", 3) == 0);
    IOS_TEST_ASSERT(memcmp(
        reply.records[0].extension_hash_text, expected_hash, sizeof(expected_hash)
    ) == 0);
    IOS_TEST_ASSERT(reply.records[0].last_directory_cluster == 8);
    IOS_TEST_ASSERT(reply.records[0].last_directory_slot == 10);
    IOS_TEST_ASSERT(memcmp(before, storage, sizeof(before)) == 0);
    IOS_TEST_ASSERT(ios_fs_registry_active_count(&registry) == 1);
}

static void test_diagnostic_reports_corruption_but_does_not_repair_or_disable(void)
{
    struct ios_process process;
    struct ios_fs_registry registry;
    struct ios_fs_registry_record_disk storage[4];
    struct ios_fs_registry_record_disk before[4];
    struct ios_fs_registry_diagnostic_service service;
    struct ios_fs_diagnostic_authority authority;
    struct ios_fs_registry_diagnostic_reply reply;
    ios_handle authority_handle;

    initialize_fixture(
        &process, &registry, storage, &service, &authority, &authority_handle
    );
    storage[0].crc32[0] ^= 1;
    memcpy(before, storage, sizeof(before));
    const struct ios_fs_registry_diagnostic_request request = request_for(authority_handle);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_diagnostic_dispatch(&service, &process, &request, &reply), IOS_OK
    );
    IOS_TEST_ASSERT(reply.enabled);
    IOS_TEST_ASSERT(reply.health == IOS_FS_REGISTRY_CORRUPT);
    IOS_TEST_ASSERT(reply.active_type_count == 0);
    IOS_TEST_ASSERT(reply.record_count == 1);
    IOS_TEST_ASSERT(reply.records[0].validation_status == IOS_ERROR(IOS_E_CORRUPT));
    IOS_TEST_ASSERT(memcmp(before, storage, sizeof(before)) == 0);
    IOS_TEST_ASSERT(ios_fs_registry_health(&registry) == IOS_FS_REGISTRY_HEALTHY);
}

static void test_missing_or_underprivileged_authority_returns_no_registry_payload(void)
{
    struct ios_process process;
    struct ios_fs_registry registry;
    struct ios_fs_registry_record_disk storage[4];
    struct ios_fs_registry_record_disk before[4];
    struct ios_fs_registry_diagnostic_service service;
    struct ios_fs_diagnostic_authority authority;
    struct ios_fs_registry_diagnostic_reply reply;
    struct ios_fs_registry_diagnostic_reply zero;
    ios_handle authority_handle;

    initialize_fixture(
        &process, &registry, storage, &service, &authority, &authority_handle
    );
    memcpy(before, storage, sizeof(before));
    memset(&zero, 0, sizeof(zero));
    struct ios_fs_registry_diagnostic_request request = request_for(IOS_INVALID_HANDLE);
    memset(&reply, 0xa5, sizeof(reply));
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_diagnostic_dispatch(&service, &process, &request, &reply),
        IOS_ERROR(IOS_E_BAD_HANDLE)
    );
    IOS_TEST_ASSERT(memcmp(&reply, &zero, sizeof(reply)) == 0);

    authority.scope = IOS_FS_DIAGNOSTIC_SCOPE_FILESYSTEM;
    request = request_for(authority_handle);
    memset(&reply, 0xa5, sizeof(reply));
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_diagnostic_dispatch(&service, &process, &request, &reply),
        IOS_ERROR(IOS_E_ACCESS_DENIED)
    );
    IOS_TEST_ASSERT(memcmp(&reply, &zero, sizeof(reply)) == 0);
    IOS_TEST_ASSERT(memcmp(before, storage, sizeof(before)) == 0);
}

static void test_authorized_runtime_control_changes_mode_without_rewriting_records(void)
{
    struct ios_process process;
    struct ios_fs_registry registry;
    struct ios_fs_registry_record_disk storage[4];
    struct ios_fs_registry_record_disk before[4];
    struct ios_fs_registry_diagnostic_service service;
    struct ios_fs_diagnostic_authority authority;
    struct ios_fs_registry_control_reply reply;
    ios_handle authority_handle;

    initialize_fixture(
        &process, &registry, storage, &service, &authority, &authority_handle
    );
    memcpy(before, storage, sizeof(before));
    struct ios_fs_registry_control_request request = control_request_for(
        authority_handle, false
    );
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_control_dispatch(&service, &process, &request, &reply), IOS_OK
    );
    IOS_TEST_ASSERT(!reply.enabled);
    IOS_TEST_ASSERT(reply.health == IOS_FS_REGISTRY_DISABLED);
    IOS_TEST_ASSERT(!ios_fs_registry_enabled(&registry));
    IOS_TEST_ASSERT(memcmp(before, storage, sizeof(before)) == 0);

    request = control_request_for(authority_handle, true);
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_control_dispatch(&service, &process, &request, &reply), IOS_OK
    );
    IOS_TEST_ASSERT(reply.enabled);
    IOS_TEST_ASSERT(reply.health == IOS_FS_REGISTRY_HEALTHY);
    IOS_TEST_ASSERT(ios_fs_registry_enabled(&registry));
    IOS_TEST_ASSERT(ios_fs_registry_active_count(&registry) == 1);
    IOS_TEST_ASSERT(memcmp(before, storage, sizeof(before)) == 0);
}

static void test_runtime_control_requires_registry_diagnostic_authority(void)
{
    struct ios_process process;
    struct ios_fs_registry registry;
    struct ios_fs_registry_record_disk storage[4];
    struct ios_fs_registry_record_disk before[4];
    struct ios_fs_registry_diagnostic_service service;
    struct ios_fs_diagnostic_authority authority;
    struct ios_fs_registry_control_reply reply;
    struct ios_fs_registry_control_reply zero;
    ios_handle authority_handle;

    initialize_fixture(
        &process, &registry, storage, &service, &authority, &authority_handle
    );
    memcpy(before, storage, sizeof(before));
    memset(&zero, 0, sizeof(zero));
    struct ios_fs_registry_control_request request = control_request_for(
        IOS_INVALID_HANDLE, false
    );
    memset(&reply, 0xa5, sizeof(reply));
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_control_dispatch(&service, &process, &request, &reply),
        IOS_ERROR(IOS_E_BAD_HANDLE)
    );
    IOS_TEST_ASSERT(memcmp(&reply, &zero, sizeof(reply)) == 0);
    IOS_TEST_ASSERT(ios_fs_registry_enabled(&registry));
    IOS_TEST_ASSERT(memcmp(before, storage, sizeof(before)) == 0);

    request = control_request_for(authority_handle, true);
    request.enabled = 2;
    memset(&reply, 0xa5, sizeof(reply));
    IOS_TEST_ASSERT_STATUS(
        ios_fs_registry_control_dispatch(&service, &process, &request, &reply),
        IOS_ERROR(IOS_E_INVALID_ARGUMENT)
    );
    IOS_TEST_ASSERT(memcmp(&reply, &zero, sizeof(reply)) == 0);
    IOS_TEST_ASSERT(ios_fs_registry_enabled(&registry));
    IOS_TEST_ASSERT(memcmp(before, storage, sizeof(before)) == 0);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_authorized_diagnostic_reports_validated_entries_without_mutation),
    IOS_TEST_CASE(test_diagnostic_reports_corruption_but_does_not_repair_or_disable),
    IOS_TEST_CASE(test_missing_or_underprivileged_authority_returns_no_registry_payload),
    IOS_TEST_CASE(test_authorized_runtime_control_changes_mode_without_rewriting_records),
    IOS_TEST_CASE(test_runtime_control_requires_registry_diagnostic_authority)
};

const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
