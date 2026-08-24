#include <inferenceos/custom_test.h>

#include <inferenceos/runtime.h>

static ios_status exchange_probe(
    struct ios_custom_test_application *application,
    enum ios_shell_operation operation,
    const struct ios_shell_file_view_request *request)
{
    struct ios_shell_dispatch_result result;
    if (application->next_request_id == 0) return IOS_ERROR(IOS_E_OVERFLOW);
    return ios_shell_service_exchange(
        application->shell, application->process, application->shell_channel,
        application->next_request_id++, operation, request, sizeof(*request), &result);
}

void ios_custom_test_disconnect(struct ios_custom_test_application *application)
{
    if (application == NULL || application->process == NULL) return;
    if (application->connected) {
        (void)handle_table_close(&application->process->handles, application->shell_channel);
    }
    if (application->adapter_handle != IOS_INVALID_HANDLE) {
        (void)handle_table_close(&application->process->handles, application->adapter_handle);
    }
    application->shell_channel = IOS_INVALID_HANDLE;
    application->adapter_handle = IOS_INVALID_HANDLE;
    application->connected = false;
}

ios_status ios_custom_test_initialize(
    struct ios_custom_test_application *application,
    struct ios_process *process,
    struct ios_shell_service *shell,
    struct ios_proprietary_adapter_service *adapters,
    ios_u64 adapter_identity,
    ios_handle content_handle,
    ios_handle directory_handle)
{
    ios_status status;
    if (application == NULL || process == NULL || shell == NULL || adapters == NULL
        || process->process_id == 0 || process->application_identity == 0
        || adapter_identity == 0 || content_handle == IOS_INVALID_HANDLE
        || directory_handle == IOS_INVALID_HANDLE) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    memset(application, 0, sizeof(*application));
    application->process = process;
    application->shell = shell;
    application->adapters = adapters;
    application->content_handle = content_handle;
    application->directory_handle = directory_handle;
    application->next_request_id = 1;
    status = ios_proprietary_adapter_authorize(
        adapters, process, adapter_identity, &application->adapter_handle);
    if (IOS_FAILED(status)) return status;
    status = ipc_service_connect(process, IOS_SERVICE_SHELL, &application->shell_channel);
    if (IOS_FAILED(status)) {
        (void)handle_table_close(&process->handles, application->adapter_handle);
        application->adapter_handle = IOS_INVALID_HANDLE;
        return status;
    }
    application->connected = true;
    return IOS_OK;
}

ios_status ios_custom_test_run(
    struct ios_custom_test_application *application,
    ios_u32 adapter_operation,
    const void *input,
    ios_size input_size)
{
    struct ios_shell_file_view_request probe;
    struct ios_proprietary_adapter_request request;
    ios_status status;
    if (application == NULL || !application->connected || application->process == NULL
        || application->shell == NULL || application->adapters == NULL
        || (input == NULL && input_size != 0)
        || input_size > IOS_PROPRIETARY_ADAPTER_PAYLOAD_CAPACITY) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    probe = (struct ios_shell_file_view_request){
        .size = sizeof(probe), .version = IOS_SHELL_PROTOCOL_VERSION,
        .flags = UINT32_C(0x545854), .directory_handle = application->directory_handle,
        .maximum_items = 1
    };
    application->raw_extension_probe_status = exchange_probe(
        application, IOS_SHELL_DIRECTORY_VIEW, &probe);
    probe.flags = 0;
    probe.reserved32 = UINT32_C(0x45373731);
    application->raw_hash_probe_status = exchange_probe(
        application, IOS_SHELL_DIRECTORY_VIEW, &probe);
    probe.reserved32 = 0;
    probe.type_icon_capability = UINT64_C(0xfeedface00010001);
    application->arbitrary_type_probe_status = exchange_probe(
        application, IOS_SHELL_TYPE_VIEW, &probe);
    if (IOS_SUCCEEDED(application->raw_extension_probe_status)
        || IOS_SUCCEEDED(application->raw_hash_probe_status)
        || IOS_SUCCEEDED(application->arbitrary_type_probe_status)) {
        return IOS_ERROR(IOS_E_ACCESS_DENIED);
    }
    request = (struct ios_proprietary_adapter_request){
        .size = sizeof(request),
        .version = IOS_PROPRIETARY_ADAPTER_VERSION,
        .adapter_handle = application->adapter_handle,
        .content_handle = application->content_handle,
        .operation = adapter_operation,
        .input_size = (ios_u16)input_size
    };
    if (input_size != 0) memcpy(request.input, input, input_size);
    status = ios_proprietary_adapter_invoke(
        application->adapters, application->process, &request,
        &application->adapter_reply);
    return status;
}
