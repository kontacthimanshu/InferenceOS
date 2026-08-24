#ifndef INFERENCEOS_CUI_FS_H
#define INFERENCEOS_CUI_FS_H

#include <inferenceos/block.h>
#include <inferenceos/cui.h>
#include <inferenceos/display_safe_entry.h>
#include <inferenceos/fs/mount.h>
#include <inferenceos/fs_diagnostic.h>
#include <inferenceos/power.h>

enum { IOS_CUI_MAX_BLOCK_DEVICES = 8 };

struct ios_cui_directory_operations {
    ios_status (*enumerate)(
        void *context,
        const char *path,
        struct ios_display_safe_entry *entries,
        ios_size capacity,
        ios_size *entry_count
    );
    ios_status (*change_current)(void *context, const char *path);
    ios_status (*get_current)(void *context, char *path, ios_size capacity);
    ios_status (*create)(void *context, const char *path);
    ios_status (*remove)(void *context, const char *path);
};

struct ios_cui_file_operations {
    ios_status (*create)(void *context, const char *path);
    ios_status (*write)(void *context, const char *path, const void *bytes, ios_size length);
    ios_status (*append)(void *context, const char *path, const void *bytes, ios_size length);
    ios_status (*type)(
        void *context, const char *path, ios_cui_write output, void *output_context
    );
    ios_status (*rename)(void *context, const char *source, const char *destination);
    ios_status (*remove)(void *context, const char *path);
};

typedef ios_status (*ios_cui_diagnostic_object_resolver)(
    void *context, const char *path, ios_u64 *object_identity
);

struct ios_cui_diagnostic_binding {
    struct ios_fs_diagnostic_service *service;
    const struct ios_process *caller;
    ios_handle authority;
    ios_cui_diagnostic_object_resolver resolve_object;
    void *resolver_context;
};

struct ios_cui_fs_context {
    struct ios_block_device *devices[IOS_CUI_MAX_BLOCK_DEVICES];
    ios_size device_count;
    struct ios_vfs_mount_registry *mount_registry;
    struct ios_fs_mount *filesystem_mount;
    ios_u32 next_volume_serial;
    void *file_context;
    struct ios_cui_file_operations file_operations;
    void *directory_context;
    struct ios_cui_directory_operations directory_operations;
    void *sync_context;
    ios_status (*sync)(void *context);
    struct ios_power_controller *power;
    struct ios_cui_diagnostic_binding diagnostic;
};

ios_status ios_cui_fs_context_initialize(
    struct ios_cui_fs_context *context,
    struct ios_vfs_mount_registry *mount_registry,
    struct ios_fs_mount *filesystem_mount
);
ios_status ios_cui_fs_add_device(
    struct ios_cui_fs_context *context, struct ios_block_device *device
);
ios_status ios_cui_fs_set_file_operations(
    struct ios_cui_fs_context *context,
    void *file_context,
    const struct ios_cui_file_operations *operations
);
ios_status ios_cui_fs_set_directory_operations(
    struct ios_cui_fs_context *context,
    void *directory_context,
    const struct ios_cui_directory_operations *operations
);
ios_status ios_cui_fs_set_sync_operation(
    struct ios_cui_fs_context *context,
    void *sync_context,
    ios_status (*sync)(void *context)
);
ios_status ios_cui_fs_set_power_controller(
    struct ios_cui_fs_context *context, struct ios_power_controller *power
);
ios_status ios_cui_fs_set_diagnostic_service(
    struct ios_cui_fs_context *context,
    struct ios_fs_diagnostic_service *service,
    const struct ios_process *caller,
    ios_handle authority,
    ios_cui_diagnostic_object_resolver resolve_object,
    void *resolver_context
);
ios_status ios_cui_register_fs_commands(struct ios_cui_command_registry *registry);
ios_status ios_cui_register_file_commands(struct ios_cui_command_registry *registry);
ios_status ios_cui_register_directory_commands(struct ios_cui_command_registry *registry);
ios_status ios_cui_register_diagnostic_commands(struct ios_cui_command_registry *registry);
ios_status ios_cui_write_expanded_fsinfo(struct ios_cui_io *io);

#endif
