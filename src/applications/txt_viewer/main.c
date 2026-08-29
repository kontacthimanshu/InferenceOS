#include <inferenceos/txt_viewer.h>

#include <inferenceos/runtime.h>

static ios_status enumerate_txt_files(
    void *context,
    ios_u64 directory_handle,
    struct ios_display_safe_entry *entries,
    ios_size capacity,
    ios_size *entry_count
)
{
    struct ios_txt_viewer_application *application = context;
    return ios_file_explorer_client_file_view(
        &application->client,
        IOS_SHELL_TYPE_VIEW,
        directory_handle,
        application->txt_type_capability,
        entries,
        capacity,
        entry_count
    );
}

static ios_status resolve_txt_icon(
    void *context,
    ios_u64 type_icon_capability,
    ios_u32 object_kind,
    enum ios_presentation_icon *icon
)
{
    struct ios_txt_viewer_application *application = context;
    return application->client.resolve_icon(
        application->client.resolve_icon_context,
        type_icon_capability,
        object_kind,
        icon
    );
}

ios_status ios_txt_viewer_initialize(
    struct ios_txt_viewer_application *application,
    struct ios_process *process,
    struct ios_shell_service *shell_service,
    ios_file_explorer_resolve_icon_function resolve_icon,
    void *resolve_icon_context,
    ios_u64 root_directory_handle,
    ios_type_icon_capability txt_type_capability,
    struct ios_graphics_surface surface,
    const struct ios_psf2_font *font
)
{
    struct ios_file_explorer_view_provider provider;
    ios_status status;

    if (application == NULL || txt_type_capability == IOS_INVALID_TYPE_ICON_CAPABILITY
        || root_directory_handle == IOS_INVALID_HANDLE) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    memset(application, 0, sizeof(*application));
    status = ios_file_explorer_client_initialize(
        &application->client,
        process,
        shell_service,
        resolve_icon,
        resolve_icon_context
    );
    if (IOS_FAILED(status)) return status;
    application->txt_type_capability = txt_type_capability;
    provider = (struct ios_file_explorer_view_provider){
        application,
        enumerate_txt_files,
        resolve_txt_icon
    };
    status = ios_file_explorer_model_initialize(
        &application->model,
        provider,
        root_directory_handle
    );
    if (status == IOS_ERROR(IOS_E_NOT_FOUND)
        || status == IOS_ERROR(IOS_E_NOT_SUPPORTED)
        || status == IOS_ERROR(IOS_E_INVALID_STATE)) {
        memset(&application->model, 0, sizeof(application->model));
        application->model.provider = provider;
        application->model.directory_handle = root_directory_handle;
        status = IOS_OK;
    }
    if (IOS_FAILED(status)) {
        ios_file_explorer_client_disconnect(&application->client);
        return status;
    }
    status = ios_file_explorer_window_initialize(
        &application->window,
        &application->model,
        surface,
        font
    );
    if (IOS_FAILED(status)) {
        ios_file_explorer_client_disconnect(&application->client);
        return status;
    }
    application->window.title = "TXT Files App";
    return IOS_OK;
}

void ios_txt_viewer_disconnect(struct ios_txt_viewer_application *application)
{
    if (application == NULL) return;
    ios_file_explorer_client_disconnect(&application->client);
}

ios_status ios_txt_viewer_render(struct ios_txt_viewer_application *application)
{
    if (application == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    return ios_file_explorer_window_render(&application->window);
}

ios_status ios_txt_viewer_handle_input(
    struct ios_txt_viewer_application *application,
    const struct ios_input_event *event,
    bool *activated,
    ios_u64 *activated_handle
)
{
    if (application == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    return ios_file_explorer_window_handle_input(
        &application->window,
        event,
        activated,
        activated_handle
    );
}
