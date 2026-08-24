#include <inferenceos/gui_view.h>

static const struct ios_process *resolve_system_process(
    void *context, ios_u64 process_id, ios_u64 application_identity
)
{
    (void)context;
    const ios_size count = process_count();
    for (ios_size index = 0; index < count; ++index) {
        const struct ios_process *process = process_at(index);
        if (process != NULL && process->process_id == process_id
            && process->application_identity == application_identity
            && process->state != IOS_PROCESS_EXITED) {
            return process;
        }
    }
    return NULL;
}

ios_status ios_gui_view_service_initialize(
    struct ios_gui_view_service *service,
    ios_gui_view_process_resolver resolve_process,
    void *resolver_context
)
{
    if (service == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    *service = (struct ios_gui_view_service){
        resolve_process != NULL ? resolve_process : resolve_system_process,
        resolver_context
    };
    return IOS_OK;
}

ios_status ios_gui_view_target_initialize(
    struct ios_gui_view_target *target,
    struct ios_window_manager *window_manager,
    struct ios_window_handle native_window,
    ios_u64 owner_process_id,
    ios_u64 owner_application_identity
)
{
    if (target == NULL || window_manager == NULL || native_window.generation == 0
        || native_window.slot >= IOS_WINDOW_CAPACITY || owner_process_id == 0
        || owner_process_id > UINT32_MAX || owner_application_identity == 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    const struct ios_window *window = &window_manager->windows[native_window.slot];
    if (!window->allocated || window->generation != native_window.generation) {
        return IOS_ERROR(IOS_E_BAD_HANDLE);
    }
    if (window->owner != (ios_u32)owner_process_id) {
        return IOS_ERROR(IOS_E_ACCESS_DENIED);
    }
    *target = (struct ios_gui_view_target){
        window_manager, native_window, owner_process_id, owner_application_identity, 0
    };
    return IOS_OK;
}

ios_status ios_gui_view_dispatch(
    void *context,
    ios_u64 caller_process_id,
    ios_u64 caller_application_identity,
    const struct ios_shell_gui_view_request *request,
    struct ios_shell_gui_view_reply *reply
)
{
    struct ios_gui_view_service *service = context;
    const struct ios_process *process;
    struct ios_gui_view_target *window_target;
    struct ios_gui_view_target *view_target;
    void *object;
    ios_status status;
    if (service == NULL || request == NULL || reply == NULL
        || caller_process_id == 0 || caller_application_identity == 0
        || request->window_handle == IOS_INVALID_HANDLE
        || request->view_handle == IOS_INVALID_HANDLE || request->render_sequence == 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    process = service->resolve_process(
        service->resolver_context, caller_process_id, caller_application_identity
    );
    if (process == NULL || process->process_id != caller_process_id
        || process->application_identity != caller_application_identity
        || process->state == IOS_PROCESS_UNUSED || process->state == IOS_PROCESS_EXITED) {
        return IOS_ERROR(IOS_E_ACCESS_DENIED);
    }
    status = handle_table_resolve(
        &process->handles, request->window_handle, IOS_OBJECT_WINDOW,
        IOS_RIGHT_WRITE, &object
    );
    if (IOS_FAILED(status)) return status;
    window_target = object;
    status = handle_table_resolve(
        &process->handles, request->view_handle, IOS_OBJECT_CONTENT,
        IOS_RIGHT_READ, &object
    );
    if (IOS_FAILED(status)) return status;
    view_target = object;
    if (window_target != view_target
        || window_target->owner_process_id != caller_process_id
        || window_target->owner_application_identity != caller_application_identity) {
        return IOS_ERROR(IOS_E_ACCESS_DENIED);
    }
    if (request->render_sequence <= window_target->last_render_sequence) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    status = ios_window_render_owned(
        window_target->window_manager,
        window_target->native_window,
        (ios_u32)caller_process_id
    );
    if (IOS_FAILED(status)) return status;
    window_target->last_render_sequence = request->render_sequence;
    reply->render_sequence = request->render_sequence;
    return IOS_OK;
}
