#include <inferenceos/type_capability.h>

#include <inferenceos/runtime.h>

static void retain_capability(void *object)
{
    struct ios_process_type_capability *capability = object;
    if (capability != NULL && capability->occupied
        && capability->reference_count != UINT32_MAX) {
        ++capability->reference_count;
    }
}

static void release_capability(void *object)
{
    struct ios_process_type_capability *capability = object;
    struct ios_type_capability_service *service;
    if (capability == NULL || !capability->occupied || capability->reference_count == 0) {
        return;
    }
    --capability->reference_count;
    if (capability->reference_count != 0) return;
    service = capability->service;
    memset(capability, 0, sizeof(*capability));
    if (service != NULL && service->active_count != 0) --service->active_count;
}

ios_status ios_type_capability_service_initialize(
    struct ios_type_capability_service *service,
    const struct ios_application_binding_registry *bindings,
    const struct ios_type_catalog *catalog)
{
    if (service == NULL || bindings == NULL || catalog == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    memset(service, 0, sizeof(*service));
    service->bindings = bindings;
    service->catalog = catalog;
    return IOS_OK;
}

ios_status ios_type_capability_mint(
    struct ios_type_capability_service *service,
    struct ios_process *process,
    ios_type_icon_capability catalog_capability,
    ios_handle *handle)
{
    struct ios_process_type_capability *capability = NULL;
    ios_u64 internal_type_identity;
    ios_status status;

    if (service == NULL || service->bindings == NULL || service->catalog == NULL
        || process == NULL || process->process_id == 0
        || process->application_identity == 0 || handle == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *handle = IOS_INVALID_HANDLE;
    status = ios_type_catalog_resolve_identity(
        service->catalog, catalog_capability, &internal_type_identity);
    if (IOS_FAILED(status)) return status;
    status = ios_application_bindings_authorize(
        service->bindings, process->application_identity, internal_type_identity);
    if (IOS_FAILED(status)) return status;
    for (ios_size index = 0; index < IOS_PROCESS_TYPE_CAPABILITY_CAPACITY; ++index) {
        if (!service->capabilities[index].occupied) {
            capability = &service->capabilities[index];
            break;
        }
    }
    if (capability == NULL) return IOS_ERROR(IOS_E_NO_SPACE);
    *capability = (struct ios_process_type_capability){
        .service = service,
        .owner_process_id = process->process_id,
        .owner_application_identity = process->application_identity,
        .internal_type_identity = internal_type_identity,
        .catalog_capability = catalog_capability,
        .reference_count = 1,
        .occupied = true
    };
    status = handle_table_insert(
        &process->handles, capability, IOS_OBJECT_TYPE_CAPABILITY,
        IOS_RIGHT_ENUMERATE, retain_capability, release_capability, handle);
    if (IOS_FAILED(status)) {
        memset(capability, 0, sizeof(*capability));
        return status;
    }
    ++service->active_count;
    return IOS_OK;
}

ios_status ios_type_capability_resolve(
    const struct ios_type_capability_service *service,
    const struct ios_process *process,
    ios_handle handle,
    ios_u64 *internal_type_identity,
    ios_type_icon_capability *catalog_capability)
{
    struct ios_process_type_capability *capability;
    void *object = NULL;
    ios_status status;

    if (service == NULL || process == NULL || internal_type_identity == NULL
        || catalog_capability == NULL || process->process_id == 0
        || process->application_identity == 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *internal_type_identity = 0;
    *catalog_capability = IOS_INVALID_TYPE_ICON_CAPABILITY;
    status = handle_table_resolve(
        &process->handles, handle, IOS_OBJECT_TYPE_CAPABILITY,
        IOS_RIGHT_ENUMERATE, &object);
    if (IOS_FAILED(status)) return status;
    capability = object;
    if (!capability->occupied || capability->service != service
        || capability->owner_process_id != process->process_id
        || capability->owner_application_identity != process->application_identity) {
        return IOS_ERROR(IOS_E_ACCESS_DENIED);
    }
    *internal_type_identity = capability->internal_type_identity;
    *catalog_capability = capability->catalog_capability;
    return IOS_OK;
}

ios_status ios_type_capability_authorize(
    const struct ios_type_capability_service *service,
    const struct ios_process *process,
    ios_handle handle,
    ios_u64 required_internal_type_identity)
{
    ios_u64 internal_type_identity;
    ios_type_icon_capability catalog_capability;
    ios_status status;
    if (required_internal_type_identity == 0) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    status = ios_type_capability_resolve(
        service, process, handle, &internal_type_identity, &catalog_capability);
    if (IOS_FAILED(status)) return status;
    return internal_type_identity == required_internal_type_identity
        ? IOS_OK : IOS_ERROR(IOS_E_ACCESS_DENIED);
}

ios_u32 ios_type_capability_active_count(
    const struct ios_type_capability_service *service)
{
    return service == NULL ? 0 : service->active_count;
}
