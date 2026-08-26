#include <inferenceos/fs/registry_diagnostics.h>

#include <inferenceos/runtime.h>

static bool all_zero(const ios_u8 *bytes, ios_size length)
{
    for (ios_size index = 0; index < length; ++index) {
        if (bytes[index] != 0) return false;
    }
    return true;
}

static bool empty_record(const struct ios_fs_registry_record_disk *record)
{
    return all_zero((const ios_u8 *)record, sizeof(*record));
}

static ios_status authorize_registry_diagnostic(
    const struct ios_process *caller,
    ios_handle authority_handle
)
{
    const struct ios_fs_diagnostic_authority *authority;
    void *object = NULL;
    ios_status status = handle_table_resolve(
        &caller->handles,
        authority_handle,
        IOS_OBJECT_DIAGNOSTIC_CAPABILITY,
        IOS_RIGHT_DIAGNOSTIC,
        &object
    );
    if (IOS_FAILED(status)) return status;
    authority = object;
    if (authority->owner_process_id != caller->process_id
        || authority->owner_application_identity != caller->application_identity
        || (authority->scope & IOS_FS_DIAGNOSTIC_SCOPE_REGISTRY) == 0) {
        return IOS_ERROR(IOS_E_ACCESS_DENIED);
    }
    return IOS_OK;
}

static void fill_record_diagnostic(
    ios_size index,
    const struct ios_fs_registry_record_disk *disk,
    struct ios_fs_registry_diagnostic_record *output
)
{
    struct ios_fs_registry_record record;
    ios_status status;
    memset(output, 0, sizeof(*output));
    output->record_index = (ios_u32)index;
    status = ios_fs_registry_record_decode(disk, &record);
    output->validation_status = status;
    if (IOS_FAILED(status)) return;
    output->active = record.active;
    output->extension_length = record.extension_length;
    memcpy(
        output->canonical_extension,
        record.canonical_extension,
        sizeof(output->canonical_extension)
    );
    memcpy(
        output->extension_hash_text,
        record.extension_hash_text,
        sizeof(output->extension_hash_text)
    );
    output->last_directory_cluster = record.last_directory_cluster;
    output->last_directory_slot = record.last_directory_slot;
    output->update_generation = record.update_generation;
}

ios_status ios_fs_registry_diagnostic_service_initialize(
    struct ios_fs_registry_diagnostic_service *service,
    struct ios_fs_registry *registry
)
{
    if (service == NULL || registry == NULL || registry->records == NULL
        || registry->capacity == 0 || registry->capacity > UINT32_MAX) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    service->registry = registry;
    return IOS_OK;
}

ios_status ios_fs_registry_diagnostic_dispatch(
    const struct ios_fs_registry_diagnostic_service *service,
    const struct ios_process *caller,
    const struct ios_fs_registry_diagnostic_request *request,
    struct ios_fs_registry_diagnostic_reply *reply
)
{
    struct ios_fs_registry *registry;
    ios_u32 active_count = 0;
    bool invalid_record = false;
    ios_status status;
    if (reply != NULL) memset(reply, 0, sizeof(*reply));
    if (service == NULL || caller == NULL || request == NULL || reply == NULL
        || service->registry == NULL || service->registry->records == NULL
        || caller->process_id == 0 || caller->application_identity == 0
        || request->size != sizeof(*request)
        || request->version != IOS_FS_REGISTRY_DIAGNOSTIC_ABI_VERSION
        || request->reserved != 0 || request->maximum_records == 0
        || request->maximum_records > IOS_FS_REGISTRY_DIAGNOSTIC_REPLY_CAPACITY
        || request->first_record > service->registry->capacity) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    status = authorize_registry_diagnostic(caller, request->authority);
    if (IOS_FAILED(status)) return status;

    registry = service->registry;
    reply->size = sizeof(*reply);
    reply->version = IOS_FS_REGISTRY_DIAGNOSTIC_ABI_VERSION;
    reply->enabled = registry->enabled;
    reply->health = registry->health;
    for (ios_size index = 0; index < registry->capacity; ++index) {
        struct ios_fs_registry_record decoded;
        if (empty_record(&registry->records[index])) continue;
        status = ios_fs_registry_record_decode(&registry->records[index], &decoded);
        if (IOS_FAILED(status)) invalid_record = true;
        else if (decoded.active) ++active_count;
        if (index < request->first_record
            || reply->record_count == request->maximum_records) {
            continue;
        }
        fill_record_diagnostic(
            index,
            &registry->records[index],
            &reply->records[reply->record_count++]
        );
    }
    if (registry->enabled && invalid_record) {
        reply->health = IOS_FS_REGISTRY_CORRUPT;
        reply->active_type_count = 0;
    } else if (registry->enabled) {
        reply->active_type_count = active_count;
    }
    return IOS_OK;
}

ios_status ios_fs_registry_control_dispatch(
    struct ios_fs_registry_diagnostic_service *service,
    const struct ios_process *caller,
    const struct ios_fs_registry_control_request *request,
    struct ios_fs_registry_control_reply *reply
)
{
    ios_status status;
    if (reply != NULL) memset(reply, 0, sizeof(*reply));
    if (service == NULL || caller == NULL || request == NULL || reply == NULL
        || service->registry == NULL || service->registry->records == NULL
        || caller->process_id == 0 || caller->application_identity == 0
        || request->size != sizeof(*request)
        || request->version != IOS_FS_REGISTRY_DIAGNOSTIC_ABI_VERSION
        || request->enabled > 1
        || !all_zero(request->reserved0, sizeof(request->reserved0))
        || request->reserved1 != 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    status = authorize_registry_diagnostic(caller, request->authority);
    if (IOS_FAILED(status)) return status;
    status = ios_fs_registry_initialize(
        service->registry,
        service->registry->records,
        service->registry->capacity,
        request->enabled != 0
    );
    if (IOS_FAILED(status)) return status;
    reply->size = sizeof(*reply);
    reply->version = IOS_FS_REGISTRY_DIAGNOSTIC_ABI_VERSION;
    reply->enabled = service->registry->enabled;
    reply->health = service->registry->health;
    return IOS_OK;
}
