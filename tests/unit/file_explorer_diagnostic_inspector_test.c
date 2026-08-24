#include <inferenceos/test.h>

#include <inferenceos/arch/interrupts.h>
#include <inferenceos/gui/file_explorer.h>

#include <string.h>

struct diagnostic_fixture {
    struct ios_fs_primary_disk primary;
    struct ios_fs_companion_disk companion;
    ios_u32 fat[8];
};

struct diagnostic_provider_context {
    struct ios_fs_diagnostic_service *service;
    const struct ios_process *caller;
    ios_handle authority;
};

ios_u64 x86_64_interrupt_save_disable(void) { return 0; }
void x86_64_interrupt_restore(ios_u64 flags) { (void)flags; }

static ios_status provide_snapshot(
    void *opaque, enum ios_fs_diagnostic_query query, ios_u64 identity,
    struct ios_fs_diagnostic_source *source
)
{
    struct diagnostic_fixture *fixture = opaque;
    if (query != IOS_FS_DIAGNOSTIC_QUERY_FILESYSTEM && identity != 77) {
        return IOS_ERROR(IOS_E_NOT_FOUND);
    }
    source->primary = fixture->primary;
    source->companion = fixture->companion;
    source->primary_record_location = 96;
    source->companion_record_location = 64;
    source->fat = fixture->fat;
    source->fat_entry_count = IOS_ARRAY_COUNT(fixture->fat);
    source->has_primary = true;
    source->has_companion = true;
    source->free_space_known = true;
    source->free_bytes = 4096;
    source->registry_health = IOS_FS_DIAGNOSTIC_REGISTRY_DISABLED;
    return IOS_OK;
}

static ios_status query_diagnostic(
    void *opaque, enum ios_fs_diagnostic_query query, ios_u64 object_identity,
    struct ios_fs_diagnostic_reply *reply
)
{
    const struct diagnostic_provider_context *provider = opaque;
    const struct ios_fs_diagnostic_request request = {
        sizeof(struct ios_fs_diagnostic_request), IOS_FS_DIAGNOSTIC_ABI_VERSION,
        query, provider->authority, object_identity,
        IOS_FS_DIAGNOSTIC_CHAIN_CAPACITY, 0
    };
    return ios_fs_diagnostic_dispatch(
        provider->service, provider->caller, &request, reply
    );
}

static void initialize_fixture(
    struct ios_file_explorer_model *model,
    struct ios_process *process,
    struct ios_fs_mount *mount,
    struct diagnostic_fixture *fixture,
    struct ios_fs_diagnostic_service *service,
    struct ios_fs_diagnostic_authority *authority,
    ios_handle *handle
)
{
    static struct ios_vfs_object root = { 2, IOS_VFS_OBJECT_DIRECTORY, 0 };
    const struct ios_fs_primary primary = {
        { 'R', 'E', 'P', 'O', 'R', 'T', ' ', ' ', 'T', 'X', 'T' },
        IOS_FS_ATTRIBUTE_REGULAR, 2, 4096
    };
    memset(model, 0, sizeof(*model));
    memset(process, 0, sizeof(*process));
    memset(mount, 0, sizeof(*mount));
    memset(fixture, 0, sizeof(*fixture));
    model->entries[0].object_handle = 77;
    model->entries[0].object_kind = IOS_DISPLAY_SAFE_REGULAR_FILE;
    model->entry_count = 1;
    model->selected_index = 0;
    model->has_selection = true;
    process->process_id = 12;
    process->application_identity = UINT64_C(0x46455850);
    process->state = IOS_PROCESS_RUNNABLE;
    IOS_TEST_ASSERT_STATUS(handle_table_initialize(&process->handles, process->process_id), IOS_OK);
    *authority = (struct ios_fs_diagnostic_authority){
        process->process_id, process->application_identity, IOS_FS_DIAGNOSTIC_SCOPE_ALL
    };
    IOS_TEST_ASSERT_STATUS(handle_table_insert(
        &process->handles, authority, IOS_OBJECT_DIAGNOSTIC_CAPABILITY,
        IOS_RIGHT_DIAGNOSTIC, NULL, NULL, handle
    ), IOS_OK);
    mount->vfs.root = &root;
    mount->vfs.state = IOS_MOUNT_RW;
    mount->vfs.lifecycle = IOS_VFS_MOUNT_ACTIVE;
    mount->vfs.mounted = true;
    mount->report.bounds_trusted = true;
    mount->geometry.total_sectors = 1000;
    mount->geometry.usable_bytes = 24576;
    mount->geometry.cluster_count = 6;
    mount->geometry.fat_sectors = 1;
    IOS_TEST_ASSERT_STATUS(ios_fs_primary_encode(&primary, &fixture->primary), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_fs_companion_encode(primary.name, true, &fixture->companion), IOS_OK);
    fixture->fat[0] = IOS_FS_FAT_END_OF_CHAIN;
    fixture->fat[1] = IOS_FS_FAT_END_OF_CHAIN;
    fixture->fat[2] = IOS_FS_FAT_END_OF_CHAIN;
    IOS_TEST_ASSERT_STATUS(ios_fs_diagnostic_service_initialize(
        service, mount, provide_snapshot, fixture
    ), IOS_OK);
}

