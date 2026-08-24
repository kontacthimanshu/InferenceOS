#ifndef INFERENCEOS_FS_TRANSACTION_H
#define INFERENCEOS_FS_TRANSACTION_H

#include <inferenceos/fs/records.h>

struct ios_fs_transaction_operations {
    ios_status (*persist_content)(void *context, const void *bytes, ios_size length);
    ios_status (*persist_allocation)(void *context);
    ios_status (*persist_primary)(void *context, const struct ios_fs_primary_disk *primary);
    ios_status (*persist_companion)(void *context, const struct ios_fs_companion_disk *companion);
    ios_status (*persist_deleted_pair)(void *context);
    ios_status (*release_allocation)(void *context, ios_u32 first_cluster);
    ios_status (*barrier)(void *context);
};

struct ios_fs_transaction {
    void *context;
    struct ios_fs_transaction_operations operations;
    bool writable;
};

ios_status ios_fs_transaction_initialize(
    struct ios_fs_transaction *transaction,
    void *context,
    const struct ios_fs_transaction_operations *operations,
    bool writable
);
ios_status ios_fs_transaction_create(
    struct ios_fs_transaction *transaction,
    const struct ios_fs_primary *primary,
    const void *content,
    ios_size content_length
);
ios_status ios_fs_transaction_save(
    struct ios_fs_transaction *transaction,
    const struct ios_fs_primary *primary,
    const void *content,
    ios_size content_length
);
ios_status ios_fs_transaction_rename(
    struct ios_fs_transaction *transaction,
    const struct ios_fs_companion_disk *current_companion,
    const struct ios_fs_primary_disk *current_primary,
    const ios_u8 new_name[IOS_FS_NAME_SIZE],
    bool destination_exists
);
ios_status ios_fs_transaction_delete(
    struct ios_fs_transaction *transaction,
    const struct ios_fs_companion_disk *current_companion,
    const struct ios_fs_primary_disk *current_primary
);

#endif
