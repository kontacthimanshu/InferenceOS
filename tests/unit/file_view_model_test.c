#include <inferenceos/test.h>

#include <inferenceos/display_safe_entry.h>
#include <inferenceos/gui/file_explorer.h>
#include <inferenceos/type_catalog.h>

#include <string.h>

#define VIEW_ARRAY_COUNT(values) (sizeof(values) / sizeof((values)[0]))

static struct ios_display_safe_entry report_entry(ios_u64 handle, ios_u64 type)
{
    const struct ios_display_safe_source_entry source = {
        .base_name = "REPORT",
        .object_handle = handle,
        .type_icon_capability = type,
        .allowed_operations = IOS_DISPLAY_SAFE_OPERATION_OPEN,
        .object_kind = IOS_DISPLAY_SAFE_REGULAR_FILE
    };
    struct ios_display_safe_entry entry;
    IOS_TEST_ASSERT_STATUS(ios_display_safe_entry_convert(&source, &entry), IOS_OK);
    return entry;
}

struct fake_view_provider {
    struct ios_type_catalog catalog;
    ios_type_icon_capability text_capability;
    ios_size enumeration_count;
    ios_size create_count;
    ios_size remove_count;
    ios_u64 mutation_parent;
    ios_u64 removed_handle;
    char created_name[IOS_DISPLAY_SAFE_NAME_CAPACITY];
    ios_status mutation_status;
};

static struct ios_display_safe_entry fake_entry(
    const char *name, ios_u64 handle, ios_u64 type, enum ios_display_safe_object_kind kind
)
{
    const struct ios_display_safe_source_entry source = {
        .base_name = name,
        .object_handle = handle,
        .type_icon_capability = type,
        .byte_size = kind == IOS_DISPLAY_SAFE_REGULAR_FILE ? 15 : 0,
        .allowed_operations = kind == IOS_DISPLAY_SAFE_DIRECTORY
            ? IOS_DISPLAY_SAFE_OPERATION_ENUMERATE : IOS_DISPLAY_SAFE_OPERATION_OPEN,
        .object_kind = kind
    };
    struct ios_display_safe_entry entry;
    IOS_TEST_ASSERT_STATUS(ios_display_safe_entry_convert(&source, &entry), IOS_OK);
    return entry;
}

static ios_status fake_enumerate(
    void *context, ios_u64 directory_handle,
    struct ios_display_safe_entry *entries, ios_size capacity, ios_size *entry_count
)
{
    struct fake_view_provider *fake = context;
    ++fake->enumeration_count;
    if (directory_handle == 1) {
        if (capacity < 3) return IOS_ERROR(IOS_E_NO_SPACE);
        entries[0] = fake_entry(
            "REPORT", 7, fake->text_capability, IOS_DISPLAY_SAFE_REGULAR_FILE);
        entries[1] = fake_entry(
            "REPORT", 9, UINT64_C(0xabcdef), IOS_DISPLAY_SAFE_REGULAR_FILE);
        entries[2] = fake_entry("DOCS", 11, 0, IOS_DISPLAY_SAFE_DIRECTORY);
        entries[2].allowed_operations |= IOS_DISPLAY_SAFE_OPERATION_DELETE;
        *entry_count = 3;
        return IOS_OK;
    }
    if (directory_handle == 11) {
        if (capacity < 1) return IOS_ERROR(IOS_E_NO_SPACE);
        entries[0] = fake_entry(
            "NOTES", 12, fake->text_capability, IOS_DISPLAY_SAFE_REGULAR_FILE);
        *entry_count = 1;
        return IOS_OK;
    }
    return IOS_ERROR(IOS_E_NOT_FOUND);
}

static ios_status fake_create_directory(void *context, ios_u64 parent, const char *name)
{
    struct fake_view_provider *fake = context;
    ++fake->create_count;
    fake->mutation_parent = parent;
    strcpy(fake->created_name, name);
    return fake->mutation_status;
}

static ios_status fake_remove_directory(void *context, ios_u64 parent, ios_u64 handle)
{
    struct fake_view_provider *fake = context;
    ++fake->remove_count;
    fake->mutation_parent = parent;
    fake->removed_handle = handle;
    return fake->mutation_status;
}

static ios_status fake_resolve_icon(
    void *context, ios_u64 capability, ios_u32 object_kind,
    enum ios_presentation_icon *icon
)
{
    struct fake_view_provider *fake = context;
    return ios_type_catalog_resolve_icon(
        &fake->catalog, capability,
        object_kind == IOS_DISPLAY_SAFE_DIRECTORY
            ? IOS_TYPE_CATALOG_DIRECTORY : IOS_TYPE_CATALOG_REGULAR_FILE,
        icon
    );
}

