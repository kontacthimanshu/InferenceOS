#ifndef INFERENCEOS_GUI_FILE_EXPLORER_H
#define INFERENCEOS_GUI_FILE_EXPLORER_H

#include <inferenceos/display_safe_entry.h>
#include <inferenceos/fs_diagnostic.h>
#include <inferenceos/gui/graphics.h>
#include <inferenceos/gui/input.h>
#include <inferenceos/shell_protocol.h>
#include <inferenceos/type_catalog.h>

enum {
    IOS_FILE_EXPLORER_ENTRY_CAPACITY = 64,
    IOS_FILE_EXPLORER_HISTORY_CAPACITY = 16,
    IOS_FILE_EXPLORER_ROW_HEIGHT = 20,
    IOS_FILE_EXPLORER_ICON_SIZE = 12
};

enum ios_file_explorer_diagnostic_page {
    IOS_FILE_EXPLORER_DIAGNOSTIC_FILESYSTEM,
    IOS_FILE_EXPLORER_DIAGNOSTIC_FILE,
    IOS_FILE_EXPLORER_DIAGNOSTIC_HASH,
    IOS_FILE_EXPLORER_DIAGNOSTIC_FAT,
    IOS_FILE_EXPLORER_DIAGNOSTIC_PAGE_COUNT
};

typedef ios_status (*ios_file_explorer_enumerate_function)(
    void *context,
    ios_u64 directory_handle,
    struct ios_display_safe_entry *entries,
    ios_size capacity,
    ios_size *entry_count
);

typedef ios_status (*ios_file_explorer_resolve_icon_function)(
    void *context,
    ios_u64 type_icon_capability,
    ios_u32 object_kind,
    enum ios_presentation_icon *icon
);

struct ios_file_explorer_view_provider {
    void *context;
    ios_file_explorer_enumerate_function enumerate;
    ios_file_explorer_resolve_icon_function resolve_icon;
};

typedef ios_status (*ios_file_explorer_create_directory_function)(
    void *context,
    ios_u64 parent_directory_handle,
    const char *name
);

typedef ios_status (*ios_file_explorer_remove_directory_function)(
    void *context,
    ios_u64 parent_directory_handle,
    ios_u64 directory_handle
);

struct ios_file_explorer_directory_operations {
    void *context;
    ios_file_explorer_create_directory_function create_directory;
    ios_file_explorer_remove_directory_function remove_directory;
};

struct ios_file_explorer_model {
    struct ios_file_explorer_view_provider provider;
    struct ios_display_safe_entry entries[IOS_FILE_EXPLORER_ENTRY_CAPACITY];
    ios_size disambiguation_workspace[IOS_FILE_EXPLORER_ENTRY_CAPACITY];
    ios_u64 history[IOS_FILE_EXPLORER_HISTORY_CAPACITY];
    ios_u64 directory_handle;
    ios_size entry_count;
    ios_size selected_index;
    ios_size history_count;
    bool has_selection;
};

struct ios_file_explorer_controller {
    struct ios_file_explorer_model *model;
    struct ios_file_explorer_directory_operations operations;
};

struct ios_file_explorer_properties {
    ios_u64 object_handle;
    ios_u64 byte_size;
    ios_u64 allowed_operations;
    ios_u32 generic_attributes;
    ios_u32 object_kind;
    ios_u16 display_name_length;
    char display_name[IOS_DISPLAY_SAFE_NAME_CAPACITY];
};

struct ios_file_explorer_window {
    struct ios_file_explorer_model *model;
    struct ios_graphics_surface surface;
    const struct ios_psf2_font *font;
    ios_u32 background_color;
    ios_u32 selection_color;
    ios_u32 foreground_color;
};

struct ios_file_explorer_client {
    struct ios_process *process;
    struct ios_shell_service *shell_service;
    ios_file_explorer_resolve_icon_function resolve_icon;
    void *resolve_icon_context;
    ios_handle shell_channel;
    ios_u64 shell_generation;
    ios_u64 next_request_id;
    ios_u64 next_render_sequence;
    bool connected;
};

typedef ios_status (*ios_file_explorer_diagnostic_query_function)(
    void *context,
    enum ios_fs_diagnostic_query query,
    ios_u64 object_identity,
    struct ios_fs_diagnostic_reply *reply
);

struct ios_file_explorer_diagnostic_provider {
    void *context;
    ios_file_explorer_diagnostic_query_function query;
};

struct ios_file_explorer_diagnostic_inspector {
    struct ios_file_explorer_model *model;
    struct ios_file_explorer_diagnostic_provider provider;
    struct ios_graphics_surface surface;
    const struct ios_psf2_font *font;
    struct ios_fs_diagnostic_reply replies[IOS_FILE_EXPLORER_DIAGNOSTIC_PAGE_COUNT];
    ios_status page_status[IOS_FILE_EXPLORER_DIAGNOSTIC_PAGE_COUNT];
    ios_u64 object_identity;
    enum ios_file_explorer_diagnostic_page page;
    bool visible;
};

