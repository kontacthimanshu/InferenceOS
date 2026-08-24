#ifndef INFERENCEOS_PROPRIETARY_TEST_H
#define INFERENCEOS_PROPRIETARY_TEST_H

#include <inferenceos/shell_protocol.h>

enum { IOS_PROPRIETARY_TEST_ENTRY_CAPACITY = 16 };

struct ios_proprietary_test_application {
    struct ios_process *process;
    struct ios_shell_service *shell;
    ios_handle shell_channel;
    ios_handle type_capability;
    ios_handle directory_handle;
    ios_u64 next_request_id;
    struct ios_display_safe_entry entries[IOS_PROPRIETARY_TEST_ENTRY_CAPACITY];
    ios_size entry_count;
    bool connected;
};

ios_status ios_proprietary_test_initialize(
    struct ios_proprietary_test_application *application,
    struct ios_process *process,
    struct ios_shell_service *shell,
    ios_handle directory_handle,
    ios_handle type_capability
);
ios_status ios_proprietary_test_run(struct ios_proprietary_test_application *application);
void ios_proprietary_test_disconnect(struct ios_proprietary_test_application *application);

#endif
