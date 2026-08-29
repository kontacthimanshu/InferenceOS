#ifndef INFERENCEOS_SHELL_PROTOCOL_H
#define INFERENCEOS_SHELL_PROTOCOL_H

#include <inferenceos/display_safe_entry.h>
#include <inferenceos/ipc.h>

enum {
    IOS_SHELL_PROTOCOL_VERSION = 1,
    IOS_SHELL_FILE_VIEW_REPLY_CAPACITY = 4
};

/* Values are part of the Shell IPC ABI and must not be renumbered. */
enum ios_shell_operation {
    IOS_SHELL_DIRECTORY_VIEW = 1,
    IOS_SHELL_TYPE_VIEW = 2,
    IOS_SHELL_SEARCH = 3,
    IOS_SHELL_GUI_VIEW = 4
};

/*
 * DIRECTORY_VIEW requires directory_handle and zero type_icon_capability.
 * TYPE_VIEW and SEARCH require an opaque type_icon_capability. TYPE_VIEW
 * includes navigable directories and exact-type files; SEARCH is file-only.
 * No ordinary request carries an extension or extension hash.
 */
struct ios_shell_file_view_request {
    ios_u16 size;
    ios_u16 version;
    ios_u32 flags;
    ios_u64 directory_handle;
    ios_u64 type_icon_capability;
    ios_u64 continuation;
    ios_u16 maximum_items;
    ios_u16 reserved16;
    ios_u32 reserved32;
};

struct ios_shell_file_view_reply {
    ios_u16 size;
    ios_u16 version;
    ios_u32 flags;
    ios_i64 status;
    ios_u64 continuation;
    ios_u16 item_count;
    ios_u16 reserved16;
    ios_u32 reserved32;
    struct ios_display_safe_entry entries[IOS_SHELL_FILE_VIEW_REPLY_CAPACITY];
};

/* GUI rendering uses opaque window/view handles and contains no filesystem metadata. */
struct ios_shell_gui_view_request {
    ios_u16 size;
    ios_u16 version;
    ios_u32 flags;
    ios_u64 window_handle;
    ios_u64 view_handle;
    ios_u64 render_sequence;
};

struct ios_shell_gui_view_reply {
    ios_u16 size;
    ios_u16 version;
    ios_u32 flags;
    ios_i64 status;
    ios_u64 render_sequence;
};

struct ios_shell_dispatch_result {
    ios_u64 request_id;
    ios_u32 operation;
    ios_u16 item_count;
    ios_u16 payload_size;
    union {
        struct ios_shell_file_view_reply file_view;
        struct ios_shell_gui_view_reply gui_view;
    } payload;
};

typedef ios_status (*ios_shell_file_view_dispatch_function)(
    void *context,
    ios_u64 caller_process_id,
    ios_u64 caller_application_identity,
    enum ios_shell_operation operation,
    const struct ios_shell_file_view_request *request,
    struct ios_shell_file_view_reply *reply
);

typedef ios_status (*ios_shell_gui_view_dispatch_function)(
    void *context,
    ios_u64 caller_process_id,
    ios_u64 caller_application_identity,
    const struct ios_shell_gui_view_request *request,
    struct ios_shell_gui_view_reply *reply
);

struct ios_shell_service_config {
    struct ios_process *process;
    ios_u16 queue_depth;
    ios_shell_file_view_dispatch_function dispatch_file_view;
    ios_shell_gui_view_dispatch_function dispatch_gui_view;
    void *dispatch_context;
};

struct ios_shell_service {
    struct ios_shell_service_config config;
    ios_handle endpoint_handle;
    ios_u64 generation;
    bool running;
};

ios_status ios_shell_message_validate(const struct ios_ipc_message *message);
ios_status ios_shell_service_start(
    struct ios_shell_service *service,
    const struct ios_shell_service_config *config
);
ios_status ios_shell_service_stop(struct ios_shell_service *service);
ios_status ios_shell_service_restart(struct ios_shell_service *service);
ios_status ios_shell_service_dispatch(
    struct ios_shell_service *service,
    const struct ios_ipc_message *message,
    struct ios_shell_dispatch_result *result
);
ios_status ios_shell_service_receive_and_dispatch(
    struct ios_shell_service *service,
    struct ios_shell_dispatch_result *result,
    bool block
);
ios_status ios_shell_service_exchange(
    struct ios_shell_service *service,
    struct ios_process *client,
    ios_handle shell_channel,
    ios_u64 request_id,
    enum ios_shell_operation operation,
    const void *payload,
    ios_u16 payload_size,
    struct ios_shell_dispatch_result *result
);

IOS_STATIC_ASSERT(
    sizeof(struct ios_shell_file_view_request) == 40,
    "Shell file-view request wire size"
);
IOS_STATIC_ASSERT(
    offsetof(struct ios_shell_file_view_request, directory_handle) == 8,
    "Shell file-view directory handle offset"
);
IOS_STATIC_ASSERT(
    offsetof(struct ios_shell_file_view_request, type_icon_capability) == 16,
    "Shell file-view type capability offset"
);
IOS_STATIC_ASSERT(
    sizeof(struct ios_shell_file_view_reply) == 480,
    "Shell file-view reply wire size"
);
IOS_STATIC_ASSERT(
    offsetof(struct ios_shell_file_view_reply, entries) == 32,
    "Shell file-view entries offset"
);
IOS_STATIC_ASSERT(
    sizeof(struct ios_shell_file_view_reply) <= IOS_IPC_MAX_PAYLOAD_SIZE,
    "Shell file-view reply must fit one IPC payload"
);
IOS_STATIC_ASSERT(
    sizeof(struct ios_shell_gui_view_request) == 32,
    "Shell GUI-view request wire size"
);
IOS_STATIC_ASSERT(
    sizeof(struct ios_shell_gui_view_reply) == 24,
    "Shell GUI-view reply wire size"
);

#endif
