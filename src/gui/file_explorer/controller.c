#include <inferenceos/gui/file_explorer.h>

#include <inferenceos/runtime.h>

static ios_status validate_controller(
    const struct ios_file_explorer_controller *controller
)
{
    if (controller == NULL || controller->model == NULL
        || controller->model->directory_handle == IOS_INVALID_HANDLE) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    return IOS_OK;
}

ios_status ios_file_explorer_controller_initialize(
    struct ios_file_explorer_controller *controller,
    struct ios_file_explorer_model *model,
    struct ios_file_explorer_directory_operations operations
)
{
    if (controller == NULL || model == NULL || model->directory_handle == IOS_INVALID_HANDLE
        || operations.create_directory == NULL || operations.remove_directory == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *controller = (struct ios_file_explorer_controller){
        .model = model,
        .operations = operations
    };
    return IOS_OK;
}

ios_status ios_file_explorer_controller_navigate_selected(
    struct ios_file_explorer_controller *controller
)
{
    ios_u64 activated_handle;
    const struct ios_display_safe_entry *entry;
    ios_status status = validate_controller(controller);
    if (IOS_FAILED(status)) return status;
    entry = ios_file_explorer_model_selected(controller->model);
    if (entry == NULL) return IOS_ERROR(IOS_E_INVALID_STATE);
    if (entry->object_kind != IOS_DISPLAY_SAFE_DIRECTORY) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    return ios_file_explorer_model_activate(controller->model, &activated_handle);
}

ios_status ios_file_explorer_controller_navigate_back(
    struct ios_file_explorer_controller *controller
)
{
    ios_status status = validate_controller(controller);
    if (IOS_FAILED(status)) return status;
    return ios_file_explorer_model_navigate_back(controller->model);
}

ios_status ios_file_explorer_controller_create_directory(
    struct ios_file_explorer_controller *controller,
    const char *name
)
{
    ios_status status = validate_controller(controller);
    if (IOS_FAILED(status)) return status;
    if (name == NULL || *name == '\0'
        || strlen(name) >= IOS_DISPLAY_SAFE_NAME_CAPACITY) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    status = controller->operations.create_directory(
        controller->operations.context, controller->model->directory_handle, name
    );
    if (IOS_FAILED(status)) return status;
    return ios_file_explorer_model_refresh(controller->model);
}

ios_status ios_file_explorer_controller_remove_selected_directory(
    struct ios_file_explorer_controller *controller
)
{
    const struct ios_display_safe_entry *entry;
    ios_u64 directory_handle;
    ios_status status = validate_controller(controller);
    if (IOS_FAILED(status)) return status;
    entry = ios_file_explorer_model_selected(controller->model);
    if (entry == NULL) return IOS_ERROR(IOS_E_INVALID_STATE);
    if (entry->object_kind != IOS_DISPLAY_SAFE_DIRECTORY) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if ((entry->allowed_operations & IOS_DISPLAY_SAFE_OPERATION_DELETE) == 0) {
        return IOS_ERROR(IOS_E_ACCESS_DENIED);
    }
    directory_handle = entry->object_handle;
    status = controller->operations.remove_directory(
        controller->operations.context,
        controller->model->directory_handle,
        directory_handle
    );
    if (IOS_FAILED(status)) return status;
    return ios_file_explorer_model_refresh(controller->model);
}
