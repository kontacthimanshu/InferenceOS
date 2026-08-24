#include <inferenceos/test.h>

#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static jmp_buf failure_target;
static const char *failure_expression;
static const char *failure_file;
static unsigned long failure_line;

_Noreturn void ios_test_fail(
    const char *expression,
    const char *file,
    unsigned long line
)
{
    failure_expression = expression;
    failure_file = file;
    failure_line = line;
    longjmp(failure_target, 1);
}

static bool name_matches(const char *name, const char *filter)
{
    return filter == NULL || strstr(name, filter) != NULL;
}

int main(int argument_count, char **arguments)
{
    const char *filter = NULL;
    size_t selected = 0;
    size_t passed = 0;
    bool list_only = false;

    for (int index = 1; index < argument_count; ++index) {
        if (strcmp(arguments[index], "--list") == 0) {
            list_only = true;
        } else if (strcmp(arguments[index], "--filter") == 0 && index + 1 < argument_count) {
            filter = arguments[++index];
        } else {
            fprintf(stderr, "usage: %s [--list] [--filter substring]\n", *arguments);
            return 2;
        }
    }
    for (size_t index = 0; index < ios_test_case_count; ++index) {
        const struct ios_test_case *test = &ios_test_cases[index];
        if (!name_matches(test->name, filter)) {
            continue;
        }
        ++selected;
        if (list_only) {
            puts(test->name);
            continue;
        }
        failure_expression = NULL;
        if (setjmp(failure_target) == 0) {
            test->function();
            ++passed;
            printf("PASS %s\n", test->name);
        } else {
            printf("FAIL %s: %s:%lu: %s\n", test->name, failure_file,
                   failure_line, failure_expression);
        }
    }
    if (list_only) {
        return selected == 0 ? 1 : 0;
    }
    printf("RESULT %zu/%zu passed\n", passed, selected);
    return selected != 0 && passed == selected ? 0 : 1;
}
