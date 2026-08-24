#include <inferenceos/gui/file_explorer.h>

enum {
    FILE_EXPLORER_BACKGROUND = UINT32_C(0x00f2f2f2),
    FILE_EXPLORER_SELECTION = UINT32_C(0x003878d8),
    FILE_EXPLORER_FOREGROUND = UINT32_C(0x00202020),
    FILE_EXPLORER_ICON_TEXT_COLOR = UINT32_C(0x004f81bd),
    FILE_EXPLORER_ICON_IMAGE_COLOR = UINT32_C(0x003e9b52),
    FILE_EXPLORER_ICON_APPLICATION_COLOR = UINT32_C(0x00934db1),
    FILE_EXPLORER_ICON_FOLDER_COLOR = UINT32_C(0x00d6a832),
    FILE_EXPLORER_ICON_GENERIC_COLOR = UINT32_C(0x00808080)
};

static ios_u32 icon_color(enum ios_presentation_icon icon)
{
    if (icon == IOS_ICON_TEXT) return FILE_EXPLORER_ICON_TEXT_COLOR;
    if (icon == IOS_ICON_IMAGE) return FILE_EXPLORER_ICON_IMAGE_COLOR;
    if (icon == IOS_ICON_APPLICATION) return FILE_EXPLORER_ICON_APPLICATION_COLOR;
    if (icon == IOS_ICON_FOLDER) return FILE_EXPLORER_ICON_FOLDER_COLOR;
    return FILE_EXPLORER_ICON_GENERIC_COLOR;
}

ios_status ios_file_explorer_window_initialize(
    struct ios_file_explorer_window *window,
    struct ios_file_explorer_model *model,
    struct ios_graphics_surface surface,
    const struct ios_psf2_font *font
)
{
    if (window == NULL || model == NULL || font == NULL || font->glyphs == NULL
        || font->width != IOS_PSF2_FONT_WIDTH || font->height != IOS_PSF2_FONT_HEIGHT
        || !ios_graphics_surface_is_valid(&surface)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *window = (struct ios_file_explorer_window){ 0 };
    window->model = model;
    window->surface = surface;
    window->font = font;
    window->background_color = FILE_EXPLORER_BACKGROUND;
    window->selection_color = FILE_EXPLORER_SELECTION;
    window->foreground_color = FILE_EXPLORER_FOREGROUND;
    return IOS_OK;
}

ios_status ios_file_explorer_window_render(struct ios_file_explorer_window *window)
{
    if (window == NULL || window->model == NULL
        || !ios_graphics_surface_is_valid(&window->surface)) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    ios_graphics_fill_rect(&window->surface, (struct ios_graphics_rect){
        0, 0, (ios_i32)window->surface.width, (ios_i32)window->surface.height
    }, window->background_color);
    for (ios_size index = 0; index < window->model->entry_count; ++index) {
        const ios_i32 y = (ios_i32)(index * IOS_FILE_EXPLORER_ROW_HEIGHT);
        if (y >= (ios_i32)window->surface.height) break;
        if (window->model->has_selection && window->model->selected_index == index) {
            ios_graphics_fill_rect(&window->surface, (struct ios_graphics_rect){
                0, y, (ios_i32)window->surface.width, IOS_FILE_EXPLORER_ROW_HEIGHT
            }, window->selection_color);
        }
        enum ios_presentation_icon icon;
        ios_status status = ios_file_explorer_provider_resolve_icon(
            &window->model->provider, &window->model->entries[index], &icon
        );
        if (IOS_FAILED(status)) return status;
        ios_graphics_fill_rect(&window->surface, (struct ios_graphics_rect){
            4, y + 4, IOS_FILE_EXPLORER_ICON_SIZE, IOS_FILE_EXPLORER_ICON_SIZE
        }, icon_color(icon));
        status = ios_graphics_draw_text(
            &window->surface, window->font, window->model->entries[index].display_name,
            20, y + 2, window->foreground_color,
            window->model->has_selection && window->model->selected_index == index
                ? window->selection_color : window->background_color,
            false
        );
        if (IOS_FAILED(status)) return status;
    }
    return IOS_OK;
}

ios_status ios_file_explorer_window_handle_input(
    struct ios_file_explorer_window *window,
    const struct ios_input_event *event,
    bool *activated,
    ios_u64 *activated_handle
)
{
    if (window == NULL || window->model == NULL || event == NULL || activated == NULL
        || activated_handle == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    *activated = false;
    *activated_handle = 0;
    if (event->type == IOS_INPUT_EVENT_POINTER_BUTTON
        && event->code == IOS_POINTER_BUTTON_LEFT
        && (event->flags & IOS_INPUT_PRESSED) != 0) {
        if (event->x < 0 || event->y < 0 || event->x >= (ios_i32)window->surface.width
            || event->y >= (ios_i32)window->surface.height) return IOS_OK;
        const ios_size index = (ios_size)event->y / IOS_FILE_EXPLORER_ROW_HEIGHT;
        if (index < window->model->entry_count) {
            return ios_file_explorer_model_select(window->model, index);
        }
        return IOS_OK;
    }
    if (event->type != IOS_INPUT_EVENT_KEY || (event->flags & IOS_INPUT_PRESSED) == 0) {
        return IOS_OK;
    }
    if (event->code == IOS_KEY_UP || event->code == IOS_KEY_DOWN) {
        if (window->model->entry_count == 0) return IOS_OK;
        ios_size index = window->model->has_selection ? window->model->selected_index : 0;
        if (event->code == IOS_KEY_UP && index > 0) --index;
        if (event->code == IOS_KEY_DOWN && index + 1U < window->model->entry_count) ++index;
        return ios_file_explorer_model_select(window->model, index);
    }
    if (event->code == IOS_KEY_ENTER && window->model->has_selection) {
        ios_status status = ios_file_explorer_model_activate(window->model, activated_handle);
        if (IOS_SUCCEEDED(status)) *activated = true;
        return status;
    }
    if (event->code == IOS_KEY_BACKSPACE) return ios_file_explorer_model_navigate_back(window->model);
    return IOS_OK;
}
