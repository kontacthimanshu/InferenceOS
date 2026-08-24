#ifndef INFERENCEOS_HANDLE_TABLE_H
#define INFERENCEOS_HANDLE_TABLE_H

#include <inferenceos/base.h>
#include <inferenceos/errors.h>

enum {
    IOS_HANDLE_TABLE_CAPACITY = 256
};

typedef ios_u64 ios_handle;

#define IOS_INVALID_HANDLE UINT64_C(0)

enum ios_object_kind {
    IOS_OBJECT_NONE = 0,
    IOS_OBJECT_PROCESS = 1,
    IOS_OBJECT_FILE = 2,
    IOS_OBJECT_DIRECTORY = 3,
    IOS_OBJECT_IPC_ENDPOINT = 4,
    IOS_OBJECT_WINDOW = 5,
    IOS_OBJECT_TYPE_CAPABILITY = 6,
    IOS_OBJECT_DIAGNOSTIC_CAPABILITY = 7,
    IOS_OBJECT_CONTENT = 8,
    IOS_OBJECT_ADAPTER_CAPABILITY = 9
};

enum ios_handle_right {
    IOS_RIGHT_READ = UINT64_C(1) << 0,
    IOS_RIGHT_WRITE = UINT64_C(1) << 1,
    IOS_RIGHT_WAIT = UINT64_C(1) << 2,
    IOS_RIGHT_SIGNAL = UINT64_C(1) << 3,
    IOS_RIGHT_DUPLICATE = UINT64_C(1) << 4,
    IOS_RIGHT_TRANSFER = UINT64_C(1) << 5,
    IOS_RIGHT_ENUMERATE = UINT64_C(1) << 6,
    IOS_RIGHT_DIAGNOSTIC = UINT64_C(1) << 7,
    IOS_RIGHT_ADMINISTER = UINT64_C(1) << 8
};

#define IOS_RIGHT_ALL UINT64_C(0x1ff)

typedef void (*ios_object_retain_function)(void *object);
typedef void (*ios_object_release_function)(void *object);

struct ios_handle_entry {
    void *object;
    ios_u64 rights;
    ios_u64 generation;
    enum ios_object_kind kind;
    ios_object_retain_function retain;
    ios_object_release_function release;
};

struct ios_handle_table {
    struct ios_handle_entry entries[IOS_HANDLE_TABLE_CAPACITY];
    ios_u32 owner_tag;
    ios_u32 open_count;
};

ios_status handle_table_initialize(struct ios_handle_table *table, ios_u64 owner_identity);
ios_status handle_table_insert(
    struct ios_handle_table *table,
    void *object,
    enum ios_object_kind kind,
    ios_u64 rights,
    ios_object_retain_function retain,
    ios_object_release_function release,
    ios_handle *handle
);
ios_status handle_table_resolve(
    const struct ios_handle_table *table,
    ios_handle handle,
    enum ios_object_kind expected_kind,
    ios_u64 required_rights,
    void **object
);
ios_status handle_table_duplicate(
    struct ios_handle_table *table,
    ios_handle source,
    ios_u64 reduced_rights,
    ios_handle *duplicate
);
ios_status handle_table_transfer(
    struct ios_handle_table *source_table,
    struct ios_handle_table *destination_table,
    ios_handle source,
    ios_u64 reduced_rights,
    bool close_source,
    ios_handle *transferred
);
ios_status handle_table_close(struct ios_handle_table *table, ios_handle handle);
void handle_table_close_all(struct ios_handle_table *table);
ios_u32 handle_table_open_count(const struct ios_handle_table *table);

#endif
