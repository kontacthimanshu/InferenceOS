#include <inferenceos/test.h>

#include <inferenceos/cui.h>

#include <string.h>

struct output_buffer { char bytes[2048]; ios_size length; };

static void capture_output(const char *value, void *context)
{
    struct output_buffer *output = context;
    const ios_size length = strlen(value);
    IOS_TEST_ASSERT(output->length + length < sizeof(output->bytes));
    memcpy(output->bytes + output->length, value, length + 1);
    output->length += length;
}

static ios_status record_command(
    ios_size count, const char *const *arguments, struct ios_cui_io *io
)
{
    ios_size *observation = io->command_context;
    IOS_TEST_ASSERT(strcmp(*arguments, "write") == 0);
    IOS_TEST_ASSERT(strcmp(arguments[1], "/DOCS/REPORT.TXT") == 0);
    IOS_TEST_ASSERT(strcmp(arguments[2], "persistent data") == 0);
    *observation = count;
    return IOS_OK;
}

static const struct ios_cui_command write_command = {
    "write", "record a test write", record_command
};

static void test_payload_boundary_and_deterministic_grammar(void)
{
    char maximum[IOS_CUI_MAX_PAYLOAD + 1];
    char excessive[IOS_CUI_MAX_PAYLOAD + 2];
    struct ios_cui_parsed_line parsed;
    memset(maximum, 'a', IOS_CUI_MAX_PAYLOAD);
    maximum[IOS_CUI_MAX_PAYLOAD] = '\0';
    memset(excessive, 'a', IOS_CUI_MAX_PAYLOAD + 1);
    excessive[IOS_CUI_MAX_PAYLOAD + 1] = '\0';
    IOS_TEST_ASSERT(ios_cui_parse_line(maximum, &parsed) == IOS_CUI_PARSE_OK);
    IOS_TEST_ASSERT(strlen(*parsed.arguments) == IOS_CUI_MAX_PAYLOAD);
    IOS_TEST_ASSERT(ios_cui_parse_line(excessive, &parsed) == IOS_CUI_PARSE_LINE_TOO_LONG);
    IOS_TEST_ASSERT(ios_cui_parse_line(
        "  write /DOCS/REPORT.TXT \"a \\\"quoted\\\" \\\\ value\"  ", &parsed
    ) == IOS_CUI_PARSE_OK);
    IOS_TEST_ASSERT(parsed.argument_count == 3);
    IOS_TEST_ASSERT(strcmp(*parsed.arguments, "write") == 0);
    IOS_TEST_ASSERT(strcmp(parsed.arguments[2], "a \"quoted\" \\ value") == 0);
    IOS_TEST_ASSERT(ios_cui_parse_line("create \"\"", &parsed) == IOS_CUI_PARSE_OK);
    IOS_TEST_ASSERT(parsed.argument_count == 2 && strcmp(parsed.arguments[1], "") == 0);
}

static void test_unsupported_syntax_and_nonprintable_input_are_rejected(void)
{
    static const char *unsupported[] = {
        "help | version", "type < file", "help > output", "echo $name",
        "dir *.TXT", "help; version", "echo `value`"
    };
    struct ios_cui_parsed_line parsed;
    for (ios_size index = 0; index < IOS_ARRAY_COUNT(unsupported); ++index) {
        IOS_TEST_ASSERT(ios_cui_parse_line(
            unsupported[index], &parsed
        ) == IOS_CUI_PARSE_INVALID_SYNTAX);
    }
    IOS_TEST_ASSERT(ios_cui_parse_line(
        "write \"unterminated", &parsed
    ) == IOS_CUI_PARSE_INVALID_SYNTAX);
    IOS_TEST_ASSERT(ios_cui_parse_line(
        "write \"bad\\n\"", &parsed
    ) == IOS_CUI_PARSE_INVALID_SYNTAX);
    IOS_TEST_ASSERT(ios_cui_parse_line(
        "help\tversion", &parsed
    ) == IOS_CUI_PARSE_INVALID_CHARACTER);
}

static void test_shared_registry_dispatches_identically(void)
{
    struct ios_cui_command_registry registry;
    struct ios_cui_parsed_line standalone;
    struct ios_cui_parsed_line terminal;
    struct output_buffer output = { 0 };
    ios_size standalone_count = 0;
    ios_size terminal_count = 0;
    struct ios_cui_io io = { capture_output, &output, &standalone_count, NULL };
    ios_cui_command_registry_initialize(&registry);
    IOS_TEST_ASSERT_STATUS(ios_cui_command_register(&registry, &write_command), IOS_OK);
    IOS_TEST_ASSERT_STATUS(
        ios_cui_command_register(&registry, &write_command), IOS_ERROR(IOS_E_ALREADY_EXISTS)
    );
    IOS_TEST_ASSERT(ios_cui_parse_line(
        "write /DOCS/REPORT.TXT \"persistent data\"", &standalone
    ) == IOS_CUI_PARSE_OK);
    IOS_TEST_ASSERT(ios_cui_parse_line(
        "write /DOCS/REPORT.TXT \"persistent data\"", &terminal
    ) == IOS_CUI_PARSE_OK);
    IOS_TEST_ASSERT_STATUS(ios_cui_command_dispatch(&registry, &standalone, &io), IOS_OK);
    io.command_context = &terminal_count;
    IOS_TEST_ASSERT_STATUS(ios_cui_command_dispatch(&registry, &terminal, &io), IOS_OK);
    IOS_TEST_ASSERT(standalone_count == 3 && terminal_count == 3);
}

static void test_console_edits_reports_errors_and_recovers_prompt(void)
{
    struct ios_cui_command_registry registry;
    struct ios_cui_console console;
    struct output_buffer output = { 0 };
    struct ios_cui_io io = { capture_output, &output, NULL, NULL };
    ios_cui_command_registry_initialize(&registry);
    IOS_TEST_ASSERT_STATUS(ios_cui_register_core_commands(&registry), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_cui_console_initialize(&console, &registry, io), IOS_OK);
    ios_cui_console_prompt(&console);
    for (const char *input = "helx"; *input != '\0'; ++input) {
        IOS_TEST_ASSERT_STATUS(ios_cui_console_feed(&console, (ios_u8)*input), IOS_OK);
    }
    IOS_TEST_ASSERT_STATUS(ios_cui_console_feed(&console, '\b'), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_cui_console_feed(&console, 'p'), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_cui_console_feed(&console, '\r'), IOS_OK);
    IOS_TEST_ASSERT(strstr(output.bytes, "help - list available commands\n") != NULL);
    IOS_TEST_ASSERT_STATUS(ios_cui_console_feed(&console, 'x'), IOS_OK);
    IOS_TEST_ASSERT_STATUS(ios_cui_console_feed(&console, '\n'), IOS_ERROR(IOS_E_NOT_FOUND));
    IOS_TEST_ASSERT(strstr(
        output.bytes, "error: command_not_found: unknown command\n"
    ) != NULL);
    IOS_TEST_ASSERT(console.line_length == 0 && *console.line == '\0');
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_payload_boundary_and_deterministic_grammar),
    IOS_TEST_CASE(test_unsupported_syntax_and_nonprintable_input_are_rejected),
    IOS_TEST_CASE(test_shared_registry_dispatches_identically),
    IOS_TEST_CASE(test_console_edits_reports_errors_and_recovers_prompt)
};

const size_t ios_test_case_count = IOS_ARRAY_COUNT(ios_test_cases);
