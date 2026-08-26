#include <inferenceos/fs/registry.h>
#include <inferenceos/fs/transaction.h>

static ios_status validate_transaction(const struct ios_fs_transaction *transaction)
{
    if (transaction == NULL || transaction->operations.persist_content == NULL
        || transaction->operations.persist_allocation == NULL
        || transaction->operations.persist_primary == NULL
        || transaction->operations.persist_companion == NULL
        || transaction->operations.persist_deleted_pair == NULL
        || transaction->operations.release_allocation == NULL
        || transaction->operations.barrier == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    return transaction->writable ? IOS_OK : IOS_ERROR(IOS_E_READ_ONLY);
}

static ios_status persist_companion(
    struct ios_fs_transaction *transaction,
    const ios_u8 name[IOS_FS_NAME_SIZE],
    bool committed
)
{
    struct ios_fs_companion_disk companion;
    ios_status status = ios_fs_companion_encode(name, committed, &companion);
    return IOS_FAILED(status) ? status
                              : transaction->operations.persist_companion(
                                    transaction->context, &companion
                                );
}

static void refresh_registry_after_commit(
    struct ios_fs_transaction *transaction,
    const struct ios_fs_primary_disk *primary
)
{
    struct ios_fs_companion_disk companion;
    ios_size record_index;
    if (transaction->registry == NULL) return;
    if (IOS_FAILED(ios_fs_companion_encode(primary->name, true, &companion))) return;
    (void)ios_fs_registry_refresh(
        transaction->registry,
        &companion,
        primary,
        transaction->registry_directory_cluster,
        transaction->registry_primary_slot,
        &record_index
    );
}

ios_status ios_fs_transaction_initialize(
    struct ios_fs_transaction *transaction,
    void *context,
    const struct ios_fs_transaction_operations *operations,
    bool writable
)
{
    if (transaction == NULL || operations == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    transaction->context = context;
    transaction->operations = *operations;
    transaction->registry = NULL;
    transaction->registry_directory_cluster = 0;
    transaction->registry_primary_slot = 0;
    transaction->writable = writable;
    {
        ios_status status = validate_transaction(transaction);
        return status == IOS_ERROR(IOS_E_READ_ONLY) ? IOS_OK : status;
    }
}

ios_status ios_fs_transaction_attach_registry(
    struct ios_fs_transaction *transaction,
    struct ios_fs_registry *registry,
    ios_u32 directory_cluster,
    ios_u16 primary_slot
)
{
    if (transaction == NULL || registry == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    transaction->registry = registry;
    transaction->registry_directory_cluster = directory_cluster;
    transaction->registry_primary_slot = primary_slot;
    return IOS_OK;
}

ios_status ios_fs_transaction_save(
    struct ios_fs_transaction *transaction,
    const struct ios_fs_primary *primary,
    const void *content,
    ios_size content_length
)
{
    struct ios_fs_primary_disk primary_disk;
    ios_status status = validate_transaction(transaction);
    if (IOS_FAILED(status)) return status;
    if (primary == NULL || (content == NULL && content_length != 0)
        || content_length != primary->file_size) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    status = ios_fs_primary_encode(primary, &primary_disk);
    if (IOS_FAILED(status)) return status;
    status = persist_companion(transaction, primary->name, false);
    if (IOS_FAILED(status)) return status;
    status = transaction->operations.barrier(transaction->context);
    if (IOS_FAILED(status)) return status;
    status = transaction->operations.persist_content(
        transaction->context, content, content_length
    );
    if (IOS_FAILED(status)) return status;
    status = transaction->operations.barrier(transaction->context);
    if (IOS_FAILED(status)) return status;
    status = transaction->operations.persist_allocation(transaction->context);
    if (IOS_FAILED(status)) return status;
    status = transaction->operations.barrier(transaction->context);
    if (IOS_FAILED(status)) return status;
    status = transaction->operations.persist_primary(transaction->context, &primary_disk);
    if (IOS_FAILED(status)) return status;
    status = persist_companion(transaction, primary->name, true);
    if (IOS_FAILED(status)) return status;
    status = transaction->operations.barrier(transaction->context);
    if (IOS_FAILED(status)) return status;
    refresh_registry_after_commit(transaction, &primary_disk);
    return IOS_OK;
}

ios_status ios_fs_transaction_create(
    struct ios_fs_transaction *transaction,
    const struct ios_fs_primary *primary,
    const void *content,
    ios_size content_length
)
{
    return ios_fs_transaction_save(transaction, primary, content, content_length);
}

ios_status ios_fs_transaction_rename(
    struct ios_fs_transaction *transaction,
    const struct ios_fs_companion_disk *current_companion,
    const struct ios_fs_primary_disk *current_primary,
    const ios_u8 new_name[IOS_FS_NAME_SIZE],
    bool destination_exists
)
{
    struct ios_fs_primary primary;
    struct ios_fs_primary_disk renamed_disk;
    ios_status status = validate_transaction(transaction);
    if (IOS_FAILED(status)) return status;
    if (current_companion == NULL || current_primary == NULL || new_name == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (destination_exists) return IOS_ERROR(IOS_E_ALREADY_EXISTS);
    status = ios_fs_record_pair_validate(current_companion, current_primary);
    if (IOS_FAILED(status)) return status;
    status = ios_fs_primary_decode(current_primary, &primary);
    if (IOS_FAILED(status)) return status;
    for (ios_size index = 0; index < IOS_FS_NAME_SIZE; ++index) primary.name[index] = new_name[index];
    status = ios_fs_primary_encode(&primary, &renamed_disk);
    if (IOS_FAILED(status)) return status;
    status = persist_companion(transaction, current_primary->name, false);
    if (IOS_FAILED(status)) return status;
    status = transaction->operations.barrier(transaction->context);
    if (IOS_FAILED(status)) return status;
    status = transaction->operations.persist_primary(transaction->context, &renamed_disk);
    if (IOS_FAILED(status)) return status;
    status = persist_companion(transaction, primary.name, true);
    if (IOS_FAILED(status)) return status;
    status = transaction->operations.barrier(transaction->context);
    if (IOS_FAILED(status)) return status;
    refresh_registry_after_commit(transaction, &renamed_disk);
    return IOS_OK;
}

ios_status ios_fs_transaction_delete(
    struct ios_fs_transaction *transaction,
    const struct ios_fs_companion_disk *current_companion,
    const struct ios_fs_primary_disk *current_primary
)
{
    struct ios_fs_primary primary;
    ios_status status = validate_transaction(transaction);
    if (IOS_FAILED(status)) return status;
    status = ios_fs_record_pair_validate(current_companion, current_primary);
    if (IOS_FAILED(status)) return status;
    status = ios_fs_primary_decode(current_primary, &primary);
    if (IOS_FAILED(status)) return status;
    status = persist_companion(transaction, primary.name, false);
    if (IOS_FAILED(status)) return status;
    status = transaction->operations.barrier(transaction->context);
    if (IOS_FAILED(status)) return status;
    status = transaction->operations.persist_deleted_pair(transaction->context);
    if (IOS_FAILED(status)) return status;
    status = transaction->operations.barrier(transaction->context);
    if (IOS_FAILED(status)) return status;
    if (primary.first_cluster != 0) {
        status = transaction->operations.release_allocation(
            transaction->context, primary.first_cluster
        );
        if (IOS_FAILED(status)) return status;
    }
    return transaction->operations.barrier(transaction->context);
}