static struct ios_file_explorer_view_provider make_fake_provider(struct fake_view_provider *fake)
{
    memset(fake, 0, sizeof(*fake));
    IOS_TEST_ASSERT_STATUS(
        ios_type_catalog_initialize(&fake->catalog, UINT64_C(0x4242)), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_type_catalog_register(
            &fake->catalog, UINT64_C(0x1001), IOS_ICON_TEXT, &fake->text_capability), IOS_OK);
    return (struct ios_file_explorer_view_provider){ fake, fake_enumerate, fake_resolve_icon };
}

static void test_hidden_name_collisions_are_stable_by_opaque_identity(void)
{
    struct ios_display_safe_entry entries[] = {
        report_entry(30, 100), report_entry(10, 200), report_entry(20, 300)
    };
    ios_size ranks[3];
    IOS_TEST_ASSERT_STATUS(
        ios_display_safe_entries_disambiguate(
            entries, VIEW_ARRAY_COUNT(entries), ranks, VIEW_ARRAY_COUNT(ranks)), IOS_OK);
    IOS_TEST_ASSERT(strcmp(entries[0].display_name, "REPORT (3)") == 0);
    IOS_TEST_ASSERT(strcmp(entries[1].display_name, "REPORT") == 0);
    IOS_TEST_ASSERT(strcmp(entries[2].display_name, "REPORT (2)") == 0);
    IOS_TEST_ASSERT(strchr(entries[0].display_name, '.') == NULL);
    IOS_TEST_ASSERT(strchr(entries[2].display_name, '.') == NULL);
}

static void test_collision_labels_do_not_change_persistent_identity(void)
{
    struct ios_display_safe_entry entries[] = {
        report_entry(9, 100), report_entry(4, 200)
    };
    const ios_u64 first_handle = entries[0].object_handle;
    const ios_u64 second_handle = entries[1].object_handle;
    ios_size ranks[2];
    IOS_TEST_ASSERT_STATUS(
        ios_display_safe_entries_disambiguate(
            entries, VIEW_ARRAY_COUNT(entries), ranks, VIEW_ARRAY_COUNT(ranks)), IOS_OK);
    IOS_TEST_ASSERT(entries[0].object_handle == first_handle);
    IOS_TEST_ASSERT(entries[1].object_handle == second_handle);
    IOS_TEST_ASSERT(entries[0].type_icon_capability == 100);
    IOS_TEST_ASSERT(entries[1].type_icon_capability == 200);
}

static void test_known_types_map_to_icons_and_unknown_types_use_generic_fallback(void)
{
    struct ios_type_catalog catalog;
    ios_type_icon_capability text_capability;
    enum ios_presentation_icon icon;
    IOS_TEST_ASSERT_STATUS(ios_type_catalog_initialize(&catalog, UINT64_C(0x12345678)), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_type_catalog_register(&catalog, UINT64_C(0x1001), IOS_ICON_TEXT, &text_capability),
        IOS_OK);
    struct ios_display_safe_entry known = report_entry(1, text_capability);
    struct ios_display_safe_entry unknown = report_entry(2, UINT64_C(0xabcdef));
    struct ios_display_safe_entry directory = report_entry(3, 1);
    directory.object_kind = IOS_DISPLAY_SAFE_DIRECTORY;
    IOS_TEST_ASSERT(
        ios_type_catalog_resolve_icon(
            &catalog, known.type_icon_capability, IOS_TYPE_CATALOG_REGULAR_FILE, &icon) == IOS_OK
        && icon == IOS_ICON_TEXT);
    IOS_TEST_ASSERT(
        ios_type_catalog_resolve_icon(
            &catalog, unknown.type_icon_capability, IOS_TYPE_CATALOG_REGULAR_FILE, &icon) == IOS_OK
        && icon == IOS_ICON_GENERIC_FILE);
    IOS_TEST_ASSERT(
        ios_type_catalog_resolve_icon(
            &catalog, directory.type_icon_capability, IOS_TYPE_CATALOG_DIRECTORY, &icon) == IOS_OK
        && icon == IOS_ICON_FOLDER);
    IOS_TEST_ASSERT(ios_type_catalog_entry_count(&catalog) == 1);
}

