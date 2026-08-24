#include <inferenceos/test.h>

#include <inferenceos/arch/interrupts.h>
#include <inferenceos/handle_table.h>

#include <string.h>

struct diagnostic_authority {
    ios_u64 owner_process_id;
    ios_u64 owner_application_identity;
    ios_u32 scope;
};

ios_u64 x86_64_interrupt_save_disable(void) { return 0; }
void x86_64_interrupt_restore(ios_u64 flags) { (void)flags; }

static void initialize_table(struct ios_handle_table *table, ios_u64 process_id)
{
    IOS_TEST_ASSERT_STATUS(handle_table_initialize(table, process_id), IOS_OK);
}

static ios_status resolve_diagnostic(
    const struct ios_handle_table *table,
    ios_handle handle,
    const struct diagnostic_authority **authority)
{
    void *object = NULL;
    ios_status status;
    if (authority == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    *authority = NULL;
    status = handle_table_resolve(
        table, handle, IOS_OBJECT_DIAGNOSTIC_CAPABILITY,
        IOS_RIGHT_DIAGNOSTIC, &object);
    if (IOS_FAILED(status)) return status;
    *authority = object;
    return IOS_OK;
}

static void test_diagnostic_authority_requires_dedicated_kind_and_right(void)
{
    struct ios_handle_table table;
    struct diagnostic_authority authority = {
        .owner_process_id = 10, .owner_application_identity = 0x435549, .scope = 1
    };
    ios_u8 content = 0;
    ios_handle diagnostic_handle;
    ios_handle content_handle;
    const struct diagnostic_authority *resolved = NULL;
    void *object = NULL;

    initialize_table(&table, authority.owner_process_id);
    IOS_TEST_ASSERT_STATUS(handle_table_insert(
        &table, &authority, IOS_OBJECT_DIAGNOSTIC_CAPABILITY,
        IOS_RIGHT_DIAGNOSTIC, NULL, NULL, &diagnostic_handle), IOS_OK);
    IOS_TEST_ASSERT_STATUS(handle_table_insert(
        &table, &content, IOS_OBJECT_CONTENT, IOS_RIGHT_READ,
        NULL, NULL, &content_handle), IOS_OK);
    IOS_TEST_ASSERT_STATUS(resolve_diagnostic(
        &table, diagnostic_handle, &resolved), IOS_OK);
    IOS_TEST_ASSERT(resolved == &authority);
    IOS_TEST_ASSERT_STATUS(resolve_diagnostic(
        &table, content_handle, &resolved), IOS_ERROR(IOS_E_WRONG_HANDLE_TYPE));
    IOS_TEST_ASSERT_STATUS(handle_table_resolve(
        &table, diagnostic_handle, IOS_OBJECT_CONTENT, IOS_RIGHT_READ, &object),
        IOS_ERROR(IOS_E_WRONG_HANDLE_TYPE));
}

static void test_underprivileged_diagnostic_object_grants_no_authority(void)
{
    struct ios_handle_table table;
    struct diagnostic_authority authority = { .owner_process_id = 20, .scope = 1 };
    ios_handle handle;
    const struct diagnostic_authority *resolved = NULL;

    initialize_table(&table, authority.owner_process_id);
    IOS_TEST_ASSERT_STATUS(handle_table_insert(
        &table, &authority, IOS_OBJECT_DIAGNOSTIC_CAPABILITY,
        IOS_RIGHT_READ, NULL, NULL, &handle), IOS_OK);
    IOS_TEST_ASSERT_STATUS(resolve_diagnostic(
        &table, handle, &resolved), IOS_ERROR(IOS_E_ACCESS_DENIED));
    IOS_TEST_ASSERT(resolved == NULL);
}

static void test_diagnostic_capability_is_process_local_unforgeable_and_stale_safe(void)
{
    struct ios_handle_table owner;
    struct ios_handle_table other;
    struct diagnostic_authority authority = { .owner_process_id = 30, .scope = 1 };
    ios_handle handle;
    const struct diagnostic_authority *resolved = NULL;

    initialize_table(&owner, authority.owner_process_id);
    initialize_table(&other, 31);
    IOS_TEST_ASSERT_STATUS(handle_table_insert(
        &owner, &authority, IOS_OBJECT_DIAGNOSTIC_CAPABILITY,
        IOS_RIGHT_DIAGNOSTIC, NULL, NULL, &handle), IOS_OK);
    IOS_TEST_ASSERT_STATUS(resolve_diagnostic(
        &other, handle, &resolved), IOS_ERROR(IOS_E_BAD_HANDLE));
    IOS_TEST_ASSERT_STATUS(resolve_diagnostic(
        &owner, handle ^ (UINT64_C(1) << 24), &resolved), IOS_ERROR(IOS_E_BAD_HANDLE));
    IOS_TEST_ASSERT_STATUS(handle_table_close(&owner, handle), IOS_OK);
    IOS_TEST_ASSERT_STATUS(resolve_diagnostic(
        &owner, handle, &resolved), IOS_ERROR(IOS_E_BAD_HANDLE));
}

static void test_diagnostic_capability_cannot_be_delegated_implicitly(void)
{
    struct ios_handle_table owner;
    struct ios_handle_table other;
    struct diagnostic_authority authority = { .owner_process_id = 40, .scope = 1 };
    ios_handle handle;
    ios_handle delegated = IOS_INVALID_HANDLE;

    initialize_table(&owner, authority.owner_process_id);
    initialize_table(&other, 41);
    IOS_TEST_ASSERT_STATUS(handle_table_insert(
        &owner, &authority, IOS_OBJECT_DIAGNOSTIC_CAPABILITY,
        IOS_RIGHT_DIAGNOSTIC, NULL, NULL, &handle), IOS_OK);
    IOS_TEST_ASSERT_STATUS(handle_table_duplicate(
        &owner, handle, IOS_RIGHT_DIAGNOSTIC, &delegated),
        IOS_ERROR(IOS_E_ACCESS_DENIED));
    IOS_TEST_ASSERT_STATUS(handle_table_transfer(
        &owner, &other, handle, IOS_RIGHT_DIAGNOSTIC, false, &delegated),
        IOS_ERROR(IOS_E_ACCESS_DENIED));
    IOS_TEST_ASSERT(delegated == IOS_INVALID_HANDLE);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_diagnostic_authority_requires_dedicated_kind_and_right),
    IOS_TEST_CASE(test_underprivileged_diagnostic_object_grants_no_authority),
    IOS_TEST_CASE(test_diagnostic_capability_is_process_local_unforgeable_and_stale_safe),
    IOS_TEST_CASE(test_diagnostic_capability_cannot_be_delegated_implicitly)
};

const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
