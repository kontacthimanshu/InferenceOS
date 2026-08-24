#include <inferenceos/gui/file_explorer.h>

#include <inferenceos/runtime.h>

ios_status ios_file_explorer_properties_from_selection(
    const struct ios_file_explorer_model *model,
    struct ios_file_explorer_properties *properties
)
{
    const struct ios_display_safe_entry *entry = ios_file_explorer_model_selected(model);
    if (entry == NULL || properties == NULL) return IOS_ERROR(IOS_E_INVALID_STATE);
    memset(properties, 0, sizeof(*properties));
    properties->object_handle = entry->object_handle;
    properties->byte_size = entry->byte_size;
    properties->allowed_operations = entry->allowed_operations;
    properties->generic_attributes = entry->generic_attributes;
    properties->object_kind = entry->object_kind;
    properties->display_name_length = entry->display_name_length;
    memcpy(properties->display_name, entry->display_name, entry->display_name_length + 1U);
    return IOS_OK;
}