static void test_inspector_uses_selected_opaque_identity_and_bounded_dtos(void)
{
    struct ios_file_explorer_model model;
    struct ios_process process;
    struct ios_fs_mount mount;
    struct diagnostic_fixture fixture;
    struct ios_fs_diagnostic_service service;
    struct ios_fs_diagnostic_authority authority;
    struct ios_file_explorer_diagnostic_inspector inspector;
    struct diagnostic_provider_context provider_context;
    ios_handle handle;
    ios_u32 pixels[320 * 160] = { 0 };
    ios_u8 glyphs[128 * 16] = { 0 };
    struct ios_graphics_surface surface = { pixels, 320, 160, 320 };
    struct ios_psf2_font font = { glyphs, sizeof(glyphs), 128, 16, 8, 16 };
    initialize_fixture(&model, &process, &mount, &fixture, &service, &authority, &handle);
    provider_context = (struct diagnostic_provider_context){ &service, &process, handle };
    IOS_TEST_ASSERT_STATUS(ios_file_explorer_diagnostic_inspector_initialize(
        &inspector, &model,
        (struct ios_file_explorer_diagnostic_provider){ &provider_context, query_diagnostic },
        surface, &font
    ), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_file_explorer_diagnostic_inspector_open(&inspector), IOS_OK);
    IOS_TEST_ASSERT(inspector.visible && inspector.object_identity == 77);
    IOS_TEST_ASSERT(inspector.page == IOS_FILE_EXPLORER_DIAGNOSTIC_FILE);
    IOS_TEST_ASSERT(inspector.replies[IOS_FILE_EXPLORER_DIAGNOSTIC_FILE]
        .value.file.first_cluster == 2);
    IOS_TEST_ASSERT(inspector.replies[IOS_FILE_EXPLORER_DIAGNOSTIC_HASH]
        .value.hash.validation_status == IOS_OK);
    IOS_TEST_ASSERT(inspector.replies[IOS_FILE_EXPLORER_DIAGNOSTIC_FAT]
        .value.fat.cluster_count == 1);
    IOS_TEST_ASSERT_STATUS(ios_file_explorer_diagnostic_inspector_render(&inspector), IOS_OK);
    IOS_TEST_ASSERT(pixels[0] != 0 && pixels[23 * 320] != pixels[0]);
}

