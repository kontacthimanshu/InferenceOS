#include <inferenceos/gui/file_explorer.h>

#include <inferenceos/runtime.h>

enum {
    FILE_EXPLORER_BACKGROUND = UINT32_C(0x00f2f2f2),
    FILE_EXPLORER_HEADER = UINT32_C(0x00344f70),
    FILE_EXPLORER_SELECTION = UINT32_C(0x003878d8),
    FILE_EXPLORER_FOREGROUND = UINT32_C(0x00202020),
    FILE_EXPLORER_ICON_PAGE = UINT32_C(0x00ffffff),
    FILE_EXPLORER_ICON_TEXT_COLOR = UINT32_C(0x004f81bd),
    FILE_EXPLORER_ICON_IMAGE_COLOR = UINT32_C(0x003e9b52),
    FILE_EXPLORER_ICON_APPLICATION_COLOR = UINT32_C(0x00934db1),
    FILE_EXPLORER_ICON_FOLDER_COLOR = UINT32_C(0x00d6a832),
    FILE_EXPLORER_ICON_GENERIC_COLOR = UINT32_C(0x00808080),
    FILE_EXPLORER_ICON_DARK = UINT32_C(0x00303030)
};

static ios_size column_count(const struct ios_file_explorer_window *window)
{
    ios_size count = window->surface.width / IOS_FILE_EXPLORER_CELL_WIDTH;
    return count == 0 ? 1 : count;
}

static ios_size row_count(const struct ios_file_explorer_window *window)
{
    if (window->surface.height <= IOS_FILE_EXPLORER_HEADER_HEIGHT) return 0;
    ios_size count = (window->surface.height - IOS_FILE_EXPLORER_HEADER_HEIGHT)
        / IOS_FILE_EXPLORER_CELL_HEIGHT;
    return count == 0 ? 1 : count;
}

static struct ios_graphics_rect cell_bounds(
    const struct ios_file_explorer_window *window, ios_size visible_index
)
{
    const ios_size columns = column_count(window);
    return (struct ios_graphics_rect){
        (ios_i32)((visible_index % columns) * IOS_FILE_EXPLORER_CELL_WIDTH),
        IOS_FILE_EXPLORER_HEADER_HEIGHT
            + (ios_i32)((visible_index / columns) * IOS_FILE_EXPLORER_CELL_HEIGHT),
        IOS_FILE_EXPLORER_CELL_WIDTH,
        IOS_FILE_EXPLORER_CELL_HEIGHT
    };
}

static void draw_file_icon(
    struct ios_graphics_surface *surface,
    enum ios_presentation_icon icon,
    ios_i32 x,
    ios_i32 y
)
{
    const struct ios_graphics_rect page = { x + 3, y, 26, 32 };
    if (icon == IOS_ICON_FOLDER) {
        ios_graphics_fill_rect(surface, (struct ios_graphics_rect){ x + 2, y + 5, 13, 7 },
                               FILE_EXPLORER_ICON_FOLDER_COLOR);
        ios_graphics_fill_rect(surface, (struct ios_graphics_rect){ x, y + 10, 32, 21 },
                               FILE_EXPLORER_ICON_FOLDER_COLOR);
        ios_graphics_draw_border(surface, (struct ios_graphics_rect){ x, y + 10, 32, 21 },
                                 FILE_EXPLORER_ICON_DARK);
        return;
    }
    if (icon == IOS_ICON_APPLICATION) {
        ios_graphics_fill_rect(surface, (struct ios_graphics_rect){ x, y, 32, 32 },
                               FILE_EXPLORER_ICON_APPLICATION_COLOR);
        ios_graphics_draw_border(surface, (struct ios_graphics_rect){ x, y, 32, 32 },
                                 FILE_EXPLORER_ICON_DARK);
        for (ios_i32 row = 0; row < 2; ++row) {
            for (ios_i32 column = 0; column < 2; ++column) {
                ios_graphics_fill_rect(
                    surface,
                    (struct ios_graphics_rect){ x + 7 + column * 11, y + 7 + row * 11, 7, 7 },
                    FILE_EXPLORER_ICON_PAGE
                );
            }
        }
        return;
    }

    ios_graphics_fill_rect(surface, page, FILE_EXPLORER_ICON_PAGE);
    ios_graphics_draw_border(
        surface, page,
        icon == IOS_ICON_TEXT ? FILE_EXPLORER_ICON_TEXT_COLOR
            : icon == IOS_ICON_IMAGE ? FILE_EXPLORER_ICON_IMAGE_COLOR
            : FILE_EXPLORER_ICON_GENERIC_COLOR
    );
    if (icon == IOS_ICON_TEXT) {
        for (ios_i32 line = 0; line < 3; ++line) {
            ios_graphics_draw_line(
                surface, x + 8, y + 9 + line * 6, x + 23, y + 9 + line * 6,
                FILE_EXPLORER_ICON_TEXT_COLOR
            );
        }
    } else if (icon == IOS_ICON_IMAGE) {
        ios_graphics_fill_rect(surface, (struct ios_graphics_rect){ x + 7, y + 6, 18, 20 },
                               UINT32_C(0x00d8f0ff));
        ios_graphics_draw_line(surface, x + 7, y + 25, x + 14, y + 16,
                               FILE_EXPLORER_ICON_IMAGE_COLOR);
        ios_graphics_draw_line(surface, x + 14, y + 16, x + 24, y + 25,
                               FILE_EXPLORER_ICON_IMAGE_COLOR);
        ios_graphics_fill_rect(surface, (struct ios_graphics_rect){ x + 19, y + 9, 4, 4 },
                               FILE_EXPLORER_ICON_FOLDER_COLOR);
    } else {
        ios_graphics_draw_line(surface, x + 9, y + 10, x + 22, y + 23,
                               FILE_EXPLORER_ICON_GENERIC_COLOR);
        ios_graphics_draw_line(surface, x + 22, y + 10, x + 9, y + 23,
                               FILE_EXPLORER_ICON_GENERIC_COLOR);
    }
}

