#include <inferenceos/test.h>

#include <inferenceos/shell.h>

#include <string.h>

enum {
    SCREEN_WIDTH = 80,
    SCREEN_HEIGHT = 64,
    TERMINAL_WIDTH = 40,
    TERMINAL_HEIGHT = 32,
    FONT_GLYPH_COUNT = 128,
    FONT_DATA_SIZE = IOS_PSF2_HEADER_SIZE + FONT_GLYPH_COUNT * IOS_PSF2_FONT_HEIGHT
};

struct test_observation {
    char output[2048];
    ios_size output_length;
    ios_u32 starts[4];
    ios_size start_count;
    ios_u32 stops[4];
    ios_size stop_count;
    ios_u32 fail_role;
    ios_size command_calls;
};

static ios_status service_command(
    ios_size argument_count, const char *const *arguments, struct ios_cui_io *io
)
{
    struct test_observation *observation = io->command_context;

    (void)arguments;
    if (argument_count != 1 || observation == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    ++observation->command_calls;
    return IOS_OK;
}

static const struct ios_cui_command service_descriptor = {
    "service", "exercise the shared command context", "service", service_command
};

static void write_u32(ios_u8 *bytes, ios_u32 value)
{
    *bytes = (ios_u8)value;
    bytes[1] = (ios_u8)(value >> 8);
    bytes[2] = (ios_u8)(value >> 16);
    bytes[3] = (ios_u8)(value >> 24);
}

static void make_font(ios_u8 *data, struct ios_psf2_font *font)
{
    memset(data, 0xff, FONT_DATA_SIZE);
    write_u32(data, IOS_PSF2_MAGIC);
    write_u32(data + 4, 0);
    write_u32(data + 8, IOS_PSF2_HEADER_SIZE);
    write_u32(data + 12, 0);
    write_u32(data + 16, FONT_GLYPH_COUNT);
    write_u32(data + 20, IOS_PSF2_FONT_HEIGHT);
    write_u32(data + 24, IOS_PSF2_FONT_HEIGHT);
    write_u32(data + 28, IOS_PSF2_FONT_WIDTH);
    IOS_TEST_ASSERT_STATUS(ios_psf2_open(data, FONT_DATA_SIZE, font), IOS_OK);
}

static void capture_output(const char *text, void *context)
{
    struct test_observation *observation = context;
    const ios_size length = strlen(text);
    IOS_TEST_ASSERT(observation->output_length + length < sizeof(observation->output));
    memcpy(observation->output + observation->output_length, text, length + 1);
    observation->output_length += length;
}

static ios_status start_module(ios_u32 role, void *context)
{
    struct test_observation *observation = context;
    observation->starts[observation->start_count++] = role;
    return role == observation->fail_role ? IOS_ERROR(IOS_E_PROTOCOL) : IOS_OK;
}

static void stop_module(ios_u32 role, void *context)
{
    struct test_observation *observation = context;
    observation->stops[observation->stop_count++] = role;
}

static struct ios_shell_config make_config(
    struct ios_boot_info *boot_info,
    ios_u32 *framebuffer,
    ios_u32 *shadow,
    ios_u32 *terminal_pixels,
    const struct ios_psf2_font *font,
    struct test_observation *observation
)
{
    *boot_info = (struct ios_boot_info){
        .structure_size = sizeof(*boot_info),
        .version = IOS_BOOT_INFO_VERSION,
        .framebuffer_address = (ios_uptr)framebuffer,
        .framebuffer_size = SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(ios_u32),
        .framebuffer_width = SCREEN_WIDTH,
        .framebuffer_height = SCREEN_HEIGHT,
        .framebuffer_stride = SCREEN_WIDTH,
        .framebuffer_format = IOS_BOOT_FRAMEBUFFER_BGRX8888
    };
    return (struct ios_shell_config){
        .boot_info = boot_info,
        .shadow = { shadow, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_WIDTH },
        .terminal_surface = {
            terminal_pixels, TERMINAL_WIDTH, TERMINAL_HEIGHT, TERMINAL_WIDTH
        },
        .font = font,
        .cui_write = capture_output,
        .cui_write_context = observation,
        .command_context = observation,
        .start_module = start_module,
        .stop_module = stop_module,
        .module_context = observation,
        .desktop_module_available = true,
        .terminal_module_available = true,
        .launch_terminal = true
    };
}

static void test_gui_starts_without_a_terminal_window_when_disabled(void)
{
    ios_u32 framebuffer[SCREEN_WIDTH * SCREEN_HEIGHT];
    ios_u32 shadow[SCREEN_WIDTH * SCREEN_HEIGHT];
    ios_u32 terminal_pixels[TERMINAL_WIDTH * TERMINAL_HEIGHT];
    ios_u8 font_data[FONT_DATA_SIZE];
    struct ios_psf2_font font;
    struct ios_boot_info boot_info;
    struct test_observation observation = { 0 };
    struct ios_shell_runtime shell;
    struct ios_shell_config config;

    make_font(font_data, &font);
    config = make_config(
        &boot_info, framebuffer, shadow, terminal_pixels, &font, &observation
    );
    config.launch_terminal = false;
    config.terminal_module_available = false;
    IOS_TEST_ASSERT_STATUS(ios_shell_bootstrap(&shell, &config), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_shell_start_gui(&shell), IOS_OK);
    IOS_TEST_ASSERT(shell.gui_running && shell.desktop.active);
    IOS_TEST_ASSERT(!shell.terminal.active && !shell.terminal_module_started);
    IOS_TEST_ASSERT(observation.start_count == 1);
    IOS_TEST_ASSERT(*observation.starts == IOS_MODULE_ROLE_GUI_DESKTOP);

    ios_shell_stop_gui(&shell, NULL);
    IOS_TEST_ASSERT(!shell.gui_running && shell.cui_usable);
    IOS_TEST_ASSERT(observation.stop_count == 1);
    IOS_TEST_ASSERT(*observation.stops == IOS_MODULE_ROLE_GUI_DESKTOP);
}

static void test_gui_command_starts_modules_and_terminal_shares_registry(void)
{
    ios_u32 framebuffer[SCREEN_WIDTH * SCREEN_HEIGHT];
    ios_u32 shadow[SCREEN_WIDTH * SCREEN_HEIGHT];
    ios_u32 terminal_pixels[TERMINAL_WIDTH * TERMINAL_HEIGHT];
    ios_u8 font_data[FONT_DATA_SIZE];
    struct ios_psf2_font font;
    struct ios_boot_info boot_info;
    struct test_observation observation = { 0 };
    struct ios_shell_runtime shell;
    struct ios_shell_config config;
    struct ios_input_event close_event;
    bool close_requested;

    make_font(font_data, &font);
    config = make_config(
        &boot_info, framebuffer, shadow, terminal_pixels, &font, &observation
    );
    IOS_TEST_ASSERT_STATUS(ios_shell_bootstrap(&shell, &config), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_cui_command_register(&shell.commands, &service_descriptor), IOS_OK
    );
    IOS_TEST_ASSERT(shell.cui_usable && !shell.gui_running);
    for (const char *input = "service"; *input != '\0'; ++input) {
        IOS_TEST_ASSERT_STATUS(
            ios_cui_console_feed(&shell.standalone_console, (ios_u8)*input), IOS_OK
        );
    }
    IOS_TEST_ASSERT_STATUS(ios_cui_console_feed(&shell.standalone_console, '\n'), IOS_OK);
    IOS_TEST_ASSERT(observation.command_calls == 1);
    for (const char *input = "gui"; *input != '\0'; ++input) {
        IOS_TEST_ASSERT_STATUS(
            ios_cui_console_feed(&shell.standalone_console, (ios_u8)*input), IOS_OK
        );
    }
    IOS_TEST_ASSERT_STATUS(ios_cui_console_feed(&shell.standalone_console, '\n'), IOS_OK);
    IOS_TEST_ASSERT(shell.gui_running && shell.desktop.active && shell.terminal.active);
    IOS_TEST_ASSERT(observation.start_count == 2);
    IOS_TEST_ASSERT(*observation.starts == IOS_MODULE_ROLE_GUI_DESKTOP);
    IOS_TEST_ASSERT(observation.starts[1] == IOS_MODULE_ROLE_GUI_TERMINAL);
    IOS_TEST_ASSERT(shell.terminal.console.registry == &shell.commands);
    IOS_TEST_ASSERT(shell.standalone_console.registry == &shell.commands);
    IOS_TEST_ASSERT(strcmp(shell.diagnostic, "gui_ready") == 0);

    for (const char *input = "service"; *input != '\0'; ++input) {
        IOS_TEST_ASSERT_STATUS(
            ios_terminal_feed_event(&shell.terminal, &(struct ios_input_event){
                .structure_size = sizeof(struct ios_input_event),
                .structure_version = IOS_INPUT_EVENT_VERSION,
                .type = IOS_INPUT_EVENT_KEY,
                .flags = IOS_INPUT_PRESSED,
                .text = *input
            }),
            IOS_OK
        );
    }
    IOS_TEST_ASSERT_STATUS(
        ios_terminal_feed_event(&shell.terminal, &(struct ios_input_event){
            .structure_size = sizeof(struct ios_input_event),
            .structure_version = IOS_INPUT_EVENT_VERSION,
            .type = IOS_INPUT_EVENT_KEY,
            .flags = IOS_INPUT_PRESSED,
            .code = IOS_KEY_ENTER,
            .text = '\n'
        }),
        IOS_OK
    );
    IOS_TEST_ASSERT(observation.command_calls == 2);

    close_event = (struct ios_input_event){
        .structure_size = sizeof(close_event),
        .structure_version = IOS_INPUT_EVENT_VERSION,
        .type = IOS_INPUT_EVENT_POINTER_BUTTON,
        .flags = IOS_INPUT_PRESSED,
        .code = IOS_POINTER_BUTTON_LEFT,
        .x = shell.desktop.close_button_bounds.x + 1,
        .y = shell.desktop.close_button_bounds.y + 1
    };
    IOS_TEST_ASSERT_STATUS(
        ios_desktop_handle_input(&shell.desktop, &close_event, &close_requested), IOS_OK
    );
    IOS_TEST_ASSERT(close_requested);

    close_event = (struct ios_input_event){
        .structure_size = sizeof(close_event),
        .structure_version = IOS_INPUT_EVENT_VERSION,
        .type = IOS_INPUT_EVENT_KEY,
        .flags = IOS_INPUT_PRESSED,
        .code = IOS_KEY_ESCAPE
    };
    IOS_TEST_ASSERT_STATUS(
        ios_desktop_handle_input(&shell.desktop, &close_event, &close_requested), IOS_OK
    );
    IOS_TEST_ASSERT(close_requested);

    ios_shell_stop_gui(&shell, NULL);
    IOS_TEST_ASSERT(!shell.gui_running && shell.cui_usable);
    IOS_TEST_ASSERT(observation.stop_count == 2);
    IOS_TEST_ASSERT(*observation.stops == IOS_MODULE_ROLE_GUI_TERMINAL);
    IOS_TEST_ASSERT(observation.stops[1] == IOS_MODULE_ROLE_GUI_DESKTOP);
}

static void test_terminal_failure_unwinds_desktop_and_preserves_cui(void)
{
    ios_u32 framebuffer[SCREEN_WIDTH * SCREEN_HEIGHT];
    ios_u32 shadow[SCREEN_WIDTH * SCREEN_HEIGHT];
    ios_u32 terminal_pixels[TERMINAL_WIDTH * TERMINAL_HEIGHT];
    ios_u8 font_data[FONT_DATA_SIZE];
    struct ios_psf2_font font;
    struct ios_boot_info boot_info;
    struct test_observation observation = { .fail_role = IOS_MODULE_ROLE_GUI_TERMINAL };
    struct ios_shell_runtime shell;
    struct ios_shell_config config;

    make_font(font_data, &font);
    config = make_config(
        &boot_info, framebuffer, shadow, terminal_pixels, &font, &observation
    );
    IOS_TEST_ASSERT_STATUS(ios_shell_bootstrap(&shell, &config), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_shell_start_gui(&shell), IOS_ERROR(IOS_E_PROTOCOL));
    IOS_TEST_ASSERT(shell.cui_usable && !shell.gui_running && !shell.desktop.active);
    IOS_TEST_ASSERT(!shell.terminal.active);
    IOS_TEST_ASSERT(strcmp(shell.diagnostic, "gui_unavailable: terminal module") == 0);
    IOS_TEST_ASSERT(observation.stop_count == 1);
    IOS_TEST_ASSERT(*observation.stops == IOS_MODULE_ROLE_GUI_DESKTOP);

    IOS_TEST_ASSERT_STATUS(ios_cui_console_feed(&shell.standalone_console, 'x'), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_cui_console_feed(&shell.standalone_console, '\n'), IOS_ERROR(IOS_E_NOT_FOUND)
    );
    IOS_TEST_ASSERT(strstr(observation.output, "InferenceOS> ") != NULL);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_gui_starts_without_a_terminal_window_when_disabled),
    IOS_TEST_CASE(test_gui_command_starts_modules_and_terminal_shares_registry),
    IOS_TEST_CASE(test_terminal_failure_unwinds_desktop_and_preserves_cui)
};

const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