static void test_inspector_navigation_and_close_clear_privileged_state(void)
{
    struct ios_file_explorer_model model;
    struct ios_process process;
    struct ios_fs_mount mount;
    struct diagnostic_fixture fixture;
    struct ios_fs_diagnostic_service service;
    struct ios_fs_diagnostic_authority authority;
    struct ios_file_explorer_diagnostic_inspector inspector;
    struct diagnostic_provider_context provider_context;
    ios_handle handle;
    ios_u32 pixels[320 * 160] = { 0 };
    ios_u8 glyphs[128 * 16] = { 0 };
    struct ios_graphics_surface surface = { pixels, 320, 160, 320 };
    struct ios_psf2_font font = { glyphs, sizeof(glyphs), 128, 16, 8, 16 };
    struct ios_input_event right = {
        .type = IOS_INPUT_EVENT_KEY, .flags = IOS_INPUT_PRESSED, .code = IOS_KEY_RIGHT
    };
    struct ios_input_event escape = {
        .type = IOS_INPUT_EVENT_KEY, .flags = IOS_INPUT_PRESSED, .code = IOS_KEY_ESCAPE
    };
    initialize_fixture(&model, &process, &mount, &fixture, &service, &authority, &handle);
    provider_context = (struct diagnostic_provider_context){ &service, &process, handle };
    IOS_TEST_ASSERT_STATUS(ios_file_explorer_diagnostic_inspector_initialize(
        &inspector, &model,
        (struct ios_file_explorer_diagnostic_provider){ &provider_context, query_diagnostic },
        surface, &font
    ), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_file_explorer_diagnostic_inspector_open(&inspector), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_file_explorer_diagnostic_inspector_handle_input(&inspector, &right), IOS_OK
    );
    IOS_TEST_ASSERT(inspector.page == IOS_FILE_EXPLORER_DIAGNOSTIC_HASH);
    IOS_TEST_ASSERT_STATUS(
        ios_file_explorer_diagnostic_inspector_handle_input(&inspector, &escape), IOS_OK
    );
    IOS_TEST_ASSERT(!inspector.visible && inspector.object_identity == 0);
    struct ios_fs_diagnostic_reply zero;
    memset(&zero, 0, sizeof(zero));
    IOS_TEST_ASSERT(memcmp(&inspector.replies[0], &zero, sizeof(zero)) == 0);
    IOS_TEST_ASSERT_STATUS(
        ios_file_explorer_diagnostic_inspector_render(&inspector),
        IOS_ERROR(IOS_E_INVALID_STATE)
    );
}

static void test_inspector_refuses_under_scoped_authority(void)
{
    struct ios_file_explorer_model model;
    struct ios_process process;
    struct ios_fs_mount mount;
    struct diagnostic_fixture fixture;
    struct ios_fs_diagnostic_service service;
    struct ios_fs_diagnostic_authority authority;
    struct ios_file_explorer_diagnostic_inspector inspector;
    struct diagnostic_provider_context provider_context;
    ios_handle handle;
    ios_u32 pixels[320 * 160] = { 0 };
    ios_u8 glyphs[128 * 16] = { 0 };
    struct ios_graphics_surface surface = { pixels, 320, 160, 320 };
    struct ios_psf2_font font = { glyphs, sizeof(glyphs), 128, 16, 8, 16 };
    initialize_fixture(&model, &process, &mount, &fixture, &service, &authority, &handle);
    authority.scope = IOS_FS_DIAGNOSTIC_SCOPE_FILESYSTEM;
    provider_context = (struct diagnostic_provider_context){ &service, &process, handle };
    IOS_TEST_ASSERT_STATUS(ios_file_explorer_diagnostic_inspector_initialize(
        &inspector, &model,
        (struct ios_file_explorer_diagnostic_provider){ &provider_context, query_diagnostic },
        surface, &font
    ), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_file_explorer_diagnostic_inspector_open(&inspector),
        IOS_ERROR(IOS_E_ACCESS_DENIED)
    );
    IOS_TEST_ASSERT(!inspector.visible && inspector.object_identity == 0);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_inspector_uses_selected_opaque_identity_and_bounded_dtos),
    IOS_TEST_CASE(test_inspector_navigation_and_close_clear_privileged_state),
    IOS_TEST_CASE(test_inspector_refuses_under_scoped_authority)
};
const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
