#include <inferenceos/gui/file_explorer.h>

#include <inferenceos/runtime.h>

enum {
    INSPECTOR_BACKGROUND = UINT32_C(0x001c2330),
    INSPECTOR_HEADER = UINT32_C(0x002f6f9f),
    INSPECTOR_FOREGROUND = UINT32_C(0x00f2f2f2),
    INSPECTOR_ERROR = UINT32_C(0x00ff8080),
    INSPECTOR_LINE_HEIGHT = 18
};

static ios_status query(
    struct ios_file_explorer_diagnostic_inspector *inspector,
    enum ios_fs_diagnostic_query kind,
    ios_u64 object_identity,
    struct ios_fs_diagnostic_reply *reply
)
{
    return inspector->provider.query(
        inspector->provider.context, kind, object_identity, reply
    );
}

static void append_text(char *line, ios_size capacity, ios_size *length, const char *text)
{
    while (*text != '\0' && *length + 1 < capacity) line[(*length)++] = *text++;
    line[*length] = '\0';
}

static void append_u64(char *line, ios_size capacity, ios_size *length, ios_u64 value)
{
    char digits[21];
    ios_size count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (count != 0 && *length + 1 < capacity) line[(*length)++] = digits[--count];
    line[*length] = '\0';
}

static ios_status draw_line(
    struct ios_file_explorer_diagnostic_inspector *inspector,
    ios_i32 row, const char *label, const ios_u8 *bytes, ios_size byte_count,
    ios_u64 number, bool show_number
)
{
    char line[96] = { 0 };
    ios_size length = 0;
    append_text(line, sizeof(line), &length, label);
    if (bytes != NULL) {
        for (ios_size index = 0; index < byte_count && length + 1 < sizeof(line); ++index) {
            line[length++] = (char)bytes[index];
        }
        line[length] = '\0';
    }
    if (show_number) append_u64(line, sizeof(line), &length, number);
    return ios_graphics_draw_text(
        &inspector->surface, inspector->font, line, 8,
        26 + row * INSPECTOR_LINE_HEIGHT, INSPECTOR_FOREGROUND,
        INSPECTOR_BACKGROUND, false
    );
}

static ios_status render_filesystem(struct ios_file_explorer_diagnostic_inspector *inspector)
{
    const struct ios_fs_diagnostic_filesystem_info *info =
        &inspector->replies[IOS_FILE_EXPLORER_DIAGNOSTIC_FILESYSTEM].value.filesystem;
    ios_status status = draw_line(inspector, 0, "Identity: ", info->identity, 8, 0, false);
    if (IOS_FAILED(status)) return status;
    status = draw_line(inspector, 1, "Format version: ", NULL, 0, info->format_version, true);
    if (IOS_FAILED(status)) return status;
    status = draw_line(inspector, 2, "Capacity bytes: ", NULL, 0, info->volume_capacity_bytes, true);
    if (IOS_FAILED(status)) return status;
    status = draw_line(inspector, 3, "Clusters: ", NULL, 0, info->cluster_count, true);
    if (IOS_FAILED(status)) return status;
    status = draw_line(inspector, 4, "FAT sectors: ", NULL, 0, info->fat_sectors, true);
    if (IOS_FAILED(status)) return status;
    return draw_line(inspector, 5, "Registry state: ", NULL, 0, info->registry_health, true);
}

static ios_status render_file(struct ios_file_explorer_diagnostic_inspector *inspector)
{
    const struct ios_fs_diagnostic_file_info *info =
        &inspector->replies[IOS_FILE_EXPLORER_DIAGNOSTIC_FILE].value.file;
    ios_status status = draw_line(
        inspector, 0, "Canonical name: ", info->canonical_name, IOS_FS_NAME_SIZE, 0, false
    );
    if (IOS_FAILED(status)) return status;
    status = draw_line(inspector, 1, "Attributes: ", NULL, 0, info->attributes, true);
    if (IOS_FAILED(status)) return status;
    status = draw_line(inspector, 2, "Size: ", NULL, 0, info->size, true);
    if (IOS_FAILED(status)) return status;
    status = draw_line(inspector, 3, "First cluster: ", NULL, 0, info->first_cluster, true);
    if (IOS_FAILED(status)) return status;
    status = draw_line(inspector, 4, "Primary location: ", NULL, 0, info->primary_record_location, true);
    if (IOS_FAILED(status)) return status;
    return draw_line(inspector, 5, "Companion location: ", NULL, 0, info->companion_record_location, true);
}

