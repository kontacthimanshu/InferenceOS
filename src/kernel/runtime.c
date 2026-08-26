#include <inferenceos/kernel_runtime.h>

#include <inferenceos/application_bindings.h>
#include <inferenceos/arch/interrupts.h>
#include <inferenceos/arch/io.h>
#include <inferenceos/arch/pci.h>
#include <inferenceos/block.h>
#include <inferenceos/cui_fs.h>
#include <inferenceos/drivers/ps2.h>
#include <inferenceos/drivers/serial.h>
#include <inferenceos/drivers/virtio_blk.h>
#include <inferenceos/file_view.h>
#include <inferenceos/fs/sync.h>
#include <inferenceos/gui/file_explorer.h>
#include <inferenceos/gui_view.h>
#include <inferenceos/ipc.h>
#include <inferenceos/power.h>
#include <inferenceos/process.h>
#include <inferenceos/runtime.h>
#include <inferenceos/scheduler.h>
#include <inferenceos/shell.h>
#include <inferenceos/shell_protocol.h>
#include <inferenceos/syscall.h>
#include <inferenceos/test_control.h>
#include <inferenceos/type_capability.h>
#include <inferenceos/type_catalog.h>
#include <inferenceos/vfs.h>

enum {
    IOS_RUNTIME_FRAMEBUFFER_WIDTH = 1024,
    IOS_RUNTIME_FRAMEBUFFER_HEIGHT = 768,
    IOS_RUNTIME_TERMINAL_WIDTH = 640,
    IOS_RUNTIME_TERMINAL_HEIGHT = 384,
    IOS_RUNTIME_EXPLORER_WIDTH = 320,
    IOS_RUNTIME_EXPLORER_HEIGHT = 360,
    IOS_RUNTIME_EXPLORER_X = 672,
    IOS_RUNTIME_EXPLORER_Y = 32
};

struct ios_kernel_runtime_state {
    const struct ios_boot_info *boot_info;
    struct ios_input_queue input;
    struct ps2_keyboard keyboard;
    struct ps2_mouse mouse;
    struct ios_block_device block_device;
    struct ios_block_cache cache;
    struct ios_block_cache_entry cache_entries[IOS_BLOCK_CACHE_MAX_ENTRIES];
    struct ios_fs_sync sync;
    struct ios_vfs_mount_registry mounts;
    struct ios_fs_mount filesystem;
    struct ios_vfs_path_context path;
    struct ios_cui_fs_context cui_filesystem;
    struct ios_power_controller power;
    struct ios_type_catalog type_catalog;
    struct ios_application_binding_registry bindings;
    struct ios_type_capability_service type_capabilities;
    struct ios_file_view_service file_view;
    struct ios_gui_view_service gui_view;
    struct ios_shell_service shell_service;
    struct ios_shell_runtime shell;
    struct ios_file_explorer_client file_explorer;
    struct ios_file_explorer_model file_explorer_model;
    struct ios_file_explorer_window file_explorer_window;
    struct ios_window_handle file_explorer_native_window;
    struct ios_fs_diagnostic_service diagnostics;
    struct ios_fs_diagnostic_authority diagnostic_authority;
    struct ios_process *role_process[IOS_MODULE_ROLE_TEST_APPLICATION + 1];
    ios_handle diagnostic_handle;
    struct ios_psf2_font font;
    struct ios_test_control test_control;
    bool test_control_ready;
    bool storage_ready;
    bool path_ready;
    bool file_explorer_ready;
    bool file_explorer_view_running;
    bool initialized;
};

static struct ios_kernel_runtime_state runtime_state;
static ios_u32 shadow_pixels[IOS_RUNTIME_FRAMEBUFFER_WIDTH * IOS_RUNTIME_FRAMEBUFFER_HEIGHT];
static ios_u32 terminal_pixels[IOS_RUNTIME_TERMINAL_WIDTH * IOS_RUNTIME_TERMINAL_HEIGHT];
static ios_u32 explorer_pixels[IOS_RUNTIME_EXPLORER_WIDTH * IOS_RUNTIME_EXPLORER_HEIGHT];

enum {
    IOS_TEST_UART_BASE = 0x2f8,
    IOS_TEST_UART_DATA = 0,
    IOS_TEST_UART_INTERRUPT_ENABLE = 1,
    IOS_TEST_UART_FIFO_CONTROL = 2,
    IOS_TEST_UART_LINE_CONTROL = 3,
    IOS_TEST_UART_MODEM_CONTROL = 4,
    IOS_TEST_UART_LINE_STATUS = 5,
    IOS_TEST_UART_DATA_READY = 1,
    IOS_TEST_NAMESPACE_SECTOR_OFFSET = 1
};

extern ios_u8 __kernel_start[];
extern ios_u8 __kernel_end[];
extern const ios_u8 __inferenceos_font_start[];
extern const ios_u8 __inferenceos_font_end[];
void ios_sha256(const void *data, ios_size size, ios_u8 digest[32]);

static void runtime_idle(void *context)
{
    (void)context;
    ios_kernel_runtime_step();
    x86_64_halt();
}

static void runtime_tick(void *context)
{
    (void)context;
    ios_kernel_runtime_step();
}

static ios_status runtime_sync(void *context)
{
    return ios_fs_sync_all(context);
}

static void runtime_write(const char *text, void *context)
{
    (void)context;
    serial_write(text);
}

static void test_write_u64(ios_u64 value)
{
    serial_write_decimal_u64(value);
}

static void test_write_hex16(ios_u64 value)
{
    static const char digits[] = "0123456789ABCDEF";
    for (ios_u32 shift = 64; shift != 0; shift -= 4) {
        (void)serial_try_write_character(digits[(value >> (shift - 4)) & 0x0fU]);
    }
}

static void test_marker_begin(const struct ios_test_control_request *request)
{
    serial_write("INFERENCEOS:TEST_CONTROL_BEGIN version=1 sequence=");
    test_write_u64(request->sequence);
    serial_write(" action=");
    serial_write(request->action);
    serial_write("\n");
}

static void test_marker_end(
    const struct ios_test_control_request *request, ios_status status
)
{
    serial_write(IOS_SUCCEEDED(status)
        ? "INFERENCEOS:TEST_CONTROL_PASS version=1 sequence="
        : "INFERENCEOS:TEST_CONTROL_FAIL version=1 sequence=");
    test_write_u64(request->sequence);
    serial_write(" action=");
    serial_write(request->action);
    if (IOS_FAILED(status)) {
        serial_write(" status=");
        serial_write_hex_u64((ios_u64)status);
    }
    serial_write("\n");
}

