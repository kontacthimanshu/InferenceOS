#ifndef INFERENCEOS_VFS_H
#define INFERENCEOS_VFS_H

#include <inferenceos/errors.h>

struct ios_block_device;

enum {
    IOS_VFS_MAX_MOUNTS = 8,
    IOS_VFS_ROOT_OBJECT_ID = 2,
    IOS_VFS_DISPLAY_NAME_CAPACITY = 64,
    IOS_VFS_PATH_MAX = 255,
    IOS_VFS_PATH_CAPACITY = IOS_VFS_PATH_MAX + 1,
    IOS_VFS_MAX_DIRECTORY_LEVELS = 16
};

enum ios_mount_state {
    IOS_MOUNT_RW,
    IOS_MOUNT_DIAGNOSTIC,
    IOS_MOUNT_REJECTED
};

enum ios_vfs_object_kind {
    IOS_VFS_OBJECT_DIRECTORY,
    IOS_VFS_OBJECT_REGULAR_FILE
};

struct ios_vfs_object {
    ios_u64 identity;
    enum ios_vfs_object_kind kind;
    ios_u32 reference_count;
};

enum ios_vfs_file_operation {
    IOS_VFS_FILE_OPEN = UINT64_C(1) << 0,
    IOS_VFS_FILE_READ = UINT64_C(1) << 1,
    IOS_VFS_FILE_WRITE = UINT64_C(1) << 2,
    IOS_VFS_FILE_RENAME = UINT64_C(1) << 3,
    IOS_VFS_FILE_DELETE = UINT64_C(1) << 4,
    IOS_VFS_FILE_ENUMERATE = UINT64_C(1) << 5
};

enum ios_vfs_generic_attribute {
    IOS_VFS_ATTRIBUTE_READ_ONLY = UINT32_C(1) << 0
};

struct ios_vfs_directory_entry {
    char display_base_name[IOS_VFS_DISPLAY_NAME_CAPACITY];
    ios_u64 object_identity;
    ios_u64 internal_type_identity;
    ios_u64 type_prefilter;
    ios_u64 byte_size;
    ios_u64 allowed_operations;
    ios_u32 generic_attributes;
    enum ios_vfs_object_kind kind;
};

typedef ios_status (*ios_vfs_enumerate_function)(
    void *driver_context,
    ios_u64 directory_identity,
    ios_u64 continuation,
    struct ios_vfs_directory_entry *entries,
    ios_size capacity,
    ios_size *entry_count,
    ios_u64 *next_continuation
);
typedef ios_status (*ios_vfs_lookup_function)(
    void *driver_context,
    ios_u64 directory_identity,
    const char *component,
    ios_size component_length,
    struct ios_vfs_object *object
);
typedef ios_status (*ios_vfs_create_directory_function)(
    void *driver_context,
    ios_u64 parent_identity,
    const char *component,
    ios_size component_length,
    struct ios_vfs_object *object
);
typedef ios_status (*ios_vfs_remove_directory_function)(
    void *driver_context,
    ios_u64 parent_identity,
    const char *component,
    ios_size component_length
);
typedef ios_status (*ios_vfs_rename_function)(
    void *driver_context,
    ios_u64 source_parent_identity,
    const char *source_component,
    ios_size source_component_length,
    ios_u64 destination_parent_identity,
    const char *destination_component,
    ios_size destination_component_length
);

typedef ios_status (*ios_vfs_unmount_sync_function)(void *context);
typedef void (*ios_vfs_unmount_invalidate_function)(void *context);

enum ios_vfs_mount_lifecycle {
    IOS_VFS_MOUNT_DETACHED,
    IOS_VFS_MOUNT_ACTIVE,
    IOS_VFS_MOUNT_DRAINING
};

struct ios_vfs_mount {
    const char *driver_name;
    void *driver_context;
    struct ios_block_device *device;
    struct ios_vfs_object *root;
    ios_vfs_enumerate_function enumerate;
    ios_vfs_lookup_function lookup;
    ios_vfs_create_directory_function create_directory;
    ios_vfs_remove_directory_function remove_directory;
    ios_vfs_rename_function rename;
    enum ios_mount_state state;
    void *unmount_context;
    ios_vfs_unmount_sync_function unmount_sync;
    ios_vfs_unmount_invalidate_function unmount_invalidate;
    ios_u32 active_operations;
    ios_u32 generation;
    enum ios_vfs_mount_lifecycle lifecycle;
    bool mounted;
};

struct ios_vfs_path_context {
    struct ios_vfs_mount *mount;
    ios_u64 current_directory_identity;
    ios_u32 mount_generation;
    char current_directory[IOS_VFS_PATH_CAPACITY];
};

struct ios_vfs_mount_registry {
    struct ios_vfs_mount *mounts[IOS_VFS_MAX_MOUNTS];
    ios_size count;
    struct ios_vfs_mount *root;
    ios_u32 next_generation;
};

void vfs_mount_registry_initialize(struct ios_vfs_mount_registry *registry);
ios_status vfs_path_normalize(
    const char *current_directory,
    const char *path,
    char *normalized,
    ios_size normalized_capacity
);
ios_status vfs_path_context_initialize(
    struct ios_vfs_path_context *context,
    struct ios_vfs_mount *mount
);
ios_status vfs_path_resolve(
    const struct ios_vfs_path_context *context,
    const char *path,
    struct ios_vfs_object *object
);
ios_status vfs_path_set_current(
    struct ios_vfs_path_context *context,
    const char *path
);
ios_status vfs_path_get_current(
    const struct ios_vfs_path_context *context,
    char *path,
    ios_size capacity
);
ios_status vfs_create_directory(
    const struct ios_vfs_path_context *context,
    const char *path,
    struct ios_vfs_object *object
);
ios_status vfs_remove_directory(
    const struct ios_vfs_path_context *context,
    const char *path
);
ios_status vfs_list_directory(
    const struct ios_vfs_path_context *context,
    const char *path,
    ios_u64 continuation,
    struct ios_vfs_directory_entry *entries,
    ios_size capacity,
    ios_size *entry_count,
    ios_u64 *next_continuation
);
ios_status vfs_rename(
    const struct ios_vfs_path_context *context,
    const char *source,
    const char *destination
);
ios_status vfs_mount_root(
    struct ios_vfs_mount_registry *registry, struct ios_vfs_mount *mount, const char *path
);
struct ios_vfs_mount *vfs_root_mount(const struct ios_vfs_mount_registry *registry);
ios_status vfs_mount_configure_unmount(
    struct ios_vfs_mount *mount,
    void *context,
    ios_vfs_unmount_sync_function sync,
    ios_vfs_unmount_invalidate_function invalidate
);
ios_status vfs_mount_begin_operation(struct ios_vfs_mount *mount, bool mutation);
ios_status vfs_mount_end_operation(struct ios_vfs_mount *mount);
ios_status vfs_enumerate(
    struct ios_vfs_mount *mount,
    ios_u64 directory_identity,
    ios_u64 continuation,
    struct ios_vfs_directory_entry *entries,
    ios_size capacity,
    ios_size *entry_count,
    ios_u64 *next_continuation
);
ios_status vfs_unmount_root(struct ios_vfs_mount_registry *registry);

#endif