static ios_status render_hash(struct ios_file_explorer_diagnostic_inspector *inspector)
{
    const struct ios_fs_diagnostic_hash_info *info =
        &inspector->replies[IOS_FILE_EXPLORER_DIAGNOSTIC_HASH].value.hash;
    ios_status status = draw_line(
        inspector, 0, "Extension: ", info->extension, info->extension_length, 0, false
    );
    if (IOS_FAILED(status)) return status;
    status = draw_line(inspector, 1, "Stored hash: ", info->stored_hash, IOS_FS_HASH_TEXT_SIZE, 0, false);
    if (IOS_FAILED(status)) return status;
    status = draw_line(inspector, 2, "Computed hash: ", info->recomputed_hash, IOS_FS_HASH_TEXT_SIZE, 0, false);
    if (IOS_FAILED(status)) return status;
    status = draw_line(inspector, 3, "Record version: ", NULL, 0, info->record_version, true);
    if (IOS_FAILED(status)) return status;
    status = draw_line(inspector, 4, "CRC valid: ", NULL, 0, info->crc_valid ? 1 : 0, true);
    if (IOS_FAILED(status)) return status;
    return draw_line(
        inspector, 5, "Association valid: ", NULL, 0,
        info->association_checksum_valid ? 1 : 0, true
    );
}

static ios_status render_fat(struct ios_file_explorer_diagnostic_inspector *inspector)
{
    const struct ios_fs_diagnostic_fat_info *info =
        &inspector->replies[IOS_FILE_EXPLORER_DIAGNOSTIC_FAT].value.fat;
    ios_status status = draw_line(inspector, 0, "Cluster count: ", NULL, 0, info->cluster_count, true);
    if (IOS_FAILED(status)) return status;
    for (ios_size index = 0; index < info->cluster_count; ++index) {
        const ios_i32 row = (ios_i32)index + 1;
        if (26 + row * INSPECTOR_LINE_HEIGHT + IOS_PSF2_FONT_HEIGHT
            > (ios_i32)inspector->surface.height) break;
        status = draw_line(inspector, row, "Cluster: ", NULL, 0, info->clusters[index], true);
        if (IOS_FAILED(status)) return status;
    }
    return IOS_OK;
}