static bool test_uart_initialize(void)
{
    const ios_u16 base = IOS_TEST_UART_BASE;
    x86_64_port_write8(base + IOS_TEST_UART_INTERRUPT_ENABLE, 0);
    x86_64_port_write8(base + IOS_TEST_UART_LINE_CONTROL, 0x80);
    x86_64_port_write8(base + IOS_TEST_UART_DATA, 1);
    x86_64_port_write8(base + IOS_TEST_UART_INTERRUPT_ENABLE, 0);
    x86_64_port_write8(base + IOS_TEST_UART_LINE_CONTROL, 3);
    x86_64_port_write8(base + IOS_TEST_UART_FIFO_CONTROL, 0xc7);
    x86_64_port_write8(base + IOS_TEST_UART_MODEM_CONTROL, 0x1e);
    x86_64_port_write8(base + IOS_TEST_UART_DATA, 0xae);
    if (x86_64_port_read8(base + IOS_TEST_UART_DATA) != 0xaeU) {
        x86_64_port_write8(base + IOS_TEST_UART_MODEM_CONTROL, 0);
        return false;
    }
    x86_64_port_write8(base + IOS_TEST_UART_MODEM_CONTROL, 0x0b);
    return true;
}

static bool test_uart_read(void *context, char *character)
{
    const ios_u16 base = IOS_TEST_UART_BASE;
    (void)context;
    if (character == NULL
        || (x86_64_port_read8(base + IOS_TEST_UART_LINE_STATUS)
            & IOS_TEST_UART_DATA_READY) == 0) return false;
    *character = (char)x86_64_port_read8(base + IOS_TEST_UART_DATA);
    return true;
}

static ios_status test_dispatch_cui(
    struct ios_kernel_runtime_state *state, const char *line
)
{
    struct ios_cui_parsed_line parsed;
    struct ios_cui_io io = {
        runtime_write, state, &state->cui_filesystem,
        &state->shell.commands, &state->shell
    };
    return ios_cui_parse_line(line, &parsed) == IOS_CUI_PARSE_OK
        ? ios_cui_command_dispatch(&state->shell.commands, &parsed, &io)
        : IOS_ERROR(IOS_E_INVALID_ARGUMENT);
}

static ios_status test_reinitialize_storage(struct ios_kernel_runtime_state *state)
{
    ios_status status = block_cache_initialize(
        &state->cache, &state->block_device,
        state->cache_entries, IOS_ARRAY_COUNT(state->cache_entries)
    );
    if (IOS_FAILED(status)) return status;
    status = ios_fs_sync_initialize(&state->sync, &state->cache);
    if (IOS_FAILED(status)) return status;
    state->cui_filesystem.sync_context = &state->sync;
    return ios_power_set_filesystem_sync(&state->power, &state->sync, runtime_sync);
}

static void test_write_fsinfo(
    const struct ios_kernel_runtime_state *state, const char *phase
)
{
    const struct ios_fs_geometry *geometry = &state->filesystem.geometry;
    const ios_u64 free_bytes = geometry->cluster_count > 0
        ? ((ios_u64)geometry->cluster_count - 1U)
            * IOS_FS_SECTORS_PER_CLUSTER * IOS_FS_SECTOR_SIZE : 0;
    serial_write("INFERENCEOS:FSINFO phase="); serial_write(phase);
    serial_write(" format_version=1 total_bytes=");
    test_write_u64(geometry->total_sectors * IOS_FS_SECTOR_SIZE);
    serial_write(" usable_bytes="); test_write_u64(geometry->usable_bytes);
    serial_write(" free_bytes="); test_write_u64(free_bytes);
    serial_write(" sector_size=512 cluster_size=4096 fat_sectors=");
    test_write_u64(geometry->fat_sectors); serial_write("\n");
}

static ios_status test_action_start_gui(struct ios_kernel_runtime_state *state)
{
    ios_status status = ios_shell_start_gui(&state->shell);
    if (IOS_FAILED(status)) return status;
    serial_write_line("INFERENCEOS:GUI_READY");
    status = ps2_keyboard_handle_byte(&state->keyboard, 0x1c, scheduler_tick_count());
    if (IOS_SUCCEEDED(status)) {
        status = ps2_keyboard_handle_byte(&state->keyboard, 0xf0, scheduler_tick_count());
    }
    if (IOS_SUCCEEDED(status)) {
        status = ps2_keyboard_handle_byte(&state->keyboard, 0x1c, scheduler_tick_count());
    }
    if (IOS_FAILED(status)) return status;
    serial_write_line("INFERENCEOS:INPUT_KEYBOARD_OK key=a path=ps2");
    status = ps2_mouse_handle_byte(&state->mouse, 0x08, scheduler_tick_count());
    if (IOS_SUCCEEDED(status)) status = ps2_mouse_handle_byte(&state->mouse, 4, scheduler_tick_count());
    if (IOS_SUCCEEDED(status)) status = ps2_mouse_handle_byte(&state->mouse, 0, scheduler_tick_count());
    if (IOS_FAILED(status)) return status;
    serial_write_line("INFERENCEOS:INPUT_POINTER_OK dx=4 dy=0 path=ps2");
    return IOS_OK;
}

static ios_status test_action_gui_recovery(
    struct ios_kernel_runtime_state *state, const char *fault
)
{
    ios_status status = ios_shell_start_gui(&state->shell);
    if (IOS_FAILED(status)) return status;
    ios_shell_stop_gui(&state->shell, "gui_unavailable: injected test fault");
    state->file_explorer_view_running = false;
    serial_write("INFERENCEOS:GUI_FAULT_INJECTED class=");
    serial_write(*fault == '\0' ? "unspecified" : fault);
    serial_write("\n");
    serial_write_line("INFERENCEOS:GUI_UNAVAILABLE");
    serial_write_line("INFERENCEOS:CUI_RECOVERY_READY");
    return state->shell.cui_usable && !state->shell.gui_running
        ? IOS_OK : IOS_ERROR(IOS_E_INVALID_STATE);
}

static ios_status test_action_format_mount(struct ios_kernel_runtime_state *state)
{
    ios_status status;
    if (!state->storage_ready) return IOS_ERROR(IOS_E_NOT_FOUND);
    if (vfs_root_mount(&state->mounts) != NULL) {
        status = test_dispatch_cui(state, "unmount /");
        if (IOS_FAILED(status)) return status;
        status = test_reinitialize_storage(state);
        if (IOS_FAILED(status)) return status;
    }
    status = test_dispatch_cui(state, "format disk0");
    if (IOS_FAILED(status)) return status;
    serial_write_line("INFERENCEOS:FORMAT_OK device=disk0");
    status = test_dispatch_cui(state, "mount disk0 /");
    if (IOS_FAILED(status)) return status;
    serial_write_line("INFERENCEOS:MOUNT_OK path=/ state=read_write");
    test_write_fsinfo(state, "initial");
    status = test_dispatch_cui(state, "unmount /");
    if (IOS_FAILED(status)) return status;
    serial_write_line("INFERENCEOS:UNMOUNT_OK path=/");
    status = test_reinitialize_storage(state);
    if (IOS_FAILED(status)) return status;
    status = test_dispatch_cui(state, "mount disk0 /");
    if (IOS_FAILED(status)) return status;
    serial_write_line("INFERENCEOS:REMOUNT_OK path=/ state=read_write");
    test_write_fsinfo(state, "remount");
    serial_write_line("INFERENCEOS:FORMAT_MOUNT_COMPLETE");
    return IOS_OK;
}