static void test_catalog_tokens_are_opaque_and_registration_is_stable(void)
{
    struct ios_type_catalog catalog;
    ios_type_icon_capability first;
    ios_type_icon_capability repeated;
    IOS_TEST_ASSERT_STATUS(ios_type_catalog_initialize(&catalog, UINT64_C(0x99)), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_type_catalog_register(&catalog, UINT64_C(0x545854), IOS_ICON_TEXT, &first), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_type_catalog_register(&catalog, UINT64_C(0x545854), IOS_ICON_TEXT, &repeated), IOS_OK);
    IOS_TEST_ASSERT(first != IOS_INVALID_TYPE_ICON_CAPABILITY);
    IOS_TEST_ASSERT(first == repeated);
    IOS_TEST_ASSERT(first != UINT64_C(0x545854));
    IOS_TEST_ASSERT(ios_type_catalog_entry_count(&catalog) == 1);
    IOS_TEST_ASSERT_STATUS(
        ios_type_catalog_register(&catalog, UINT64_C(0x545854), IOS_ICON_IMAGE, &repeated),
        IOS_ERROR(IOS_E_ALREADY_EXISTS));
}

static void test_injected_provider_builds_safe_selectable_model_and_properties(void)
{
    struct fake_view_provider fake;
    struct ios_file_explorer_model model;
    struct ios_file_explorer_properties properties;
    IOS_TEST_ASSERT_STATUS(
        ios_file_explorer_model_initialize(&model, make_fake_provider(&fake), 1), IOS_OK);
    IOS_TEST_ASSERT(model.entry_count == 3);
    IOS_TEST_ASSERT(strcmp(model.entries[0].display_name, "REPORT") == 0);
    IOS_TEST_ASSERT(strcmp(model.entries[1].display_name, "REPORT (2)") == 0);
    IOS_TEST_ASSERT_STATUS(ios_file_explorer_model_select(&model, 1), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_file_explorer_properties_from_selection(&model, &properties), IOS_OK);
    IOS_TEST_ASSERT(properties.object_handle == 9 && properties.byte_size == 15);
    IOS_TEST_ASSERT(strcmp(properties.display_name, "REPORT (2)") == 0);
    IOS_TEST_ASSERT(strchr(properties.display_name, '.') == NULL);
}

static void test_pointer_keyboard_navigation_and_rendering_use_safe_model(void)
{
    struct fake_view_provider fake;
    struct ios_file_explorer_model model;
    ios_u32 pixels[320 * 220] = { 0 };
    ios_u8 glyphs[128 * 16] = { 0 };
    struct ios_graphics_surface surface = { pixels, 320, 220, 320 };
    struct ios_psf2_font font = {
        glyphs, sizeof(glyphs), 128, 16, 8, 16, IOS_RASTER_FONT_MONO1
    };
    struct ios_file_explorer_window window;
    bool activated;
    ios_u64 handle;
    struct ios_input_event pointer = {
        .type = IOS_INPUT_EVENT_POINTER_BUTTON,
        .flags = IOS_INPUT_PRESSED,
        .code = IOS_POINTER_BUTTON_LEFT,
        .x = IOS_FILE_EXPLORER_CELL_WIDTH + 1,
        .y = IOS_FILE_EXPLORER_HEADER_HEIGHT + 1
    };
    struct ios_input_event enter = {
        .type = IOS_INPUT_EVENT_KEY, .flags = IOS_INPUT_PRESSED, .code = IOS_KEY_ENTER
    };
    IOS_TEST_ASSERT_STATUS(
        ios_file_explorer_model_initialize(&model, make_fake_provider(&fake), 1), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_file_explorer_window_initialize(&window, &model, surface, &font), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_file_explorer_window_handle_input(&window, &pointer, &activated, &handle), IOS_OK);
    IOS_TEST_ASSERT(ios_file_explorer_model_selected(&model)->object_handle == 9);
    IOS_TEST_ASSERT_STATUS(
        ios_file_explorer_window_handle_input(&window, &enter, &activated, &handle), IOS_OK);
    IOS_TEST_ASSERT(activated && handle == 9);
    IOS_TEST_ASSERT_STATUS(ios_file_explorer_window_render(&window), IOS_OK);
    IOS_TEST_ASSERT(
        pixels[(IOS_FILE_EXPLORER_HEADER_HEIGHT + 2) * 320
            + IOS_FILE_EXPLORER_CELL_WIDTH + 2] == window.selection_color
    );
    /* Text, generic-file, and folder entries use visibly distinct glyphs. */
    IOS_TEST_ASSERT(pixels[34 * 320 + 35] != pixels[34 * 320 + 131]);
    IOS_TEST_ASSERT(pixels[45 * 320 + 225] != pixels[34 * 320 + 35]);
    IOS_TEST_ASSERT(pixels[45 * 320 + 225] != pixels[34 * 320 + 131]);

    IOS_TEST_ASSERT_STATUS(ios_file_explorer_model_select(&model, 2), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_file_explorer_model_activate(&model, &handle), IOS_OK);
    IOS_TEST_ASSERT(handle == 11 && model.directory_handle == 11 && model.entry_count == 1);
    IOS_TEST_ASSERT_STATUS(ios_file_explorer_model_navigate_back(&model), IOS_OK);
    IOS_TEST_ASSERT(model.directory_handle == 1 && model.entry_count == 3);
}

