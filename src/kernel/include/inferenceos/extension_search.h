#ifndef INFERENCEOS_EXTENSION_SEARCH_H
#define INFERENCEOS_EXTENSION_SEARCH_H

#include <inferenceos/process.h>
#include <inferenceos/vfs.h>

enum {
    IOS_EXTENSION_SEARCH_ABI_VERSION = 1,
    IOS_EXTENSION_SEARCH_INPUT_CAPACITY = 4,
    IOS_EXTENSION_SEARCH_RESULT_CAPACITY = IOS_VFS_SEARCH_RESULT_CAPACITY,
    IOS_EXTENSION_SEARCH_REPLY_TRUNCATED = UINT32_C(1) << 0
};

struct ios_extension_search_request {
    ios_u16 size;
    ios_u16 version;
    ios_u32 flags;
    ios_u16 extension_length;
    ios_u16 reserved;
    char extension[IOS_EXTENSION_SEARCH_INPUT_CAPACITY];
};

struct ios_extension_search_result {
    ios_u64 object_identity;
    ios_u16 location_length;
    ios_u16 reserved;
    char location[IOS_VFS_PATH_CAPACITY];
};

struct ios_extension_search_reply {
    ios_u16 size;
    ios_u16 version;
    ios_u32 flags;
    ios_status status;
    ios_u32 item_count;
    ios_u32 reserved;
    struct ios_extension_search_result entries[IOS_EXTENSION_SEARCH_RESULT_CAPACITY];
};

struct ios_extension_search_service {
    struct ios_vfs_mount_registry *mount_registry;
};

IOS_STATIC_ASSERT(
    sizeof(struct ios_extension_search_request) == 16,
    "extension search request ABI size"
);
IOS_STATIC_ASSERT(
    offsetof(struct ios_extension_search_request, extension) == 12,
    "extension search request payload offset"
);
IOS_STATIC_ASSERT(
    sizeof(struct ios_extension_search_result) == 272,
    "extension search result ABI size"
);
IOS_STATIC_ASSERT(
    offsetof(struct ios_extension_search_reply, entries) == 24,
    "extension search reply entries offset"
);
IOS_STATIC_ASSERT(
    sizeof(struct ios_extension_search_reply) == 4376,
    "extension search reply ABI size"
);

ios_status ios_extension_search_service_initialize(
    struct ios_extension_search_service *service,
    struct ios_vfs_mount_registry *mount_registry
);
ios_status ios_extension_search_dispatch(
    struct ios_extension_search_service *service,
    const struct ios_process *caller,
    const struct ios_extension_search_request *request,
    struct ios_extension_search_reply *reply
);

#endif
