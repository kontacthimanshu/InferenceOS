#include <inferenceos/cui.h>

#include <inferenceos/runtime.h>

static bool forbidden_syntax(char character)
{
    return character == '|' || character == '<' || character == '>'
        || character == '$' || character == '*' || character == '?'
        || character == ';' || character == '`';
}

enum ios_cui_parse_error ios_cui_parse_line(
    const char *input, struct ios_cui_parsed_line *parsed
)
{
    ios_size input_length;
    ios_size source = 0;
    ios_size destination = 0;
    if (input == NULL || parsed == NULL) return IOS_CUI_PARSE_INVALID_SYNTAX;
    input_length = strlen(input);
    if (input_length > IOS_CUI_MAX_PAYLOAD) return IOS_CUI_PARSE_LINE_TOO_LONG;
    memset(parsed, 0, sizeof(*parsed));
    while (source < input_length) {
        bool quoted = false;
        while (source < input_length && input[source] == ' ') ++source;
        if (source == input_length) break;
        if (parsed->argument_count == IOS_CUI_MAX_ARGUMENTS) {
            return IOS_CUI_PARSE_TOO_MANY_ARGUMENTS;
        }
        parsed->arguments[parsed->argument_count++] = &parsed->storage[destination];
        while (source < input_length) {
            const unsigned char character = (unsigned char)input[source];
            if (character < 0x20 || character > 0x7e) {
                return IOS_CUI_PARSE_INVALID_CHARACTER;
            }
            if (!quoted && character == ' ') break;
            if (character == '"') {
                quoted = !quoted;
                ++source;
                continue;
            }
            if (forbidden_syntax((char)character)) return IOS_CUI_PARSE_INVALID_SYNTAX;
            if (character == '\\') {
                if (!quoted || source + 1 >= input_length
                    || (input[source + 1] != '"' && input[source + 1] != '\\')) {
                    return IOS_CUI_PARSE_INVALID_SYNTAX;
                }
                ++source;
                parsed->storage[destination++] = input[source++];
                continue;
            }
            parsed->storage[destination++] = (char)character;
            ++source;
        }
        if (quoted) return IOS_CUI_PARSE_INVALID_SYNTAX;
        parsed->storage[destination++] = '\0';
    }
    return IOS_CUI_PARSE_OK;
}

const char *ios_cui_parse_error_symbol(enum ios_cui_parse_error error)
{
    switch (error) {
    case IOS_CUI_PARSE_LINE_TOO_LONG: return "line_too_long";
    case IOS_CUI_PARSE_INVALID_CHARACTER: return "invalid_character";
    case IOS_CUI_PARSE_INVALID_SYNTAX: return "invalid_syntax";
    case IOS_CUI_PARSE_TOO_MANY_ARGUMENTS: return "too_many_arguments";
    default: return "ok";
    }
}
