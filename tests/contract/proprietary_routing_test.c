#include <inferenceos/test.h>

#include <inferenceos/arch/interrupts.h>
#include <inferenceos/application_bindings.h>
#include <inferenceos/handle_table.h>
#include <inferenceos/type_capability.h>
#include <inferenceos/type_catalog.h>

enum {
    APPLICATION_REPORT_VIEWER = 0x52505456,
    APPLICATION_IMAGE_VIEWER = 0x494d4756,
    TYPE_TEXT_DOCUMENT = 0x545854,
    TYPE_IMAGE_DOCUMENT = 0x494d47
};

ios_u64 x86_64_interrupt_save_disable(void)
{
    return 0;
}

void x86_64_interrupt_restore(ios_u64 previous_flags)
{
    (void)previous_flags;
}

static ios_status authorize_type_capability(
    const struct ios_application_binding_registry *bindings,
    const struct ios_type_catalog *catalog,
    ios_u64 application_identity,
    ios_type_icon_capability catalog_capability,
    ios_u64 *internal_type_identity)
{
    ios_u64 resolved_identity;
    ios_status status;

    if (internal_type_identity == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    *internal_type_identity = 0;
    status = ios_type_catalog_resolve_identity(
        catalog, catalog_capability, &resolved_identity);
    if (IOS_FAILED(status)) return status;
    status = ios_application_bindings_authorize(
        bindings, application_identity, resolved_identity);
    if (IOS_FAILED(status)) return status;
    *internal_type_identity = resolved_identity;
    return IOS_OK;
}

static void initialize_catalog(
    struct ios_type_catalog *catalog,
    ios_type_icon_capability *text,
    ios_type_icon_capability *image)
{
    IOS_TEST_ASSERT_STATUS(
        ios_type_catalog_initialize(catalog, UINT64_C(0xa55a0123456789ab)), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_type_catalog_register(catalog, TYPE_TEXT_DOCUMENT, IOS_ICON_TEXT, text), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_type_catalog_register(catalog, TYPE_IMAGE_DOCUMENT, IOS_ICON_IMAGE, image), IOS_OK);
}

static void initialize_bindings(struct ios_application_binding_registry *bindings)
{
    ios_application_bindings_initialize(bindings);
    IOS_TEST_ASSERT_STATUS(
        ios_application_bindings_register(
            bindings, APPLICATION_REPORT_VIEWER, TYPE_TEXT_DOCUMENT),
        IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_application_bindings_register(
            bindings, APPLICATION_IMAGE_VIEWER, TYPE_IMAGE_DOCUMENT),
        IOS_OK);
}

static void initialize_process(
    struct ios_process *process, ios_u64 process_id, ios_u64 application_identity)
{
    *process = (struct ios_process){
        .process_id = process_id,
        .application_identity = application_identity,
        .state = IOS_PROCESS_RUNNABLE
    };
    IOS_TEST_ASSERT_STATUS(handle_table_initialize(&process->handles, process_id), IOS_OK);
}

static void test_trusted_application_binding_allows_only_registered_types(void)
{
    struct ios_type_catalog catalog;
    struct ios_application_binding_registry bindings;
    ios_type_icon_capability text;
    ios_type_icon_capability image;
    ios_u64 resolved = 0;

    initialize_catalog(&catalog, &text, &image);
    initialize_bindings(&bindings);
    IOS_TEST_ASSERT_STATUS(
        authorize_type_capability(
            &bindings, &catalog, APPLICATION_REPORT_VIEWER, text, &resolved),
        IOS_OK);
    IOS_TEST_ASSERT(resolved == TYPE_TEXT_DOCUMENT);
    IOS_TEST_ASSERT_STATUS(
        authorize_type_capability(
            &bindings, &catalog, APPLICATION_REPORT_VIEWER, image, &resolved),
        IOS_ERROR(IOS_E_ACCESS_DENIED));
    IOS_TEST_ASSERT(resolved == 0);
    IOS_TEST_ASSERT_STATUS(
        authorize_type_capability(
            &bindings, &catalog, UINT64_C(0x554e545255535445), text, &resolved),
        IOS_ERROR(IOS_E_ACCESS_DENIED));
}

static void test_fabricated_catalog_capabilities_grant_no_type(void)
{
    struct ios_type_catalog catalog;
    struct ios_application_binding_registry bindings;
    struct ios_type_capability_service service;
    struct ios_process process;
    ios_type_icon_capability text;
    ios_type_icon_capability image;
    ios_handle handle = UINT64_MAX;
    ios_u64 resolved = UINT64_MAX;

    initialize_catalog(&catalog, &text, &image);
    initialize_bindings(&bindings);
    initialize_process(&process, UINT64_C(0x4004), APPLICATION_REPORT_VIEWER);
    IOS_TEST_ASSERT_STATUS(
        ios_type_capability_service_initialize(&service, &bindings, &catalog), IOS_OK);
    (void)image;
    IOS_TEST_ASSERT_STATUS(
        authorize_type_capability(
            &bindings, &catalog, APPLICATION_REPORT_VIEWER,
            text ^ (UINT64_C(1) << 32), &resolved),
        IOS_ERROR(IOS_E_BAD_HANDLE));
    IOS_TEST_ASSERT(resolved == 0);
    IOS_TEST_ASSERT_STATUS(
        authorize_type_capability(
            &bindings, &catalog, APPLICATION_REPORT_VIEWER,
            IOS_INVALID_TYPE_ICON_CAPABILITY, &resolved),
        IOS_ERROR(IOS_E_BAD_HANDLE));
    IOS_TEST_ASSERT_STATUS(
        ios_type_capability_mint(
            &service, &process, text ^ (UINT64_C(1) << 32), &handle),
        IOS_ERROR(IOS_E_BAD_HANDLE));
    IOS_TEST_ASSERT(handle == IOS_INVALID_HANDLE);
    IOS_TEST_ASSERT(ios_type_capability_active_count(&service) == 0);
}

static void test_binding_registry_is_bounded_deduplicated_and_application_scoped(void)
{
    struct ios_application_binding_registry bindings;
    ios_u64 types[2] = { UINT64_MAX, UINT64_MAX };
    ios_size type_count = 0;

    initialize_bindings(&bindings);
    IOS_TEST_ASSERT_STATUS(
        ios_application_bindings_register(
            &bindings, APPLICATION_REPORT_VIEWER, TYPE_TEXT_DOCUMENT),
        IOS_ERROR(IOS_E_ALREADY_EXISTS));
    IOS_TEST_ASSERT_STATUS(
        ios_application_bindings_register(
            &bindings, APPLICATION_REPORT_VIEWER, TYPE_IMAGE_DOCUMENT),
        IOS_OK);
    IOS_TEST_ASSERT(ios_application_bindings_count(&bindings) == 3);
    IOS_TEST_ASSERT_STATUS(
        ios_application_bindings_enumerate(
            &bindings, APPLICATION_REPORT_VIEWER, types,
            IOS_ARRAY_COUNT(types), &type_count),
        IOS_OK);
    IOS_TEST_ASSERT(type_count == 2);
    IOS_TEST_ASSERT(types[0] == TYPE_TEXT_DOCUMENT);
    IOS_TEST_ASSERT(types[1] == TYPE_IMAGE_DOCUMENT);

    types[0] = UINT64_MAX;
    type_count = 0;
    IOS_TEST_ASSERT_STATUS(
        ios_application_bindings_enumerate(
            &bindings, APPLICATION_REPORT_VIEWER, types, 1, &type_count),
        IOS_ERROR(IOS_E_NO_SPACE));
    IOS_TEST_ASSERT(type_count == 2 && types[0] == UINT64_MAX);
    IOS_TEST_ASSERT_STATUS(
        ios_application_bindings_enumerate(
            &bindings, UINT64_C(0x554e545255535445), types,
            IOS_ARRAY_COUNT(types), &type_count),
        IOS_ERROR(IOS_E_NOT_FOUND));
}

static void test_type_handles_are_process_local_rights_checked_and_typed(void)
{
    struct ios_application_binding_registry bindings;
    struct ios_type_catalog catalog;
    struct ios_type_capability_service service;
    struct ios_process owner;
    struct ios_process other;
    ios_type_icon_capability text;
    ios_type_icon_capability image;
    ios_handle type_handle;
    ios_handle file_handle;
    ios_u64 internal_type_identity;
    ios_type_icon_capability resolved_catalog_capability;
    ios_u8 file_object = 0;

    initialize_catalog(&catalog, &text, &image);
    initialize_bindings(&bindings);
    initialize_process(&owner, UINT64_C(0x1001), APPLICATION_REPORT_VIEWER);
    initialize_process(&other, UINT64_C(0x2002), APPLICATION_REPORT_VIEWER);
    IOS_TEST_ASSERT_STATUS(
        ios_type_capability_service_initialize(&service, &bindings, &catalog), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_type_capability_mint(&service, &owner, text, &type_handle), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_type_capability_resolve(
            &service, &owner, type_handle, &internal_type_identity,
            &resolved_catalog_capability),
        IOS_OK);
    IOS_TEST_ASSERT(internal_type_identity == TYPE_TEXT_DOCUMENT);
    IOS_TEST_ASSERT(resolved_catalog_capability == text);
    IOS_TEST_ASSERT_STATUS(
        ios_type_capability_authorize(
            &service, &owner, type_handle, TYPE_TEXT_DOCUMENT),
        IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_type_capability_authorize(
            &service, &owner, type_handle, TYPE_IMAGE_DOCUMENT),
        IOS_ERROR(IOS_E_ACCESS_DENIED));
    IOS_TEST_ASSERT_STATUS(
        ios_type_capability_resolve(
            &service, &other, type_handle, &internal_type_identity,
            &resolved_catalog_capability),
        IOS_ERROR(IOS_E_BAD_HANDLE));
    IOS_TEST_ASSERT_STATUS(
        ios_type_capability_resolve(
            &service, &owner, type_handle ^ (UINT64_C(1) << 24),
            &internal_type_identity, &resolved_catalog_capability),
        IOS_ERROR(IOS_E_BAD_HANDLE));

    IOS_TEST_ASSERT_STATUS(
        handle_table_insert(
            &owner.handles, &file_object, IOS_OBJECT_FILE, IOS_RIGHT_ENUMERATE,
            NULL, NULL, &file_handle),
        IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_type_capability_resolve(
            &service, &owner, file_handle, &internal_type_identity,
            &resolved_catalog_capability),
        IOS_ERROR(IOS_E_WRONG_HANDLE_TYPE));
    IOS_TEST_ASSERT_STATUS(
        ios_type_capability_mint(&service, &owner, image, &type_handle),
        IOS_ERROR(IOS_E_ACCESS_DENIED));
}

static void test_stale_or_underprivileged_type_handles_grant_nothing(void)
{
    struct ios_application_binding_registry bindings;
    struct ios_type_catalog catalog;
    struct ios_type_capability_service service;
    struct ios_process process;
    struct ios_process_type_capability fake_capability = { .occupied = true };
    ios_type_icon_capability text;
    ios_type_icon_capability image;
    ios_handle handle;
    ios_handle underprivileged;
    ios_u64 internal_type_identity;
    ios_type_icon_capability resolved_catalog_capability;

    initialize_catalog(&catalog, &text, &image);
    initialize_bindings(&bindings);
    initialize_process(&process, UINT64_C(0x3003), APPLICATION_REPORT_VIEWER);
    IOS_TEST_ASSERT_STATUS(
        ios_type_capability_service_initialize(&service, &bindings, &catalog), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_type_capability_mint(&service, &process, text, &handle), IOS_OK);
    IOS_TEST_ASSERT(ios_type_capability_active_count(&service) == 1);
    IOS_TEST_ASSERT_STATUS(handle_table_close(&process.handles, handle), IOS_OK);
    IOS_TEST_ASSERT(ios_type_capability_active_count(&service) == 0);
    IOS_TEST_ASSERT_STATUS(
        ios_type_capability_resolve(
            &service, &process, handle, &internal_type_identity,
            &resolved_catalog_capability),
        IOS_ERROR(IOS_E_BAD_HANDLE));

    IOS_TEST_ASSERT_STATUS(
        handle_table_insert(
            &process.handles, &fake_capability, IOS_OBJECT_TYPE_CAPABILITY,
            IOS_RIGHT_READ, NULL, NULL, &underprivileged),
        IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_type_capability_resolve(
            &service, &process, underprivileged, &internal_type_identity,
            &resolved_catalog_capability),
        IOS_ERROR(IOS_E_ACCESS_DENIED));
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_trusted_application_binding_allows_only_registered_types),
    IOS_TEST_CASE(test_fabricated_catalog_capabilities_grant_no_type),
    IOS_TEST_CASE(test_binding_registry_is_bounded_deduplicated_and_application_scoped),
    IOS_TEST_CASE(test_type_handles_are_process_local_rights_checked_and_typed),
    IOS_TEST_CASE(test_stale_or_underprivileged_type_handles_grant_nothing)
};

const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