ios_status ios_file_explorer_diagnostic_inspector_initialize(
    struct ios_file_explorer_diagnostic_inspector *inspector,
    struct ios_file_explorer_model *model,
    struct ios_file_explorer_diagnostic_provider provider,
    struct ios_graphics_surface surface,
    const struct ios_psf2_font *font
)
{
    if (inspector == NULL || model == NULL || provider.query == NULL
        || font == NULL || font->glyphs == NULL
        || !ios_graphics_surface_is_valid(&surface)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    memset(inspector, 0, sizeof(*inspector));
    inspector->model = model;
    inspector->provider = provider;
    inspector->surface = surface;
    inspector->font = font;
    return IOS_OK;
}

ios_status ios_file_explorer_diagnostic_inspector_open(
    struct ios_file_explorer_diagnostic_inspector *inspector
)
{
    const struct ios_display_safe_entry *selected;
    if (inspector == NULL || inspector->model == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    selected = ios_file_explorer_model_selected(inspector->model);
    if (selected == NULL || selected->object_handle == 0) return IOS_ERROR(IOS_E_INVALID_STATE);
    memset(inspector->replies, 0, sizeof(inspector->replies));
    inspector->object_identity = selected->object_handle;
    inspector->page_status[IOS_FILE_EXPLORER_DIAGNOSTIC_FILESYSTEM] = query(
        inspector, IOS_FS_DIAGNOSTIC_QUERY_FILESYSTEM, 0,
        &inspector->replies[IOS_FILE_EXPLORER_DIAGNOSTIC_FILESYSTEM]
    );
    inspector->page_status[IOS_FILE_EXPLORER_DIAGNOSTIC_FILE] = query(
        inspector, IOS_FS_DIAGNOSTIC_QUERY_FILE, inspector->object_identity,
        &inspector->replies[IOS_FILE_EXPLORER_DIAGNOSTIC_FILE]
    );
    inspector->page_status[IOS_FILE_EXPLORER_DIAGNOSTIC_HASH] = query(
        inspector, IOS_FS_DIAGNOSTIC_QUERY_HASH, inspector->object_identity,
        &inspector->replies[IOS_FILE_EXPLORER_DIAGNOSTIC_HASH]
    );
    inspector->page_status[IOS_FILE_EXPLORER_DIAGNOSTIC_FAT] = query(
        inspector, IOS_FS_DIAGNOSTIC_QUERY_FAT, inspector->object_identity,
        &inspector->replies[IOS_FILE_EXPLORER_DIAGNOSTIC_FAT]
    );
    if (IOS_FAILED(inspector->page_status[IOS_FILE_EXPLORER_DIAGNOSTIC_FILE])) {
        ios_status status = inspector->page_status[IOS_FILE_EXPLORER_DIAGNOSTIC_FILE];
        ios_file_explorer_diagnostic_inspector_close(inspector);
        return status;
    }
    inspector->page = IOS_FILE_EXPLORER_DIAGNOSTIC_FILE;
    inspector->visible = true;
    return IOS_OK;
}

void ios_file_explorer_diagnostic_inspector_close(
    struct ios_file_explorer_diagnostic_inspector *inspector
)
{
    if (inspector == NULL) return;
    memset(inspector->replies, 0, sizeof(inspector->replies));
    memset(inspector->page_status, 0, sizeof(inspector->page_status));
    inspector->object_identity = 0;
    inspector->page = IOS_FILE_EXPLORER_DIAGNOSTIC_FILESYSTEM;
    inspector->visible = false;
}

ios_status ios_file_explorer_diagnostic_inspector_render(
    struct ios_file_explorer_diagnostic_inspector *inspector
)
{
    static const char *titles[] = { "Filesystem", "File", "Hash", "FAT chain" };
    if (inspector == NULL || !inspector->visible
        || inspector->page >= IOS_FILE_EXPLORER_DIAGNOSTIC_PAGE_COUNT) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    ios_graphics_fill_rect(&inspector->surface, (struct ios_graphics_rect){
        0, 0, (ios_i32)inspector->surface.width, (ios_i32)inspector->surface.height
    }, INSPECTOR_BACKGROUND);
    ios_graphics_fill_rect(&inspector->surface, (struct ios_graphics_rect){
        0, 0, (ios_i32)inspector->surface.width, 22
    }, INSPECTOR_HEADER);
    ios_status status = ios_graphics_draw_text(
        &inspector->surface, inspector->font, titles[inspector->page], 6, 3,
        INSPECTOR_FOREGROUND, INSPECTOR_HEADER, false
    );
    if (IOS_FAILED(status)) return status;
    if (IOS_FAILED(inspector->page_status[inspector->page])) {
        return ios_graphics_draw_text(
            &inspector->surface, inspector->font, "Diagnostic unavailable", 8, 28,
            INSPECTOR_ERROR, INSPECTOR_BACKGROUND, false
        );
    }
    switch (inspector->page) {
    case IOS_FILE_EXPLORER_DIAGNOSTIC_FILESYSTEM: return render_filesystem(inspector);
    case IOS_FILE_EXPLORER_DIAGNOSTIC_FILE: return render_file(inspector);
    case IOS_FILE_EXPLORER_DIAGNOSTIC_HASH: return render_hash(inspector);
    case IOS_FILE_EXPLORER_DIAGNOSTIC_FAT: return render_fat(inspector);
    default: return IOS_ERROR(IOS_E_INVALID_STATE);
    }
}

ios_status ios_file_explorer_diagnostic_inspector_handle_input(
    struct ios_file_explorer_diagnostic_inspector *inspector,
    const struct ios_input_event *event
)
{
    if (inspector == NULL || event == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    if (!inspector->visible || event->type != IOS_INPUT_EVENT_KEY
        || (event->flags & IOS_INPUT_PRESSED) == 0) return IOS_OK;
    if (event->code == IOS_KEY_ESCAPE) {
        ios_file_explorer_diagnostic_inspector_close(inspector);
    } else if (event->code == IOS_KEY_LEFT && inspector->page > 0) {
        inspector->page = (enum ios_file_explorer_diagnostic_page)(inspector->page - 1);
    } else if (event->code == IOS_KEY_RIGHT
               && inspector->page + 1 < IOS_FILE_EXPLORER_DIAGNOSTIC_PAGE_COUNT) {
        inspector->page = (enum ios_file_explorer_diagnostic_page)(inspector->page + 1);
    }
    return IOS_OK;
}