ios_status ios_file_explorer_client_initialize(
    struct ios_file_explorer_client *client,
    struct ios_process *process,
    struct ios_shell_service *shell_service,
    ios_file_explorer_resolve_icon_function resolve_icon,
    void *resolve_icon_context
);
void ios_file_explorer_client_disconnect(struct ios_file_explorer_client *client);
struct ios_file_explorer_view_provider ios_file_explorer_client_provider(
    struct ios_file_explorer_client *client
);
ios_status ios_file_explorer_client_file_view(
    struct ios_file_explorer_client *client,
    enum ios_shell_operation operation,
    ios_u64 directory_handle,
    ios_type_icon_capability type_icon_capability,
    struct ios_display_safe_entry *entries,
    ios_size capacity,
    ios_size *entry_count
);
ios_status ios_file_explorer_client_render(
    struct ios_file_explorer_client *client,
    ios_handle window_handle,
    ios_handle view_handle
);
ios_status ios_file_explorer_client_refresh(
    struct ios_file_explorer_client *client,
    struct ios_file_explorer_model *model
);

ios_status ios_file_explorer_provider_enumerate(
    const struct ios_file_explorer_view_provider *provider,
    ios_u64 directory_handle,
    struct ios_display_safe_entry *entries,
    ios_size capacity,
    ios_size *entry_count
);
ios_status ios_file_explorer_provider_resolve_icon(
    const struct ios_file_explorer_view_provider *provider,
    const struct ios_display_safe_entry *entry,
    enum ios_presentation_icon *icon
);

ios_status ios_file_explorer_model_initialize(
    struct ios_file_explorer_model *model,
    struct ios_file_explorer_view_provider provider,
    ios_u64 root_directory_handle
);
ios_status ios_file_explorer_model_refresh(struct ios_file_explorer_model *model);
ios_status ios_file_explorer_model_select(
    struct ios_file_explorer_model *model, ios_size index
);
const struct ios_display_safe_entry *ios_file_explorer_model_selected(
    const struct ios_file_explorer_model *model
);
ios_status ios_file_explorer_model_activate(
    struct ios_file_explorer_model *model, ios_u64 *activated_handle
);
ios_status ios_file_explorer_model_navigate_back(struct ios_file_explorer_model *model);

ios_status ios_file_explorer_controller_initialize(
    struct ios_file_explorer_controller *controller,
    struct ios_file_explorer_model *model,
    struct ios_file_explorer_directory_operations operations
);
ios_status ios_file_explorer_controller_navigate_selected(
    struct ios_file_explorer_controller *controller
);
ios_status ios_file_explorer_controller_navigate_back(
    struct ios_file_explorer_controller *controller
);
ios_status ios_file_explorer_controller_create_directory(
    struct ios_file_explorer_controller *controller,
    const char *name
);
ios_status ios_file_explorer_controller_remove_selected_directory(
    struct ios_file_explorer_controller *controller
);

ios_status ios_file_explorer_properties_from_selection(
    const struct ios_file_explorer_model *model,
    struct ios_file_explorer_properties *properties
);

ios_status ios_file_explorer_window_initialize(
    struct ios_file_explorer_window *window,
    struct ios_file_explorer_model *model,
    struct ios_graphics_surface surface,
    const struct ios_psf2_font *font
);
ios_status ios_file_explorer_window_render(struct ios_file_explorer_window *window);
ios_status ios_file_explorer_window_handle_input(
    struct ios_file_explorer_window *window,
    const struct ios_input_event *event,
    bool *activated,
    ios_u64 *activated_handle
);

ios_status ios_file_explorer_diagnostic_inspector_initialize(
    struct ios_file_explorer_diagnostic_inspector *inspector,
    struct ios_file_explorer_model *model,
    struct ios_file_explorer_diagnostic_provider provider,
    struct ios_graphics_surface surface,
    const struct ios_psf2_font *font
);
ios_status ios_file_explorer_diagnostic_inspector_open(
    struct ios_file_explorer_diagnostic_inspector *inspector
);
void ios_file_explorer_diagnostic_inspector_close(
    struct ios_file_explorer_diagnostic_inspector *inspector
);
ios_status ios_file_explorer_diagnostic_inspector_render(
    struct ios_file_explorer_diagnostic_inspector *inspector
);
ios_status ios_file_explorer_diagnostic_inspector_handle_input(
    struct ios_file_explorer_diagnostic_inspector *inspector,
    const struct ios_input_event *event
);

#endif
