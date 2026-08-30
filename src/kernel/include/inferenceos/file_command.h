#ifndef INFERENCEOS_FILE_COMMAND_H
#define INFERENCEOS_FILE_COMMAND_H

#include <inferenceos/file_view.h>

struct ios_file_command_service {
    struct ios_file_view_service *file_view;
    struct ios_vfs_path_context *path_context;
};

typedef void (*ios_file_command_output)(const char *text, void *context);

ios_status ios_file_command_service_initialize(
    struct ios_file_command_service *service,
    struct ios_file_view_service *file_view,
    struct ios_vfs_path_context *path_context
);
ios_status ios_file_command_write(
    struct ios_file_command_service *service,
    const char *display_path,
    const void *bytes,
    ios_size length
);
ios_status ios_file_command_append(
    struct ios_file_command_service *service,
    const char *display_path,
    const void *bytes,
    ios_size length
);
ios_status ios_file_command_cat(
    struct ios_file_command_service *service,
    const char *display_path,
    ios_file_command_output output,
    void *output_context
);
ios_status ios_file_command_rename(
    struct ios_file_command_service *service,
    const char *source_display_path,
    const char *destination_display_path
);
ios_status ios_file_command_remove(
    struct ios_file_command_service *service,
    const char *display_path
);
ios_status ios_file_command_resolve_diagnostic(
    struct ios_file_command_service *service,
    const char *display_path,
    ios_u64 *object_identity
);

#endif
