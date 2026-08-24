#include <inferenceos/gui/file_explorer.h>

ios_status ios_file_explorer_provider_enumerate(
    const struct ios_file_explorer_view_provider *provider,
    ios_u64 directory_handle,
    struct ios_display_safe_entry *entries,
    ios_size capacity,
    ios_size *entry_count
)
{
    if (provider == NULL || provider->enumerate == NULL || entries == NULL
        || entry_count == NULL || capacity == 0 || directory_handle == 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *entry_count = 0;
    ios_status status = provider->enumerate(
        provider->context, directory_handle, entries, capacity, entry_count
    );
    if (IOS_FAILED(status)) return status;
    if (*entry_count > capacity) {
        *entry_count = 0;
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    return IOS_OK;
}

ios_status ios_file_explorer_provider_resolve_icon(
    const struct ios_file_explorer_view_provider *provider,
    const struct ios_display_safe_entry *entry,
    enum ios_presentation_icon *icon
)
{
    if (provider == NULL || provider->resolve_icon == NULL || entry == NULL || icon == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *icon = IOS_ICON_GENERIC_FILE;
    ios_status status = provider->resolve_icon(
        provider->context, entry->type_icon_capability, entry->object_kind, icon
    );
    if (IOS_FAILED(status)) return status;
    if (*icon != IOS_ICON_GENERIC_FILE && *icon != IOS_ICON_FOLDER
        && *icon != IOS_ICON_TEXT && *icon != IOS_ICON_IMAGE
        && *icon != IOS_ICON_APPLICATION) {
        *icon = IOS_ICON_GENERIC_FILE;
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    return IOS_OK;
}