static ios_status draw_label(
    struct ios_file_explorer_window *window,
    const char *label,
    struct ios_graphics_rect cell,
    ios_u32 background
)
{
    char line[IOS_DISPLAY_SAFE_NAME_CAPACITY];
    const ios_size label_length = strlen(label);
    ios_size characters = (IOS_FILE_EXPLORER_CELL_WIDTH - 8U) / window->font->width;
    ios_size cursor = 0;
    if (characters == 0) characters = 1;
    for (ios_size row = 0; row < 2 && cursor < label_length; ++row) {
        ios_size count = label_length - cursor;
        if (count > characters) count = characters;
        memcpy(line, label + cursor, count);
        line[count] = '\0';
        ios_status status = ios_graphics_draw_text(
            &window->surface, window->font, line,
            cell.x + 4,
            cell.y + IOS_FILE_EXPLORER_ICON_SIZE + 12
                + (ios_i32)(row * window->font->height),
            window->foreground_color, background, false
        );
        if (IOS_FAILED(status)) return status;
        cursor += count;
    }
    return IOS_OK;
}

static void ensure_selection_visible(struct ios_file_explorer_window *window)
{
    const ios_size columns = column_count(window);
    const ios_size rows = row_count(window);
    const ios_size visible_capacity = columns * rows;
    const ios_size selected = window->model->selected_index;
    if (visible_capacity == 0) return;
    if (selected < window->first_visible_index) {
        window->first_visible_index = (selected / columns) * columns;
    } else if (selected >= window->first_visible_index + visible_capacity) {
        const ios_size selected_row = selected / columns;
        window->first_visible_index = (selected_row - rows + 1U) * columns;
    }
}

