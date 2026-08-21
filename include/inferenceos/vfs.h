#ifndef INFERENCEOS_VFS_H
#define INFERENCEOS_VFS_H

#include <inferenceos/base.h>

#define INFERENCEOS_VFS_MAX_PATH_LENGTH 255U
#define INFERENCEOS_VFS_PATH_CAPACITY 256U
#define INFERENCEOS_VFS_MAX_COMPONENT_LENGTH 255U
#define INFERENCEOS_VFS_MAX_DIRECTORY_LEVELS 16U
#define INFERENCEOS_VFS_PATH_SEPARATOR '/'

typedef struct inferenceos_vfs_mount inferenceos_vfs_mount;
typedef struct inferenceos_vfs_node inferenceos_vfs_node;
typedef struct inferenceos_vfs_file inferenceos_vfs_file;
typedef struct inferenceos_vfs_directory inferenceos_vfs_directory;

typedef enum inferenceos_vfs_status {
    INFERENCEOS_VFS_STATUS_OK = 0,
    INFERENCEOS_VFS_STATUS_NOT_FOUND = 1,
    INFERENCEOS_VFS_STATUS_ALREADY_EXISTS = 2,
    INFERENCEOS_VFS_STATUS_INVALID_NAME = 3,
    INFERENCEOS_VFS_STATUS_INVALID_PATH = 4,
    INFERENCEOS_VFS_STATUS_INVALID_ARGUMENT = 5,
    INFERENCEOS_VFS_STATUS_NOT_MOUNTED = 6,
    INFERENCEOS_VFS_STATUS_READ_ONLY = 7,
    INFERENCEOS_VFS_STATUS_NOT_EMPTY = 8,
    INFERENCEOS_VFS_STATUS_NO_SPACE = 9,
    INFERENCEOS_VFS_STATUS_OUT_OF_RANGE = 10,
    INFERENCEOS_VFS_STATUS_OVERFLOW = 11,
    INFERENCEOS_VFS_STATUS_CORRUPT = 12,
    INFERENCEOS_VFS_STATUS_UNSUPPORTED = 13,
    INFERENCEOS_VFS_STATUS_IO_ERROR = 14,
    INFERENCEOS_VFS_STATUS_TIMEOUT = 15,
    INFERENCEOS_VFS_STATUS_BUSY = 16,
    INFERENCEOS_VFS_STATUS_INVARIANT_FAILURE = 17,
    INFERENCEOS_VFS_STATUS_END_OF_DIRECTORY = 18
} inferenceos_vfs_status;

typedef enum inferenceos_vfs_mount_state {
    INFERENCEOS_VFS_MOUNT_UNMOUNTED = 0,
    INFERENCEOS_VFS_MOUNT_CLEAN_WRITABLE = 1,
    INFERENCEOS_VFS_MOUNT_DIAGNOSTIC_READ_ONLY = 2,
    INFERENCEOS_VFS_MOUNT_REJECTED = 3
} inferenceos_vfs_mount_state;

typedef enum inferenceos_vfs_node_type {
    INFERENCEOS_VFS_NODE_REGULAR_FILE = 1,
    INFERENCEOS_VFS_NODE_DIRECTORY = 2
} inferenceos_vfs_node_type;

typedef enum inferenceos_vfs_open_mode {
    INFERENCEOS_VFS_OPEN_READ = 1U << 0,
    INFERENCEOS_VFS_OPEN_WRITE = 1U << 1,
    INFERENCEOS_VFS_OPEN_APPEND = 1U << 2
} inferenceos_vfs_open_mode;

typedef enum inferenceos_vfs_seek_origin {
    INFERENCEOS_VFS_SEEK_START = 0,
    INFERENCEOS_VFS_SEEK_CURRENT = 1,
    INFERENCEOS_VFS_SEEK_END = 2
} inferenceos_vfs_seek_origin;

typedef struct inferenceos_vfs_path {
    const char *data;
    inferenceos_size length;
} inferenceos_vfs_path;

typedef struct inferenceos_vfs_normalized_path {
    char text[INFERENCEOS_VFS_PATH_CAPACITY];
    inferenceos_size length;
    inferenceos_size component_count;
    inferenceos_u16 component_offsets[INFERENCEOS_VFS_MAX_DIRECTORY_LEVELS];
    inferenceos_u16 component_lengths[INFERENCEOS_VFS_MAX_DIRECTORY_LEVELS];
} inferenceos_vfs_normalized_path;

/* Produces an owned canonical absolute path. current_directory may be NULL to
 * mean root. It is ignored for absolute input. Repeated separators and '.' are
 * removed; '..' at root remains at root. Output is unchanged on failure. */
