#ifndef INFERENCEOS_FILE_VIEW_H
#define INFERENCEOS_FILE_VIEW_H

#include <inferenceos/shell_protocol.h>
#include <inferenceos/type_catalog.h>
#include <inferenceos/vfs.h>

struct ios_file_view_service {
    struct ios_vfs_mount_registry *mount_registry;
    const struct ios_type_catalog *type_catalog;
    ios_type_icon_capability generic_file_capability;
};

ios_status ios_file_view_service_initialize(
    struct ios_file_view_service *service,
    struct ios_vfs_mount_registry *mount_registry,
    const struct ios_type_catalog *type_catalog,
    ios_type_icon_capability generic_file_capability
);

ios_status ios_file_view_dispatch(
    void *context,
    ios_u64 caller_process_id,
    ios_u64 caller_application_identity,
    enum ios_shell_operation operation,
    const struct ios_shell_file_view_request *request,
    struct ios_shell_file_view_reply *reply
);

#endif