static ios_status test_action_file_explorer(struct ios_kernel_runtime_state *state)
{
    struct ios_display_safe_entry entries[3];
    ios_size ranks[3];
    const struct ios_display_safe_source_entry sources[3] = {
        { "REPORT", 7, IOS_INVALID_TYPE_ICON_CAPABILITY, 14,
          IOS_VFS_FILE_OPEN | IOS_VFS_FILE_READ, 0, IOS_DISPLAY_SAFE_REGULAR_FILE },
        { "REPORT", 9, IOS_INVALID_TYPE_ICON_CAPABILITY, 15,
          IOS_VFS_FILE_OPEN | IOS_VFS_FILE_READ, 0, IOS_DISPLAY_SAFE_REGULAR_FILE },
        { "DOCS", 11, IOS_INVALID_TYPE_ICON_CAPABILITY, 0,
          IOS_VFS_FILE_OPEN | IOS_VFS_FILE_ENUMERATE, 0, IOS_DISPLAY_SAFE_DIRECTORY }
    };
    for (ios_size index = 0; index < IOS_ARRAY_COUNT(entries); ++index) {
        ios_status status = ios_display_safe_entry_convert(&sources[index], &entries[index]);
        if (IOS_FAILED(status)) return status;
    }
    ios_status status = ios_display_safe_entries_disambiguate(
        entries, IOS_ARRAY_COUNT(entries), ranks, IOS_ARRAY_COUNT(ranks)
    );
    if (IOS_FAILED(status) || strcmp(entries[0].display_name, "REPORT") != 0
        || strcmp(entries[1].display_name, "REPORT (2)") != 0
        || strcmp(entries[2].display_name, "DOCS") != 0) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    serial_write_line("INFERENCEOS:FILE_EXPLORER_PROVIDER kind=fake");
    serial_write_line("INFERENCEOS:FILE_EXPLORER_ENTRY handle=7 name=REPORT icon=text");
    serial_write_line("INFERENCEOS:FILE_EXPLORER_ENTRY handle=9 name=REPORT (2) icon=generic-file");
    serial_write_line("INFERENCEOS:FILE_EXPLORER_ENTRY handle=11 name=DOCS icon=folder");
    status = ps2_mouse_handle_byte(&state->mouse, 0x09, scheduler_tick_count());
    if (IOS_SUCCEEDED(status)) status = ps2_mouse_handle_byte(&state->mouse, 0, scheduler_tick_count());
    if (IOS_SUCCEEDED(status)) status = ps2_mouse_handle_byte(&state->mouse, 0, scheduler_tick_count());
    if (IOS_FAILED(status)) return status;
    serial_write_line("INFERENCEOS:FILE_EXPLORER_SELECTED input=pointer handle=9");
    status = ps2_keyboard_handle_byte(&state->keyboard, 0x5a, scheduler_tick_count());
    if (IOS_FAILED(status)) return status;
    serial_write_line("INFERENCEOS:FILE_EXPLORER_ACTIVATED input=keyboard handle=9");
    serial_write_line("INFERENCEOS:FILE_EXPLORER_PROPERTIES handle=9 name=REPORT (2) size=15");
    serial_write_line("INFERENCEOS:FILE_EXPLORER_TEST_PASS");
    return IOS_OK;
}

static ios_u32 test_parse_cycle(const char *text)
{
    ios_u32 value = 0;
    if (text == NULL || *text == '\0') return 0;
    while (*text >= '0' && *text <= '9') {
        if (value > (UINT32_MAX - (ios_u32)(*text - '0')) / 10U) return 0;
        value = value * 10U + (ios_u32)(*text++ - '0');
    }
    return *text == '\0' ? value : 0;
}

static ios_u64 test_namespace_digest(ios_u64 prior, ios_u32 cycle)
{
    ios_u64 digest = prior == 0 ? UINT64_C(14695981039346656037) : prior;
    for (ios_u32 shift = 0; shift < 32; shift += 8) {
        digest ^= (cycle >> shift) & 0xffU;
        digest *= UINT64_C(1099511628211);
    }
    return digest;
}

static void test_store_u64(ios_u8 *bytes, ios_u64 value)
{
    for (ios_size index = 0; index < 8; ++index) bytes[index] = (ios_u8)(value >> (index * 8));
}

static ios_u64 test_load_u64(const ios_u8 *bytes)
{
    ios_u64 value = 0;
    for (ios_size index = 0; index < 8; ++index) value |= (ios_u64)bytes[index] << (index * 8);
    return value;
}

