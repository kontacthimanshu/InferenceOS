#ifndef INFERENCEOS_GUI_VIEW_H
#define INFERENCEOS_GUI_VIEW_H

#include <inferenceos/gui/window.h>
#include <inferenceos/process.h>
#include <inferenceos/shell_protocol.h>

struct ios_gui_view_target {
    struct ios_window_manager *window_manager;
    struct ios_window_handle native_window;
    ios_u64 owner_process_id;
    ios_u64 owner_application_identity;
    ios_u64 last_render_sequence;
};

typedef const struct ios_process *(*ios_gui_view_process_resolver)(
    void *context,
    ios_u64 process_id,
    ios_u64 application_identity
);

struct ios_gui_view_service {
    ios_gui_view_process_resolver resolve_process;
    void *resolver_context;
};

ios_status ios_gui_view_service_initialize(
    struct ios_gui_view_service *service,
    ios_gui_view_process_resolver resolve_process,
    void *resolver_context
);
ios_status ios_gui_view_target_initialize(
    struct ios_gui_view_target *target,
    struct ios_window_manager *window_manager,
    struct ios_window_handle native_window,
    ios_u64 owner_process_id,
    ios_u64 owner_application_identity
);
ios_status ios_gui_view_dispatch(
    void *context,
    ios_u64 caller_process_id,
    ios_u64 caller_application_identity,
    const struct ios_shell_gui_view_request *request,
    struct ios_shell_gui_view_reply *reply
);

#endif