static void test_icon_grid_renders_all_presentations_and_scrolls_by_rows(void)
{
    struct fake_view_provider fake;
    struct ios_file_explorer_model model;
    ios_type_icon_capability image_capability;
    ios_type_icon_capability application_capability;
    ios_u32 pixels[320 * 220] = { 0 };
    ios_u8 glyphs[128 * 16] = { 0 };
    struct ios_graphics_surface surface = { pixels, 320, 220, 320 };
    struct ios_psf2_font font = {
        glyphs, sizeof(glyphs), 128, 16, 8, 16, IOS_RASTER_FONT_MONO1
    };
    struct ios_file_explorer_window window;
    bool activated;
    ios_u64 handle;
    const struct ios_input_event down = {
        .type = IOS_INPUT_EVENT_KEY, .flags = IOS_INPUT_PRESSED, .code = IOS_KEY_DOWN
    };

    IOS_TEST_ASSERT_STATUS(
        ios_file_explorer_model_initialize(&model, make_fake_provider(&fake), 1), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_type_catalog_register(
            &fake.catalog, UINT64_C(0x2002), IOS_ICON_IMAGE, &image_capability
        ), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_type_catalog_register(
            &fake.catalog, UINT64_C(0x3003), IOS_ICON_APPLICATION,
            &application_capability
        ), IOS_OK);
    model.entries[0] = fake_entry(
        "TEXT", 20, fake.text_capability, IOS_DISPLAY_SAFE_REGULAR_FILE);
    model.entries[1] = fake_entry(
        "IMAGE", 21, image_capability, IOS_DISPLAY_SAFE_REGULAR_FILE);
    model.entries[2] = fake_entry(
        "PROGRAM", 22, application_capability, IOS_DISPLAY_SAFE_REGULAR_FILE);
    model.entries[3] = fake_entry("FOLDER", 23, 0, IOS_DISPLAY_SAFE_DIRECTORY);
    model.entries[4] = fake_entry(
        "ARCHIVE", 24, UINT64_C(0xabcdef), IOS_DISPLAY_SAFE_REGULAR_FILE);
    model.entries[5] = fake_entry(
        "DATA", 25, UINT64_C(0xabcdef), IOS_DISPLAY_SAFE_REGULAR_FILE);
    model.entries[6] = fake_entry(
        "MORE", 26, UINT64_C(0xabcdef), IOS_DISPLAY_SAFE_REGULAR_FILE);
    model.entry_count = 7;
    model.has_selection = false;

    IOS_TEST_ASSERT_STATUS(
        ios_file_explorer_window_initialize(&window, &model, surface, &font), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_file_explorer_window_render(&window), IOS_OK);
    IOS_TEST_ASSERT(pixels[34 * 320 + 35] == UINT32_C(0x004f81bd));
    IOS_TEST_ASSERT(pixels[34 * 320 + 131] == UINT32_C(0x003e9b52));
    IOS_TEST_ASSERT(pixels[35 * 320 + 225] == UINT32_C(0x00934db1));
    IOS_TEST_ASSERT(pixels[137 * 320 + 33] == UINT32_C(0x00d6a832));
    IOS_TEST_ASSERT(pixels[126 * 320 + 131] == UINT32_C(0x00808080));

    IOS_TEST_ASSERT_STATUS(ios_file_explorer_model_select(&model, 0), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_file_explorer_window_handle_input(&window, &down, &activated, &handle), IOS_OK);
    IOS_TEST_ASSERT(model.selected_index == 3 && window.first_visible_index == 0);
    IOS_TEST_ASSERT_STATUS(
        ios_file_explorer_window_handle_input(&window, &down, &activated, &handle), IOS_OK);
    IOS_TEST_ASSERT(model.selected_index == 6 && window.first_visible_index == 3);
}