inferenceos_vfs_status inferenceos_vfs_path_normalize(
    const inferenceos_vfs_normalized_path *current_directory,
    inferenceos_vfs_path input,
    inferenceos_vfs_normalized_path *output
);

inferenceos_vfs_status inferenceos_vfs_path_component(
    const inferenceos_vfs_normalized_path *path,
    inferenceos_size component_index,
    inferenceos_vfs_path *component
);

bool inferenceos_vfs_path_is_root(
    const inferenceos_vfs_normalized_path *path
);

typedef struct inferenceos_vfs_metadata {
    inferenceos_vfs_node_type type;
    inferenceos_u32 attributes;
    inferenceos_u64 size;
} inferenceos_vfs_metadata;

typedef struct inferenceos_vfs_directory_entry {
    char name[INFERENCEOS_VFS_MAX_COMPONENT_LENGTH + 1U];
    inferenceos_size name_length;
    inferenceos_vfs_metadata metadata;
} inferenceos_vfs_directory_entry;

/* Filesystem adapters implement this table. Handles returned by an adapter
 * are opaque, must carry the owning mount generation, and must reject every
 * operation after successful unmount. */
typedef struct inferenceos_vfs_operations {
    inferenceos_vfs_status (*mount)(
        void *filesystem_context,
        inferenceos_vfs_mount **mount,
        inferenceos_vfs_mount_state *state
    );
    inferenceos_vfs_status (*unmount)(inferenceos_vfs_mount *mount);
    inferenceos_vfs_status (*sync)(inferenceos_vfs_mount *mount);
    inferenceos_vfs_status (*resolve)(
        inferenceos_vfs_mount *mount,
        inferenceos_vfs_node *current_directory,
        inferenceos_vfs_path path,
        inferenceos_vfs_node **node
    );
    inferenceos_vfs_status (*release_node)(inferenceos_vfs_node *node);
    inferenceos_vfs_status (*create)(
        inferenceos_vfs_mount *mount,
        inferenceos_vfs_node *current_directory,
        inferenceos_vfs_path path
    );
    inferenceos_vfs_status (*open)(
        inferenceos_vfs_mount *mount,
        inferenceos_vfs_node *current_directory,
        inferenceos_vfs_path path,
        inferenceos_u32 mode,
        inferenceos_vfs_file **file
    );
    inferenceos_vfs_status (*close)(inferenceos_vfs_file *file);
    inferenceos_vfs_status (*read)(
        inferenceos_vfs_file *file,
        void *destination,
        inferenceos_size requested,
        inferenceos_size *transferred
    );
    inferenceos_vfs_status (*write)(
        inferenceos_vfs_file *file,
        const void *source,
        inferenceos_size requested,
        inferenceos_size *transferred
    );
    inferenceos_vfs_status (*seek)(
        inferenceos_vfs_file *file,
        inferenceos_i64 offset,
        inferenceos_vfs_seek_origin origin,
        inferenceos_u64 *new_offset
    );
    inferenceos_vfs_status (*open_directory)(
        inferenceos_vfs_mount *mount,
        inferenceos_vfs_node *current_directory,
        inferenceos_vfs_path path,
        inferenceos_vfs_directory **directory
    );
    inferenceos_vfs_status (*list_directory)(
        inferenceos_vfs_directory *directory,
        inferenceos_vfs_directory_entry *entry
    );
    inferenceos_vfs_status (*close_directory)(
        inferenceos_vfs_directory *directory
    );
    inferenceos_vfs_status (*create_directory)(
        inferenceos_vfs_mount *mount,
        inferenceos_vfs_node *current_directory,
        inferenceos_vfs_path path
    );
    inferenceos_vfs_status (*remove)(
        inferenceos_vfs_mount *mount,
        inferenceos_vfs_node *current_directory,
        inferenceos_vfs_path path
    );
    inferenceos_vfs_status (*rename)(
        inferenceos_vfs_mount *mount,
        inferenceos_vfs_node *current_directory,
        inferenceos_vfs_path source,
        inferenceos_vfs_path destination
    );
    inferenceos_vfs_status (*metadata_query)(
        const inferenceos_vfs_node *node,
        inferenceos_vfs_metadata *metadata
    );
} inferenceos_vfs_operations;

typedef struct inferenceos_vfs_filesystem {
    const char *name;
    const inferenceos_vfs_operations *operations;
    void *filesystem_context;
} inferenceos_vfs_filesystem;

static inline bool inferenceos_vfs_status_is_success(
    inferenceos_vfs_status status
)
{
    return status == INFERENCEOS_VFS_STATUS_OK;
}

static inline bool inferenceos_vfs_mount_is_writable(
    inferenceos_vfs_mount_state state
)
{
    return state == INFERENCEOS_VFS_MOUNT_CLEAN_WRITABLE;
}

#endif