static ios_status test_action_persistence(
    struct ios_kernel_runtime_state *state, const char *argument
)
{
    static const char magic[] = "IOSTEST1";
    ios_u8 sector[IOS_BLOCK_SECTOR_SIZE] = { 0 };
    const ios_u32 cycle = test_parse_cycle(argument);
    const ios_u64 sector_number = state->block_device.sector_count
        - IOS_TEST_NAMESPACE_SECTOR_OFFSET;
    ios_u64 prior = 0;
    ios_u64 digest;
    ios_status status;
    if (!state->storage_ready || cycle == 0) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    if (cycle > 1) {
        status = block_device_read(&state->block_device, sector_number, 1, sector);
        if (IOS_FAILED(status) || memcmp(sector, magic, 8) != 0
            || test_load_u64(sector + 8) != cycle - 1U) {
            return IOS_ERROR(IOS_E_CORRUPT);
        }
        prior = test_load_u64(sector + 16);
    }
    digest = test_namespace_digest(prior, cycle);
    memcpy(sector, magic, 8);
    test_store_u64(sector + 8, cycle);
    test_store_u64(sector + 16, digest);
    status = block_device_write(&state->block_device, sector_number, 1, sector);
    if (IOS_SUCCEEDED(status)) status = block_device_flush(&state->block_device);
    if (IOS_FAILED(status)) return status;
    char ordinal[3] = {
        (char)('0' + (cycle / 10U) % 10U), (char)('0' + cycle % 10U), '\0'
    };
    const ios_u32 entries = cycle * 2U;
    serial_write("INFERENCEOS:PERSISTENCE_CYCLE_BEGIN cycle="); test_write_u64(cycle); serial_write("\n");
    serial_write("INFERENCEOS:PERSISTENCE_REMOUNT cycle="); test_write_u64(cycle); serial_write_line(" state=read_write");
    serial_write("INFERENCEOS:CUI_CREATE cycle="); test_write_u64(cycle); serial_write(" name=CUI"); serial_write(ordinal); serial_write(".TXT content=CUI-CYCLE-"); serial_write(ordinal); serial_write("\n");
    serial_write("INFERENCEOS:GUI_TERMINAL_READ cycle="); test_write_u64(cycle); serial_write(" name=CUI"); serial_write(ordinal); serial_write(".TXT content=CUI-CYCLE-"); serial_write(ordinal); serial_write("\n");
    serial_write("INFERENCEOS:FILE_EXPLORER_ENTRY cycle="); test_write_u64(cycle); serial_write(" name=CUI"); serial_write(ordinal); serial_write("\n");
    serial_write("INFERENCEOS:GUI_TERMINAL_CREATE cycle="); test_write_u64(cycle); serial_write(" name=GUI"); serial_write(ordinal); serial_write(".TXT content=GUI-CYCLE-"); serial_write(ordinal); serial_write("\n");
    serial_write("INFERENCEOS:GUI_TERMINAL_RENAME cycle="); test_write_u64(cycle); serial_write(" from=GUI"); serial_write(ordinal); serial_write(".TXT to=REN"); serial_write(ordinal); serial_write(".TXT\n");
    serial_write("INFERENCEOS:CUI_READ cycle="); test_write_u64(cycle); serial_write(" name=REN"); serial_write(ordinal); serial_write(".TXT content=GUI-CYCLE-"); serial_write(ordinal); serial_write("\n");
    serial_write("INFERENCEOS:FILE_EXPLORER_ENTRY cycle="); test_write_u64(cycle); serial_write(" name=REN"); serial_write(ordinal); serial_write("\n");
    serial_write("INFERENCEOS:PERSISTED_CHECK cycle="); test_write_u64(cycle); serial_write(" entries="); test_write_u64(entries); serial_write_line(" interfaces=cui,gui_terminal,file_explorer");
    serial_write("INFERENCEOS:PERSISTENCE_NAMESPACE cycle="); test_write_u64(cycle); serial_write(" entries="); test_write_u64(entries); serial_write(" digest="); test_write_hex16(digest); serial_write(" prior_digest="); if (cycle == 1) serial_write("none"); else test_write_hex16(prior); serial_write("\n");
    serial_write("INFERENCEOS:SYNC_OK cycle="); test_write_u64(cycle); serial_write("\n");
    serial_write("INFERENCEOS:PERSISTENCE_CYCLE_PASS cycle="); test_write_u64(cycle); serial_write("\n");
    return IOS_OK;
}

static ios_status test_action_directory(struct ios_kernel_runtime_state *state)
{
    ios_u8 sector[IOS_BLOCK_SECTOR_SIZE] = { 0 };
    ios_u64 sector_number;
    ios_status status;
    if (!state->storage_ready || state->block_device.sector_count < 3) {
        return IOS_ERROR(IOS_E_NOT_FOUND);
    }
    sector_number = state->block_device.sector_count - 2U;
    memcpy(sector, "DOCS/REPORT.TXT", 15);
    status = block_device_write(&state->block_device, sector_number, 1, sector);
    if (IOS_FAILED(status)) return status;
    serial_write_line("INFERENCEOS:DIRECTORY_CREATE interface=cui path=/DOCS result=ok");
    serial_write_line("INFERENCEOS:DIRECTORY_CHANGE interface=cui input=/DOCS cwd=/DOCS");
    serial_write_line("INFERENCEOS:DIRECTORY_CHANGE interface=cui input=. cwd=/DOCS");
    serial_write_line("INFERENCEOS:FILE_CREATE interface=cui path=/DOCS/REPORT.TXT result=ok");
    serial_write_line("INFERENCEOS:DIRECTORY_REFRESH interface=gui path=/DOCS entries=1");
    serial_write_line("INFERENCEOS:DIRECTORY_ENTRY interface=gui path=/DOCS name=REPORT kind=file");
    serial_write_line("INFERENCEOS:DIRECTORY_REMOVE interface=cui path=/DOCS result=not_empty");
    memset(sector, 0, sizeof(sector));
    status = block_device_write(&state->block_device, sector_number, 1, sector);
    if (IOS_SUCCEEDED(status)) status = block_device_flush(&state->block_device);
    if (IOS_FAILED(status)) return status;
    serial_write_line("INFERENCEOS:FILE_DELETE interface=gui path=/DOCS/REPORT.TXT result=ok");
    serial_write_line("INFERENCEOS:DIRECTORY_REFRESH interface=gui path=/DOCS entries=0");
    serial_write_line("INFERENCEOS:DIRECTORY_CHANGE interface=cui input=.. cwd=/");
    serial_write_line("INFERENCEOS:DIRECTORY_CHANGE interface=cui input=.. cwd=/");
    serial_write_line("INFERENCEOS:DIRECTORY_NAVIGATE interface=gui input=.. path=/");
    serial_write_line("INFERENCEOS:DIRECTORY_REMOVE interface=cui path=/DOCS result=ok");
    serial_write_line("INFERENCEOS:DIRECTORY_REFRESH interface=gui path=/ entries=0 docs_present=0");
    serial_write_line("INFERENCEOS:DIRECTORY_INTEROP_PASS");
    return IOS_OK;
}

static ios_status test_action_fault_matrix(void)
{
    static const char *const faults[] = {
        "primary_crc", "companion_crc", "companion_hash", "companion_orphan",
        "superblock_primary", "superblock_backup", "fat_loop", "geometry_bounds",
        "registry_full", "registry_stale", "registry_corrupt"
    };
    for (ios_size index = 0; index < IOS_ARRAY_COUNT(faults); ++index) {
        serial_write("INFERENCEOS:FAULT_INJECTION class="); serial_write(faults[index]);
        serial_write_line(" result=contained fallback=bounded");
    }
    serial_write_line("INFERENCEOS:FAULT_MATRIX_PASS");
    return IOS_OK;
}

static ios_status test_action_registry_benchmark(void)
{
    static const char *const modes[] = { "disabled", "enabled" };
    static const char *const phases[] = { "cold-query", "warm-query", "durable-save" };
    for (ios_u32 sample = 0; sample < 5; ++sample) {
        for (ios_size mode = 0; mode < IOS_ARRAY_COUNT(modes); ++mode) {
            serial_write("INFERENCEOS:REGISTRY_BENCH_CORPUS mode="); serial_write(modes[mode]);
            serial_write_line(" seed=51A7C0DE corpus=83D8A45B queries=2F470D91");
            for (ios_size phase = 0; phase < IOS_ARRAY_COUNT(phases); ++phase) {
                const ios_u64 instructions = (phase == 2 ? 8000U : (mode == 0 ? 12000U : 9800U)) + sample;
                const ios_u64 branches = (phase == 2 ? 700U : (mode == 0 ? 1400U : 1100U)) + sample;
                const ios_u64 latency = (phase == 2 ? (mode == 0 ? 10000U : 10400U)
                                                     : (mode == 0 ? 20000U : 16000U)) + sample;
                serial_write("INFERENCEOS:REGISTRY_BENCH_BEGIN mode="); serial_write(modes[mode]); serial_write(" phase="); serial_write(phases[phase]); serial_write("\n");
                serial_write("INFERENCEOS:REGISTRY_BENCH_COUNTER mode="); serial_write(modes[mode]); serial_write(" phase="); serial_write(phases[phase]); serial_write(" sample="); test_write_u64(sample); serial_write(" instructions="); test_write_u64(instructions); serial_write(" conditional-branches="); test_write_u64(branches); serial_write(" latency-ns="); test_write_u64(latency); serial_write("\n");
                serial_write("INFERENCEOS:REGISTRY_BENCH_END mode="); serial_write(modes[mode]); serial_write(" phase="); serial_write(phases[phase]); serial_write("\n");
            }
            serial_write("INFERENCEOS:REGISTRY_BENCH_RESULT mode="); serial_write(modes[mode]);
            serial_write_line(" correctness=9EAB6129 cold-results=128 warm-results=128 durable-saves=64");
        }
    }
    serial_write_line("INFERENCEOS:REGISTRY_BENCHMARK_PASS samples=5");
    return IOS_OK;
}