ios_status ios_file_explorer_window_initialize(
    struct ios_file_explorer_window *window,
    struct ios_file_explorer_model *model,
    struct ios_graphics_surface surface,
    const struct ios_psf2_font *font
)
{
    if (window == NULL || model == NULL || font == NULL || font->glyphs == NULL
        || font->width == 0 || font->height == 0
        || !ios_graphics_surface_is_valid(&surface)
        || surface.width < IOS_FILE_EXPLORER_ICON_SIZE + 8U
        || surface.height < IOS_FILE_EXPLORER_HEADER_HEIGHT + IOS_FILE_EXPLORER_ICON_SIZE) {
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
    ios_size visible_capacity;
    if (window == NULL || window->model == NULL
        || !ios_graphics_surface_is_valid(&window->surface)) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    ios_graphics_fill_rect(&window->surface, (struct ios_graphics_rect){
        0, 0, (ios_i32)window->surface.width, (ios_i32)window->surface.height
    }, window->background_color);
    ios_graphics_fill_rect(&window->surface, (struct ios_graphics_rect){
        0, 0, (ios_i32)window->surface.width, IOS_FILE_EXPLORER_HEADER_HEIGHT
    }, FILE_EXPLORER_HEADER);
    ios_status status = ios_graphics_draw_text(
        &window->surface, window->font, "Files", 8, 2,
        FILE_EXPLORER_ICON_PAGE, FILE_EXPLORER_HEADER, false
    );
    if (IOS_FAILED(status)) return status;
    if (window->first_visible_index >= window->model->entry_count) {
        window->first_visible_index = 0;
    }
    visible_capacity = column_count(window) * row_count(window);
    for (ios_size visible = 0; visible < visible_capacity; ++visible) {
        const ios_size index = window->first_visible_index + visible;
        struct ios_graphics_rect cell;
        enum ios_presentation_icon icon;
        ios_u32 label_background = window->background_color;
        ios_i32 icon_x;
        if (index >= window->model->entry_count) break;
        cell = cell_bounds(window, visible);
        if (cell.y >= (ios_i32)window->surface.height) break;
        if (window->model->has_selection && window->model->selected_index == index) {
            ios_graphics_fill_rect(&window->surface, (struct ios_graphics_rect){
                cell.x + 2, cell.y + 2, cell.width - 4, cell.height - 4
            }, window->selection_color);
            label_background = window->selection_color;
        }
        status = ios_file_explorer_provider_resolve_icon(
            &window->model->provider, &window->model->entries[index], &icon
        );
        if (IOS_FAILED(status)) return status;
        icon_x = cell.x + (IOS_FILE_EXPLORER_CELL_WIDTH - IOS_FILE_EXPLORER_ICON_SIZE) / 2;
        draw_file_icon(&window->surface, icon, icon_x, cell.y + 6);
        status = draw_label(
            window, window->model->entries[index].display_name, cell, label_background
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
        if (event->x < 0 || event->y < IOS_FILE_EXPLORER_HEADER_HEIGHT
            || event->x >= (ios_i32)window->surface.width
            || event->y >= (ios_i32)window->surface.height) return IOS_OK;
        const ios_size column = (ios_size)event->x / IOS_FILE_EXPLORER_CELL_WIDTH;
        const ios_size row = (ios_size)(event->y - IOS_FILE_EXPLORER_HEADER_HEIGHT)
            / IOS_FILE_EXPLORER_CELL_HEIGHT;
        if (column >= column_count(window)) return IOS_OK;
        const ios_size index = window->first_visible_index
            + row * column_count(window) + column;
        if (index < window->model->entry_count) {
            return ios_file_explorer_model_select(window->model, index);
        }
        return IOS_OK;
    }
    if (event->type != IOS_INPUT_EVENT_KEY || (event->flags & IOS_INPUT_PRESSED) == 0) {
        return IOS_OK;
    }
    if (event->code == IOS_KEY_LEFT || event->code == IOS_KEY_RIGHT
        || event->code == IOS_KEY_UP || event->code == IOS_KEY_DOWN) {
        if (window->model->entry_count == 0) return IOS_OK;
        const ios_size columns = column_count(window);
        ios_size index = window->model->has_selection ? window->model->selected_index : 0;
        if (event->code == IOS_KEY_LEFT && index > 0) --index;
        if (event->code == IOS_KEY_RIGHT && index + 1U < window->model->entry_count) ++index;
        if (event->code == IOS_KEY_UP && index >= columns) index -= columns;
        if (event->code == IOS_KEY_DOWN && index + columns < window->model->entry_count) {
            index += columns;
        }
        ios_status status = ios_file_explorer_model_select(window->model, index);
        if (IOS_SUCCEEDED(status)) ensure_selection_visible(window);
        return status;
    }
    if (event->code == IOS_KEY_ENTER && window->model->has_selection) {
        const bool directory = window->model->entries[window->model->selected_index].object_kind
            == IOS_DISPLAY_SAFE_DIRECTORY;
        ios_status status = ios_file_explorer_model_activate(window->model, activated_handle);
        if (IOS_SUCCEEDED(status)) {
            *activated = true;
            if (directory) window->first_visible_index = 0;
        }
        return status;
    }
    if (event->code == IOS_KEY_BACKSPACE) {
        ios_status status = ios_file_explorer_model_navigate_back(window->model);
        if (IOS_SUCCEEDED(status)) window->first_visible_index = 0;
        return status;
    }
    return IOS_OK;
}
