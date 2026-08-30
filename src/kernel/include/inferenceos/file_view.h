#ifndef INFERENCEOS_FILE_VIEW_H
#define INFERENCEOS_FILE_VIEW_H

#include <inferenceos/shell_protocol.h>
#include <inferenceos/type_catalog.h>
#include <inferenceos/vfs.h>

struct ios_file_view_service {
    struct ios_vfs_mount_registry *mount_registry;
    const struct ios_type_catalog *type_catalog;
    ios_type_icon_capability generic_file_capability;
    struct ios_vfs_directory_entry candidates[64];
    struct ios_display_safe_entry display_entries[64];
    ios_size disambiguation_workspace[64];
};

enum { IOS_FILE_VIEW_DIRECTORY_CAPACITY = 64 };

struct ios_file_view_resolved_object {
    struct ios_vfs_mount *mount;
    ios_u64 object_identity;
    ios_u64 parent_identity;
    ios_u64 internal_type_identity;
    ios_u64 byte_size;
    ios_u64 allowed_operations;
    ios_u32 generic_attributes;
    enum ios_vfs_object_kind kind;
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

ios_status ios_file_view_list_directory_path(
    struct ios_file_view_service *service,
    const struct ios_vfs_path_context *path_context,
    const char *path,
    struct ios_display_safe_entry *entries,
    ios_size capacity,
    ios_size *entry_count
);

ios_status ios_file_view_resolve_display_path(
    struct ios_file_view_service *service,
    const struct ios_vfs_path_context *path_context,
    const char *path,
    struct ios_file_view_resolved_object *resolved
);

#endif