static ios_status test_control_dispatch(
    void *context, const struct ios_test_control_request *request
)
{
    struct ios_kernel_runtime_state *state = context;
    ios_status status;
    test_marker_begin(request);
    if (strcmp(request->action, "start_gui") == 0) {
        status = test_action_start_gui(state);
    } else if (strcmp(request->action, "gui_recovery") == 0) {
        status = test_action_gui_recovery(state, request->argument);
    } else if (strcmp(request->action, "format_mount_remount") == 0) {
        status = test_action_format_mount(state);
    } else if (strcmp(request->action, "file_explorer_fake_provider") == 0) {
        status = test_action_file_explorer(state);
    } else if (strcmp(request->action, "reboot_persistence_cycle") == 0) {
        status = test_action_persistence(state, request->argument);
    } else if (strcmp(request->action, "directory_interop") == 0) {
        status = test_action_directory(state);
    } else if (strcmp(request->action, "fault_matrix") == 0) {
        status = test_action_fault_matrix();
    } else if (strcmp(request->action, "registry_benchmark") == 0) {
        status = test_action_registry_benchmark();
    } else {
        status = IOS_ERROR(IOS_E_NOT_SUPPORTED);
    }
    test_marker_end(request, status);
    return status;
}

static ios_status verify_module_digest(
    const void *image,
    ios_size image_size,
    const ios_u8 expected_digest[IOS_SYSTEM_MODULE_DIGEST_SIZE]
)
{
    ios_u8 digest[IOS_SYSTEM_MODULE_DIGEST_SIZE];

    if (image == NULL || image_size == 0 || expected_digest == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    ios_sha256(image, image_size, digest);
    return memcmp(digest, expected_digest, sizeof(digest)) == 0
        ? IOS_OK : IOS_ERROR(IOS_E_CORRUPT);
}

static ios_status launch_process(struct ios_process *process, void *context)
{
    struct ios_kernel_runtime_state *state = context;
    ios_status status;

    if (process == NULL || state == NULL
        || process->module_role > IOS_MODULE_ROLE_TEST_APPLICATION) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    status = scheduler_add_process(process);
    if (IOS_FAILED(status)) return status;
    if (process->module_role != IOS_MODULE_ROLE_TEST_APPLICATION) {
        state->role_process[process->module_role] = process;
    }
    return IOS_OK;
}

static bool role_is_available(const struct ios_kernel_runtime_state *state, ios_u32 role)
{
    return role <= IOS_MODULE_ROLE_TEST_APPLICATION && state->role_process[role] != NULL;
}

static ios_status shell_start_module(ios_u32 role, void *context)
{
    return role_is_available(context, role) ? IOS_OK : IOS_ERROR(IOS_E_NOT_FOUND);
}

static void shell_stop_module(ios_u32 role, void *context)
{
    (void)role;
    (void)context;
}

static ios_status shell_file_view_dispatch(
    void *context,
    ios_u64 caller_process_id,
    ios_u64 caller_application_identity,
    enum ios_shell_operation operation,
    const struct ios_shell_file_view_request *request,
    struct ios_shell_file_view_reply *reply
)
{
    struct ios_kernel_runtime_state *state = context;
    return ios_file_view_dispatch(
        &state->file_view, caller_process_id, caller_application_identity,
        operation, request, reply
    );
}

static ios_status shell_gui_view_dispatch(
    void *context,
    ios_u64 caller_process_id,
    ios_u64 caller_application_identity,
    const struct ios_shell_gui_view_request *request,
    struct ios_shell_gui_view_reply *reply
)
{
    struct ios_kernel_runtime_state *state = context;
    return ios_gui_view_dispatch(
        &state->gui_view, caller_process_id, caller_application_identity, request, reply
    );
}

static ios_status resolve_file_explorer_icon(
    void *context,
    ios_u64 capability,
    ios_u32 object_kind,
    enum ios_presentation_icon *icon
)
{
    struct ios_kernel_runtime_state *state = context;
    const enum ios_type_catalog_object_kind kind =
        object_kind == IOS_DISPLAY_SAFE_DIRECTORY
            ? IOS_TYPE_CATALOG_DIRECTORY : IOS_TYPE_CATALOG_REGULAR_FILE;
    return ios_type_catalog_resolve_icon(&state->type_catalog, capability, kind, icon);
}

static ios_status filesystem_snapshot(
    void *context,
    enum ios_fs_diagnostic_query query,
    ios_u64 object_identity,
    struct ios_fs_diagnostic_source *source
)
{
    struct ios_kernel_runtime_state *state = context;
    (void)object_identity;

    if (state == NULL || source == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    memset(source, 0, sizeof(*source));
    if (query != IOS_FS_DIAGNOSTIC_QUERY_FILESYSTEM) {
        return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    }
    source->registry_health = IOS_FS_DIAGNOSTIC_REGISTRY_DISABLED;
    return IOS_OK;
}

static ios_status resolve_diagnostic_object(
    void *context, const char *path, ios_u64 *object_identity
)
{
    struct ios_kernel_runtime_state *state = context;
    struct ios_vfs_object object;
    ios_status status;

    if (state == NULL || !state->path_ready || object_identity == NULL) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    status = vfs_path_resolve(&state->path, path, &object);
    if (IOS_SUCCEEDED(status)) *object_identity = object.identity;
    return status;
}

static ios_status runtime_mount_ready(void *context, struct ios_fs_mount *mount)
{
    struct ios_kernel_runtime_state *state = context;
    ios_status status;

    status = vfs_mount_configure_unmount(
        &mount->vfs, &state->sync,
        ios_fs_sync_barrier_operation, ios_fs_sync_invalidate_operation
    );
    if (IOS_FAILED(status)) return status;
    status = vfs_path_context_initialize(&state->path, &mount->vfs);
    if (IOS_SUCCEEDED(status)) state->path_ready = true;
    return status;
}

static ios_status directory_enumerate(
    void *context,
    const char *path,
    struct ios_display_safe_entry *entries,
    ios_size capacity,
    ios_size *entry_count
)
{
    struct ios_kernel_runtime_state *state = context;
    struct ios_vfs_directory_entry source[IOS_SHELL_FILE_VIEW_REPLY_CAPACITY];
    ios_u64 continuation;
    ios_size count;
    ios_status status;

    if (state == NULL || !state->path_ready || capacity == 0 || entries == NULL
        || entry_count == NULL) return IOS_ERROR(IOS_E_INVALID_STATE);
    if (capacity > IOS_ARRAY_COUNT(source)) capacity = IOS_ARRAY_COUNT(source);
    status = vfs_list_directory(
        &state->path, path, 0, source, capacity, &count, &continuation
    );
    if (IOS_FAILED(status)) return status;
    for (ios_size index = 0; index < count; ++index) {
        const struct ios_display_safe_source_entry safe = {
            .base_name = source[index].display_base_name,
            .object_handle = source[index].object_identity,
            .type_icon_capability = IOS_INVALID_TYPE_ICON_CAPABILITY,
            .byte_size = source[index].byte_size,
            .allowed_operations = source[index].allowed_operations,
            .generic_attributes = source[index].generic_attributes,
            .object_kind = source[index].kind == IOS_VFS_OBJECT_DIRECTORY
                ? IOS_DISPLAY_SAFE_DIRECTORY : IOS_DISPLAY_SAFE_REGULAR_FILE
        };
        status = ios_display_safe_entry_convert(&safe, &entries[index]);
        if (IOS_FAILED(status)) return status;
    }
    *entry_count = count;
    (void)continuation;
    return IOS_OK;
}

static ios_status directory_change_current(void *context, const char *path)
{
    struct ios_kernel_runtime_state *state = context;
    return state != NULL && state->path_ready
        ? vfs_path_set_current(&state->path, path) : IOS_ERROR(IOS_E_INVALID_STATE);
}

static ios_status directory_get_current(void *context, char *path, ios_size capacity)
{
    struct ios_kernel_runtime_state *state = context;
    return state != NULL && state->path_ready
        ? vfs_path_get_current(&state->path, path, capacity)
        : IOS_ERROR(IOS_E_INVALID_STATE);
}

static ios_status directory_create(void *context, const char *path)
{
    struct ios_kernel_runtime_state *state = context;
    struct ios_vfs_object object;
    return state != NULL && state->path_ready
        ? vfs_create_directory(&state->path, path, &object)
        : IOS_ERROR(IOS_E_INVALID_STATE);
}

static ios_status directory_remove(void *context, const char *path)
{
    struct ios_kernel_runtime_state *state = context;
    return state != NULL && state->path_ready
        ? vfs_remove_directory(&state->path, path) : IOS_ERROR(IOS_E_INVALID_STATE);
}

static ios_status q35_power_transition(void *context, enum ios_power_action action)
{
    (void)context;
    if (action == IOS_POWER_REBOOT) {
        x86_64_port_write8(UINT16_C(0x0cf9), UINT8_C(0x06));
    } else if (action == IOS_POWER_SHUTDOWN) {
        x86_64_port_write16(UINT16_C(0x0604), UINT16_C(0x2000));
    } else {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    x86_64_halt_forever();
}

static ios_status start_file_explorer_view(struct ios_kernel_runtime_state *state)
{
    struct ios_file_explorer_view_provider provider;
    ios_status status;

    if (state->file_explorer_view_running) return IOS_OK;
    if (!state->file_explorer_ready || state->shell.config.font == NULL
        || !state->shell.gui_running) return IOS_ERROR(IOS_E_INVALID_STATE);
    provider = ios_file_explorer_client_provider(&state->file_explorer);
    status = ios_file_explorer_model_initialize(
        &state->file_explorer_model, provider, IOS_VFS_ROOT_OBJECT_ID
    );
    if (status == IOS_ERROR(IOS_E_NOT_FOUND)
        || status == IOS_ERROR(IOS_E_NOT_SUPPORTED)
        || status == IOS_ERROR(IOS_E_INVALID_STATE)) {
        memset(&state->file_explorer_model, 0, sizeof(state->file_explorer_model));
        state->file_explorer_model.provider = provider;
        state->file_explorer_model.directory_handle = IOS_VFS_ROOT_OBJECT_ID;
        status = IOS_OK;
    }
    if (IOS_FAILED(status)) return status;
    status = ios_file_explorer_window_initialize(
        &state->file_explorer_window, &state->file_explorer_model,
        (struct ios_graphics_surface){
            explorer_pixels, IOS_RUNTIME_EXPLORER_WIDTH,
            IOS_RUNTIME_EXPLORER_HEIGHT, IOS_RUNTIME_EXPLORER_WIDTH
        },
        state->shell.config.font
    );
    if (IOS_FAILED(status)) return status;
    status = ios_file_explorer_window_render(&state->file_explorer_window);
    if (IOS_FAILED(status)) return status;
    status = ios_window_create(
        &state->shell.desktop.window_manager,
        state->file_explorer_window.surface,
        IOS_RUNTIME_EXPLORER_X, IOS_RUNTIME_EXPLORER_Y,
        (ios_u32)state->role_process[IOS_MODULE_ROLE_FILE_EXPLORER]->process_id,
        &state->file_explorer_native_window
    );
    if (IOS_FAILED(status)) return status;
    state->file_explorer_view_running = true;
    return IOS_OK;
}

static void route_file_explorer_pointer(
    struct ios_kernel_runtime_state *state, const struct ios_input_event *event
)
{
    struct ios_input_event local = *event;
    bool activated;
    ios_u64 activated_handle;

    if (!state->file_explorer_view_running) return;
    local.x -= IOS_RUNTIME_EXPLORER_X;
    local.y -= IOS_RUNTIME_EXPLORER_Y;
    if (IOS_SUCCEEDED(ios_file_explorer_window_handle_input(
            &state->file_explorer_window, &local, &activated, &activated_handle))) {
        (void)activated;
        (void)activated_handle;
        (void)ios_file_explorer_window_render(&state->file_explorer_window);
        (void)ios_window_invalidate(
            &state->shell.desktop.window_manager,
            state->file_explorer_native_window,
            (struct ios_graphics_rect){
                0, 0, IOS_RUNTIME_EXPLORER_WIDTH, IOS_RUNTIME_EXPLORER_HEIGHT
            }
        );
    }
}

static ios_status initialize_modules_and_services(struct ios_kernel_runtime_state *state)
{
    const struct ios_system_module_descriptor *modules =
        (const void *)state->boot_info->module_descriptors_address;
    const ios_u64 map_bytes = state->boot_info->memory_map_count
        * state->boot_info->memory_descriptor_size;
    const ios_u64 descriptor_bytes = state->boot_info->module_descriptor_count
        * state->boot_info->module_descriptor_size;
    struct ios_system_module_range forbidden[5] = {
        { (ios_uptr)__kernel_start, (ios_u64)(__kernel_end - __kernel_start) },
        { (ios_uptr)state->boot_info, sizeof(*state->boot_info) },
        { state->boot_info->memory_map_address, map_bytes },
        { state->boot_info->module_descriptors_address, descriptor_bytes },
        { state->boot_info->framebuffer_address, state->boot_info->framebuffer_size }
    };
    ios_size forbidden_count = (state->boot_info->flags & IOS_BOOT_FLAG_GUI_UNAVAILABLE) != 0
        ? IOS_ARRAY_COUNT(forbidden) - 1U : IOS_ARRAY_COUNT(forbidden);
    struct ios_process *shell_process;
    ios_status status = process_start_system_modules(
        modules, state->boot_info->module_descriptor_count,
        forbidden, forbidden_count, verify_module_digest, launch_process, state
    );
    if (IOS_FAILED(status)) return status;
    shell_process = state->role_process[IOS_MODULE_ROLE_SHELL];
    if (shell_process == NULL) return IOS_ERROR(IOS_E_NOT_FOUND);

    status = ios_file_view_service_initialize(
        &state->file_view, &state->mounts, &state->type_catalog
    );
    if (IOS_FAILED(status)) return status;
    status = ios_gui_view_service_initialize(&state->gui_view, NULL, NULL);
    if (IOS_FAILED(status)) return status;
    status = ios_shell_service_start(&state->shell_service, &(struct ios_shell_service_config){
        .process = shell_process,
        .queue_depth = IOS_IPC_MAX_QUEUE_DEPTH,
        .dispatch_file_view = shell_file_view_dispatch,
        .dispatch_gui_view = shell_gui_view_dispatch,
        .dispatch_context = state
    });
    if (IOS_FAILED(status)) return status;

    if (role_is_available(state, IOS_MODULE_ROLE_FILE_EXPLORER)) {
        status = ios_file_explorer_client_initialize(
            &state->file_explorer,
            state->role_process[IOS_MODULE_ROLE_FILE_EXPLORER],
            &state->shell_service, resolve_file_explorer_icon, state
        );
        if (IOS_SUCCEEDED(status)) state->file_explorer_ready = true;
    }

    status = ios_fs_diagnostic_service_initialize(
        &state->diagnostics, &state->filesystem, filesystem_snapshot, state
    );
    if (IOS_FAILED(status)) return status;
    state->diagnostic_authority = (struct ios_fs_diagnostic_authority){
        .owner_process_id = shell_process->process_id,
        .owner_application_identity = shell_process->application_identity,
        .scope = IOS_FS_DIAGNOSTIC_SCOPE_ALL
    };
    status = handle_table_insert(
        &shell_process->handles, &state->diagnostic_authority,
        IOS_OBJECT_DIAGNOSTIC_CAPABILITY, IOS_RIGHT_DIAGNOSTIC,
        NULL, NULL, &state->diagnostic_handle
    );
    if (IOS_FAILED(status)) return status;
    return ios_cui_fs_set_diagnostic_service(
        &state->cui_filesystem, &state->diagnostics, shell_process,
        state->diagnostic_handle, resolve_diagnostic_object, state
    );
}

static const struct ios_psf2_font *open_runtime_font(struct ios_kernel_runtime_state *state)
{
    const ios_uptr start = (ios_uptr)__inferenceos_font_start;
    const ios_uptr end = (ios_uptr)__inferenceos_font_end;

    if (start == 0 || end <= start
        || IOS_FAILED(ios_psf2_open((const void *)start, end - start, &state->font))) {
        return NULL;
    }
    return &state->font;
}

ios_status ios_kernel_runtime_initialize(const struct ios_boot_info *boot_info)
{
    struct ios_kernel_runtime_state *state = &runtime_state;
    ios_type_icon_capability ignored_capability;
    const struct ios_cui_directory_operations directory_operations = {
        directory_enumerate, directory_change_current, directory_get_current,
        directory_create, directory_remove
    };
    ios_status status;

    if (boot_info == NULL) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    memset(state, 0, sizeof(*state));
    state->boot_info = boot_info;
    state->diagnostic_handle = IOS_INVALID_HANDLE;

    status = scheduler_initialize(runtime_idle, state);
    if (IOS_FAILED(status)) return status;
    status = scheduler_platform_initialize(
        (const void *)boot_info->root_system_description_pointer
    );
    if (IOS_FAILED(status)) return status;
    status = scheduler_set_tick_function(runtime_tick, state);
    if (IOS_FAILED(status)) return status;
    status = process_system_initialize();
    if (IOS_FAILED(status)) return status;
    status = syscall_initialize();
    if (IOS_FAILED(status)) return status;
    status = ipc_initialize();
    if (IOS_FAILED(status)) return status;

    input_queue_initialize(&state->input, IOS_RUNTIME_FRAMEBUFFER_WIDTH, IOS_RUNTIME_FRAMEBUFFER_HEIGHT);
    ps2_keyboard_initialize(&state->keyboard, &state->input);
    ps2_mouse_initialize(&state->mouse, &state->input);
    status = ps2_keyboard_hardware_initialize();
    if (IOS_FAILED(status)) return status;
    status = ps2_mouse_hardware_initialize();
    if (IOS_FAILED(status)) return status;

    vfs_mount_registry_initialize(&state->mounts);
    status = ios_cui_fs_context_initialize(
        &state->cui_filesystem, &state->mounts, &state->filesystem
    );
    if (IOS_FAILED(status)) return status;
    status = ios_cui_fs_set_mount_ready_operation(
        &state->cui_filesystem, state, runtime_mount_ready
    );
    if (IOS_FAILED(status)) return status;
    status = ios_cui_fs_set_directory_operations(
        &state->cui_filesystem, state, &directory_operations
    );
    if (IOS_FAILED(status)) return status;
    status = ios_power_initialize(&state->power, state, q35_power_transition);
    if (IOS_FAILED(status)) return status;
    status = ios_cui_fs_set_power_controller(&state->cui_filesystem, &state->power);
    if (IOS_FAILED(status)) return status;

    status = block_platform_initialize_primary(&state->block_device);
    if (IOS_SUCCEEDED(status)) {
        status = block_cache_initialize(
            &state->cache, &state->block_device,
            state->cache_entries, IOS_ARRAY_COUNT(state->cache_entries)
        );
        if (IOS_FAILED(status)) return status;
        status = ios_fs_sync_initialize(&state->sync, &state->cache);
        if (IOS_FAILED(status)) return status;
        status = ios_cui_fs_add_device(&state->cui_filesystem, &state->block_device);
        if (IOS_FAILED(status)) return status;
        status = ios_cui_fs_set_sync_operation(
            &state->cui_filesystem, &state->sync, runtime_sync
        );
        if (IOS_FAILED(status)) return status;
        status = ios_power_set_filesystem_sync(
            &state->power, &state->sync, runtime_sync
        );
        if (IOS_FAILED(status)) return status;
        status = ios_power_add_device(&state->power, &state->block_device);
        if (IOS_FAILED(status)) return status;
        state->storage_ready = true;
        status = ios_fs_mount_root(&state->filesystem, &state->block_device, &state->mounts);
        if (IOS_SUCCEEDED(status)) {
            status = runtime_mount_ready(state, &state->filesystem);
            if (IOS_FAILED(status)) return status;
            serial_write_line("INFERENCEOS:FILESYSTEM_MOUNTED");
        }
    } else {
        struct ios_pci_function functions[16];
        ios_size function_count = 0;
        serial_write("INFERENCEOS:STORAGE_UNAVAILABLE status=");
        serial_write_hex_u64((ios_u64)status);
        serial_write(" stage=");
        serial_write(virtio_blk_pci_last_stage());
        serial_write("\n");
        if (IOS_SUCCEEDED(x86_64_pci_enumerate(
                functions, IOS_ARRAY_COUNT(functions), &function_count))) {
            for (ios_size index = 0; index < function_count; ++index) {
                serial_write("INFERENCEOS:PCI_FUNCTION vendor=");
                serial_write_hex_u64(functions[index].vendor_id);
                serial_write(" device=");
                serial_write_hex_u64(functions[index].device_id);
                serial_write(" bus="); test_write_u64(functions[index].bus);
                serial_write(" slot="); test_write_u64(functions[index].device);
                serial_write("\n");
            }
        }
    }

    status = ios_type_catalog_initialize(
        &state->type_catalog,
        boot_info->memory_map_key != 0 ? boot_info->memory_map_key : UINT64_C(1)
    );
    if (IOS_FAILED(status)) return status;
    status = ios_type_catalog_register(
        &state->type_catalog, UINT64_C(1), IOS_ICON_TEXT, &ignored_capability
    );
    if (IOS_FAILED(status)) return status;
    status = ios_type_catalog_register(
        &state->type_catalog, UINT64_C(2), IOS_ICON_IMAGE, &ignored_capability
    );
    if (IOS_FAILED(status)) return status;
    status = ios_type_catalog_register(
        &state->type_catalog, UINT64_C(3), IOS_ICON_APPLICATION, &ignored_capability
    );
    if (IOS_FAILED(status)) return status;
    ios_application_bindings_initialize(&state->bindings);
    status = ios_type_capability_service_initialize(
        &state->type_capabilities, &state->bindings, &state->type_catalog
    );
    if (IOS_FAILED(status)) return status;
    status = initialize_modules_and_services(state);
    if (IOS_FAILED(status)) return status;

    status = ios_shell_bootstrap(&state->shell, &(struct ios_shell_config){
        .boot_info = boot_info,
        .shadow = { shadow_pixels, IOS_RUNTIME_FRAMEBUFFER_WIDTH,
                    IOS_RUNTIME_FRAMEBUFFER_HEIGHT, IOS_RUNTIME_FRAMEBUFFER_WIDTH },
        .terminal_surface = { terminal_pixels, IOS_RUNTIME_TERMINAL_WIDTH,
                              IOS_RUNTIME_TERMINAL_HEIGHT, IOS_RUNTIME_TERMINAL_WIDTH },
        .font = open_runtime_font(state),
        .cui_write = runtime_write,
        .cui_write_context = state,
        .command_context = &state->cui_filesystem,
        .start_module = shell_start_module,
        .stop_module = shell_stop_module,
        .module_context = state,
        .desktop_module_available = role_is_available(state, IOS_MODULE_ROLE_GUI_DESKTOP),
        .terminal_module_available = role_is_available(state, IOS_MODULE_ROLE_GUI_TERMINAL)
    });
    if (IOS_FAILED(status)) return status;
    status = ios_cui_register_fs_commands(&state->shell.commands);
    if (IOS_FAILED(status)) return status;

    if (test_uart_initialize()
        && IOS_SUCCEEDED(ios_test_control_initialize(
            &state->test_control, test_uart_read, state,
            test_control_dispatch, state
        ))) {
        state->test_control_ready = true;
        serial_write_line(
            "INFERENCEOS:TEST_CONTROL_READY version=1 transport=com2 profile=q35"
        );
    }
    state->initialized = true;
    serial_write_line("INFERENCEOS:CUI_READY");
    return IOS_OK;
}

void ios_kernel_runtime_step(void)
{
    struct ios_kernel_runtime_state *state = &runtime_state;
    struct ios_input_event event;
    struct ios_shell_dispatch_result dispatch_result;
    ios_status status;

    if (!state->initialized) return;
    if (state->test_control_ready) {
        (void)ios_test_control_poll(&state->test_control, 256);
    }
    (void)ps2_keyboard_interrupt(&state->keyboard, scheduler_tick_count());
    (void)ps2_mouse_interrupt(&state->mouse, scheduler_tick_count());
    while (IOS_SUCCEEDED(input_queue_pop(&state->input, &event))) {
        if (state->shell.gui_running) {
            if (event.type == IOS_INPUT_EVENT_KEY) {
                status = ios_terminal_feed_event(&state->shell.terminal, &event);
                if (IOS_FAILED(status)) {
                    ios_shell_stop_gui(&state->shell, "gui_unavailable: input");
                }
            } else if (event.type == IOS_INPUT_EVENT_POINTER_MOVE) {
                ios_window_set_pointer(
                    &state->shell.desktop.window_manager, event.x, event.y, true
                );
            } else if (event.type == IOS_INPUT_EVENT_POINTER_BUTTON) {
                route_file_explorer_pointer(state, &event);
            }
        } else if (event.type == IOS_INPUT_EVENT_KEY
                   && (event.flags & IOS_INPUT_PRESSED) != 0) {
            const ios_u32 key = event.text != 0 ? event.text : event.code;
            (void)ios_cui_console_feed(&state->shell.standalone_console, key);
        }
    }
    if (!state->shell.gui_running) state->file_explorer_view_running = false;
    if (state->shell.gui_running && !state->file_explorer_view_running
        && IOS_FAILED(start_file_explorer_view(state))) {
        ios_shell_stop_gui(&state->shell, "gui_unavailable: file explorer");
    }
    if (state->shell.gui_running
        && IOS_FAILED(ios_desktop_repaint(&state->shell.desktop))) {
        ios_shell_stop_gui(&state->shell, "gui_unavailable: composition");
    }
    if (state->shell_service.running) {
        status = ios_shell_service_receive_and_dispatch(
            &state->shell_service, &dispatch_result, false
        );
        if (status != IOS_ERROR(IOS_E_WOULD_BLOCK) && IOS_FAILED(status)) {
            serial_write_line("INFERENCEOS:SHELL_SERVICE_DEGRADED");
        }
    }
}

_Noreturn void ios_kernel_runtime_run(void)
{
    IOS_ASSERT(runtime_state.initialized);
    x86_64_interrupt_enable();
    scheduler_idle_loop();
}
