#ifndef INFERENCEOS_TXT_VIEWER_H
#define INFERENCEOS_TXT_VIEWER_H

#include <inferenceos/gui/file_explorer.h>

enum {
    IOS_TXT_VIEWER_APPLICATION_ID = 4104
};

struct ios_txt_viewer_application {
    struct ios_file_explorer_client client;
    struct ios_file_explorer_model model;
    struct ios_file_explorer_window window;
    ios_type_icon_capability txt_type_capability;
};

ios_status ios_txt_viewer_initialize(
    struct ios_txt_viewer_application *application,
    struct ios_process *process,
    struct ios_shell_service *shell_service,
    ios_file_explorer_resolve_icon_function resolve_icon,
    void *resolve_icon_context,
    ios_u64 root_directory_handle,
    ios_type_icon_capability txt_type_capability,
    struct ios_graphics_surface surface,
    const struct ios_psf2_font *font
);
void ios_txt_viewer_disconnect(struct ios_txt_viewer_application *application);
ios_status ios_txt_viewer_render(struct ios_txt_viewer_application *application);
ios_status ios_txt_viewer_handle_input(
    struct ios_txt_viewer_application *application,
    const struct ios_input_event *event,
    bool *activated,
    ios_u64 *activated_handle
);

#endif
