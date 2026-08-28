#ifndef INFERENCEOS_TEST_CONTROL_H
#define INFERENCEOS_TEST_CONTROL_H

#include <inferenceos/errors.h>

enum {
    IOS_TEST_CONTROL_PROTOCOL_VERSION = 1,
    IOS_TEST_CONTROL_LINE_CAPACITY = 255,
    IOS_TEST_CONTROL_ACTION_CAPACITY = 48,
    IOS_TEST_CONTROL_ARGUMENT_CAPACITY = 160
};

struct ios_test_control_request {
    ios_u32 sequence;
    char action[IOS_TEST_CONTROL_ACTION_CAPACITY];
    char argument[IOS_TEST_CONTROL_ARGUMENT_CAPACITY];
};

typedef bool (*ios_test_control_read_character)(void *context, char *character);
typedef ios_status (*ios_test_control_dispatch)(
    void *context, const struct ios_test_control_request *request
);

struct ios_test_control {
    ios_test_control_read_character read_character;
    void *read_context;
    ios_test_control_dispatch dispatch;
    void *dispatch_context;
    char line[IOS_TEST_CONTROL_LINE_CAPACITY + 1];
    ios_size line_length;
    ios_u32 last_sequence;
    bool discarding_line;
};

ios_status ios_test_control_initialize(
    struct ios_test_control *control,
    ios_test_control_read_character read_character,
    void *read_context,
    ios_test_control_dispatch dispatch,
    void *dispatch_context
);

/* Polls at most character_budget bytes. Malformed lines are discarded independently. */
ios_status ios_test_control_poll(
    struct ios_test_control *control, ios_size character_budget
);

#endif
