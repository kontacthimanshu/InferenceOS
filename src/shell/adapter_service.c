#include <inferenceos/proprietary_adapter.h>

#include <inferenceos/runtime.h>

#define IOS_ADAPTER_CONTENT_RIGHTS (IOS_RIGHT_READ | IOS_RIGHT_WRITE)

static void retain_capability(void *object)
{
    struct ios_proprietary_adapter_capability *capability = object;
    if (capability != NULL && capability->occupied
        && capability->reference_count != UINT32_MAX) ++capability->reference_count;
}

static void release_capability(void *object)
{
    struct ios_proprietary_adapter_capability *capability = object;
    struct ios_proprietary_adapter_service *service;
    if (capability == NULL || !capability->occupied || capability->reference_count == 0) return;
    --capability->reference_count;
    if (capability->reference_count != 0) return;
    service = capability->service;
    memset(capability, 0, sizeof(*capability));
    if (service != NULL && service->active_capability_count != 0) {
        --service->active_capability_count;
    }
}

ios_status ios_proprietary_adapter_service_initialize(
    struct ios_proprietary_adapter_service *service)
{
    if (service == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    memset(service, 0, sizeof(*service));
    service->initialized = true;
    return IOS_OK;
}

ios_status ios_proprietary_adapter_register(
    struct ios_proprietary_adapter_service *service,
    struct ios_process *proprietary_process,
    const struct ios_proprietary_adapter_descriptor *descriptor)
{
    struct ios_proprietary_adapter_registration *registration = NULL;
    if (service == NULL || !service->initialized || proprietary_process == NULL
        || descriptor == NULL || descriptor->size != sizeof(*descriptor)
        || descriptor->version != IOS_PROPRIETARY_ADAPTER_VERSION || descriptor->flags != 0
        || descriptor->adapter_identity == 0
        || descriptor->proprietary_application_identity == 0
        || descriptor->authorized_caller_identity == 0
        || descriptor->allowed_operation_mask == 0
        || descriptor->required_content_rights == 0
        || (descriptor->required_content_rights & ~IOS_ADAPTER_CONTENT_RIGHTS) != 0
        || descriptor->invoke == NULL || proprietary_process->process_id == 0
        || proprietary_process->application_identity
            != descriptor->proprietary_application_identity) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    for (ios_size index = 0; index < IOS_PROPRIETARY_ADAPTER_REGISTRATION_CAPACITY; ++index) {
        if (service->registrations[index].occupied
            && service->registrations[index].descriptor.adapter_identity
                == descriptor->adapter_identity) return IOS_ERROR(IOS_E_ALREADY_EXISTS);
        if (!service->registrations[index].occupied && registration == NULL) {
            registration = &service->registrations[index];
        }
    }
    if (registration == NULL) return IOS_ERROR(IOS_E_NO_SPACE);
    *registration = (struct ios_proprietary_adapter_registration){
        .proprietary_process = proprietary_process,
        .descriptor = *descriptor,
        .occupied = true
    };
    ++service->registration_count;
    return IOS_OK;
}

ios_status ios_proprietary_adapter_authorize(
    struct ios_proprietary_adapter_service *service,
    struct ios_process *caller_process,
    ios_u64 adapter_identity,
    ios_handle *adapter_handle)
{
    struct ios_proprietary_adapter_registration *registration = NULL;
    struct ios_proprietary_adapter_capability *capability = NULL;
    ios_status status;
    if (service == NULL || !service->initialized || caller_process == NULL
        || caller_process->process_id == 0 || caller_process->application_identity == 0
        || adapter_identity == 0 || adapter_handle == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *adapter_handle = IOS_INVALID_HANDLE;
    for (ios_size index = 0; index < IOS_PROPRIETARY_ADAPTER_REGISTRATION_CAPACITY; ++index) {
        if (service->registrations[index].occupied
            && service->registrations[index].descriptor.adapter_identity == adapter_identity) {
            registration = &service->registrations[index];
            break;
        }
    }
    if (registration == NULL) return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    if (registration->descriptor.authorized_caller_identity
        != caller_process->application_identity) return IOS_ERROR(IOS_E_ACCESS_DENIED);
    for (ios_size index = 0; index < IOS_PROPRIETARY_ADAPTER_CAPABILITY_CAPACITY; ++index) {
        if (!service->capabilities[index].occupied) {
            capability = &service->capabilities[index];
            break;
        }
    }
    if (capability == NULL) return IOS_ERROR(IOS_E_NO_SPACE);
    *capability = (struct ios_proprietary_adapter_capability){
        .service = service,
        .registration = registration,
        .owner_process_id = caller_process->process_id,
        .owner_application_identity = caller_process->application_identity,
        .reference_count = 1,
        .occupied = true
    };
    status = handle_table_insert(
        &caller_process->handles, capability, IOS_OBJECT_ADAPTER_CAPABILITY,
        IOS_RIGHT_SIGNAL, retain_capability, release_capability, adapter_handle);
    if (IOS_FAILED(status)) {
        memset(capability, 0, sizeof(*capability));
        return status;
    }
    ++service->active_capability_count;
    return IOS_OK;
}

static ios_status resolve_capability(
    struct ios_proprietary_adapter_service *service,
    struct ios_process *caller_process,
    ios_handle handle,
    struct ios_proprietary_adapter_capability **resolved)
{
    void *object = NULL;
    ios_status status = handle_table_resolve(
        &caller_process->handles, handle, IOS_OBJECT_ADAPTER_CAPABILITY,
        IOS_RIGHT_SIGNAL, &object);
    if (IOS_FAILED(status)) return status;
    *resolved = object;
    if (!(*resolved)->occupied || (*resolved)->service != service
        || (*resolved)->registration == NULL || !(*resolved)->registration->occupied
        || (*resolved)->owner_process_id != caller_process->process_id
        || (*resolved)->owner_application_identity != caller_process->application_identity) {
        return IOS_ERROR(IOS_E_ACCESS_DENIED);
    }
    return IOS_OK;
}

ios_status ios_proprietary_adapter_invoke(
    struct ios_proprietary_adapter_service *service,
    struct ios_process *caller_process,
    const struct ios_proprietary_adapter_request *request,
    struct ios_proprietary_adapter_reply *reply)
{
    struct ios_proprietary_adapter_capability *capability = NULL;
    struct ios_proprietary_adapter_registration *registration;
    ios_handle reduced_handle = IOS_INVALID_HANDLE;
    ios_size output_size = 0;
    void *content = NULL;
    ios_status status = IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    if (reply == NULL) return status;
    memset(reply, 0, sizeof(*reply));
    reply->size = sizeof(*reply);
    reply->version = IOS_PROPRIETARY_ADAPTER_VERSION;
    if (service == NULL || !service->initialized || caller_process == NULL
        || request == NULL || caller_process->process_id == 0
        || caller_process->application_identity == 0
        || request->size != sizeof(*request)
        || request->version != IOS_PROPRIETARY_ADAPTER_VERSION || request->flags != 0
        || request->adapter_handle == IOS_INVALID_HANDLE
        || request->content_handle == IOS_INVALID_HANDLE || request->operation >= 64
        || request->input_size > IOS_PROPRIETARY_ADAPTER_PAYLOAD_CAPACITY
        || request->reserved16 != 0) {
        status = IOS_ERROR(IOS_E_PROTOCOL);
        goto done;
    }
    status = resolve_capability(
        service, caller_process, request->adapter_handle, &capability);
    if (IOS_FAILED(status)) goto done;
    registration = capability->registration;
    if (registration->proprietary_process == NULL
        || registration->proprietary_process->process_id == 0
        || registration->proprietary_process->application_identity
            != registration->descriptor.proprietary_application_identity) {
        status = IOS_ERROR(IOS_E_INVALID_STATE);
        goto done;
    }
    if ((registration->descriptor.allowed_operation_mask
         & (UINT64_C(1) << request->operation)) == 0) {
        status = IOS_ERROR(IOS_E_NOT_SUPPORTED);
        goto done;
    }
    status = handle_table_resolve(
        &caller_process->handles, request->content_handle, IOS_OBJECT_CONTENT,
        registration->descriptor.required_content_rights | IOS_RIGHT_TRANSFER, &content);
    if (IOS_FAILED(status)) goto done;
    (void)content;
    status = handle_table_transfer(
        &caller_process->handles, &registration->proprietary_process->handles,
        request->content_handle, registration->descriptor.required_content_rights,
        false, &reduced_handle);
    if (IOS_FAILED(status)) goto done;
    status = registration->descriptor.invoke(
        registration->descriptor.context, registration->proprietary_process,
        reduced_handle, request->operation, request->input, request->input_size,
        reply->output, IOS_PROPRIETARY_ADAPTER_PAYLOAD_CAPACITY, &output_size);
    (void)handle_table_close(&registration->proprietary_process->handles, reduced_handle);
    if (IOS_FAILED(status)) {
        memset(reply->output, 0, sizeof(reply->output));
        goto done;
    }
    if (output_size > IOS_PROPRIETARY_ADAPTER_PAYLOAD_CAPACITY) {
        memset(reply->output, 0, sizeof(reply->output));
        status = IOS_ERROR(IOS_E_PROTOCOL);
        goto done;
    }
    reply->output_size = (ios_u16)output_size;

done:
    reply->status = status;
    return status;
}
