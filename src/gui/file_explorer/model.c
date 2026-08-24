#include <inferenceos/gui/file_explorer.h>

#include <inferenceos/runtime.h>

static ios_status refresh_directory(
    struct ios_file_explorer_model *model, ios_u64 directory_handle
)
{
    struct ios_display_safe_entry pending[IOS_FILE_EXPLORER_ENTRY_CAPACITY];
    ios_size pending_count = 0;
    ios_status status = ios_file_explorer_provider_enumerate(
        &model->provider, directory_handle, pending,
        IOS_FILE_EXPLORER_ENTRY_CAPACITY, &pending_count
    );
    if (IOS_FAILED(status)) return status;
    status = ios_display_safe_entries_disambiguate(
        pending, pending_count, model->disambiguation_workspace,
        IOS_FILE_EXPLORER_ENTRY_CAPACITY
    );
    if (IOS_FAILED(status)) return status;
    memset(model->entries, 0, sizeof(model->entries));
    memcpy(
        model->entries, pending,
        pending_count * sizeof(struct ios_display_safe_entry)
    );
    model->entry_count = pending_count;
    model->directory_handle = directory_handle;
    model->selected_index = 0;
    model->has_selection = false;
    return IOS_OK;
}

ios_status ios_file_explorer_model_initialize(
    struct ios_file_explorer_model *model,
    struct ios_file_explorer_view_provider provider,
    ios_u64 root_directory_handle
)
{
    if (model == NULL || provider.enumerate == NULL || provider.resolve_icon == NULL
        || root_directory_handle == 0) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    memset(model, 0, sizeof(*model));
    model->provider = provider;
    return refresh_directory(model, root_directory_handle);
}

ios_status ios_file_explorer_model_refresh(struct ios_file_explorer_model *model)
{
    if (model == NULL || model->directory_handle == 0) return IOS_ERROR(IOS_E_INVALID_STATE);
    return refresh_directory(model, model->directory_handle);
}

ios_status ios_file_explorer_model_select(
    struct ios_file_explorer_model *model, ios_size index
)
{
    if (model == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    if (index >= model->entry_count) return IOS_ERROR(IOS_E_OUT_OF_RANGE);
    model->selected_index = index;
    model->has_selection = true;
    return IOS_OK;
}

const struct ios_display_safe_entry *ios_file_explorer_model_selected(
    const struct ios_file_explorer_model *model
)
{
    if (model == NULL || !model->has_selection || model->selected_index >= model->entry_count) {
        return NULL;
    }
    return &model->entries[model->selected_index];
}

ios_status ios_file_explorer_model_activate(
    struct ios_file_explorer_model *model, ios_u64 *activated_handle
)
{
    const struct ios_display_safe_entry *entry = ios_file_explorer_model_selected(model);
    if (entry == NULL || activated_handle == NULL) return IOS_ERROR(IOS_E_INVALID_STATE);
    *activated_handle = entry->object_handle;
    if (entry->object_kind != IOS_DISPLAY_SAFE_DIRECTORY) return IOS_OK;
    if ((entry->allowed_operations & IOS_DISPLAY_SAFE_OPERATION_ENUMERATE) == 0) {
        return IOS_ERROR(IOS_E_ACCESS_DENIED);
    }
    if (model->history_count >= IOS_FILE_EXPLORER_HISTORY_CAPACITY) {
        return IOS_ERROR(IOS_E_NO_SPACE);
    }
    const ios_u64 previous = model->directory_handle;
    ios_status status = refresh_directory(model, entry->object_handle);
    if (IOS_FAILED(status)) return status;
    model->history[model->history_count++] = previous;
    return IOS_OK;
}

ios_status ios_file_explorer_model_navigate_back(struct ios_file_explorer_model *model)
{
    if (model == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    if (model->history_count == 0) return IOS_ERROR(IOS_E_NOT_FOUND);
    const ios_u64 destination = model->history[model->history_count - 1U];
    ios_status status = refresh_directory(model, destination);
    if (IOS_SUCCEEDED(status)) --model->history_count;
    return status;
}