static void test_controller_navigates_and_mutates_through_opaque_provider(void)
{
    struct fake_view_provider fake;
    struct ios_file_explorer_model model;
    struct ios_file_explorer_controller controller;
    const struct ios_file_explorer_directory_operations operations = {
        .context = &fake,
        .create_directory = fake_create_directory,
        .remove_directory = fake_remove_directory
    };
    IOS_TEST_ASSERT_STATUS(
        ios_file_explorer_model_initialize(&model, make_fake_provider(&fake), 1), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_file_explorer_controller_initialize(&controller, &model, operations), IOS_OK);

    IOS_TEST_ASSERT_STATUS(ios_file_explorer_model_select(&model, 2), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_file_explorer_controller_navigate_selected(&controller), IOS_OK);
    IOS_TEST_ASSERT(model.directory_handle == 11 && model.history_count == 1);
    IOS_TEST_ASSERT_STATUS(
        ios_file_explorer_controller_navigate_back(&controller), IOS_OK);
    IOS_TEST_ASSERT(model.directory_handle == 1 && model.history_count == 0);

    IOS_TEST_ASSERT_STATUS(
        ios_file_explorer_controller_create_directory(&controller, "PROJECTS"), IOS_OK);
    IOS_TEST_ASSERT(fake.create_count == 1 && fake.mutation_parent == 1);
    IOS_TEST_ASSERT(strcmp(fake.created_name, "PROJECTS") == 0);
    IOS_TEST_ASSERT_STATUS(ios_file_explorer_model_select(&model, 2), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_file_explorer_controller_remove_selected_directory(&controller), IOS_OK);
    IOS_TEST_ASSERT(fake.remove_count == 1 && fake.mutation_parent == 1);
    IOS_TEST_ASSERT(fake.removed_handle == 11);
}

static void test_controller_enforces_rights_and_preserves_view_on_provider_failure(void)
{
    struct fake_view_provider fake;
    struct ios_file_explorer_model model;
    struct ios_file_explorer_controller controller;
    const struct ios_file_explorer_directory_operations operations = {
        .context = &fake,
        .create_directory = fake_create_directory,
        .remove_directory = fake_remove_directory
    };
    IOS_TEST_ASSERT_STATUS(
        ios_file_explorer_model_initialize(&model, make_fake_provider(&fake), 1), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_file_explorer_controller_initialize(&controller, &model, operations), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_file_explorer_model_select(&model, 0), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_file_explorer_controller_navigate_selected(&controller),
        IOS_ERROR(IOS_E_INVALID_ARGUMENT));
    IOS_TEST_ASSERT_STATUS(
        ios_file_explorer_controller_remove_selected_directory(&controller),
        IOS_ERROR(IOS_E_INVALID_ARGUMENT));

    fake.mutation_status = IOS_ERROR(IOS_E_IO);
    IOS_TEST_ASSERT_STATUS(
        ios_file_explorer_controller_create_directory(&controller, "FAILED"),
        IOS_ERROR(IOS_E_IO));
    IOS_TEST_ASSERT(model.directory_handle == 1 && model.entry_count == 3);
    IOS_TEST_ASSERT_STATUS(
        ios_file_explorer_controller_create_directory(&controller, ""),
        IOS_ERROR(IOS_E_INVALID_ARGUMENT));

    IOS_TEST_ASSERT_STATUS(ios_file_explorer_model_select(&model, 2), IOS_OK);
    model.entries[2].allowed_operations &= ~IOS_DISPLAY_SAFE_OPERATION_DELETE;
    IOS_TEST_ASSERT_STATUS(
        ios_file_explorer_controller_remove_selected_directory(&controller),
        IOS_ERROR(IOS_E_ACCESS_DENIED));
    IOS_TEST_ASSERT(fake.remove_count == 0);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_hidden_name_collisions_are_stable_by_opaque_identity),
    IOS_TEST_CASE(test_collision_labels_do_not_change_persistent_identity),
    IOS_TEST_CASE(test_known_types_map_to_icons_and_unknown_types_use_generic_fallback),
    IOS_TEST_CASE(test_catalog_tokens_are_opaque_and_registration_is_stable),
    IOS_TEST_CASE(test_injected_provider_builds_safe_selectable_model_and_properties),
    IOS_TEST_CASE(test_pointer_keyboard_navigation_and_rendering_use_safe_model),
    IOS_TEST_CASE(test_icon_grid_renders_all_presentations_and_scrolls_by_rows),
    IOS_TEST_CASE(test_controller_navigates_and_mutates_through_opaque_provider),
    IOS_TEST_CASE(test_controller_enforces_rights_and_preserves_view_on_provider_failure)
};
const size_t ios_test_case_count = VIEW_ARRAY_COUNT(ios_test_cases);
