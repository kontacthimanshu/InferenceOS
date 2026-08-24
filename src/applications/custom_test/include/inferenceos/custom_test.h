#ifndef INFERENCEOS_CUSTOM_TEST_H
#define INFERENCEOS_CUSTOM_TEST_H

#include <inferenceos/proprietary_adapter.h>
#include <inferenceos/shell_protocol.h>

struct ios_custom_test_application {
    struct ios_process *process;
    struct ios_shell_service *shell;
    struct ios_proprietary_adapter_service *adapters;
    ios_handle shell_channel;
    ios_handle adapter_handle;
    ios_handle content_handle;
    ios_handle directory_handle;
    ios_u64 next_request_id;
    ios_status raw_extension_probe_status;
    ios_status raw_hash_probe_status;
    ios_status arbitrary_type_probe_status;
    struct ios_proprietary_adapter_reply adapter_reply;
    bool connected;
};

ios_status ios_custom_test_initialize(
    struct ios_custom_test_application *application,
    struct ios_process *process,
    struct ios_shell_service *shell,
    struct ios_proprietary_adapter_service *adapters,
    ios_u64 adapter_identity,
    ios_handle content_handle,
    ios_handle directory_handle
);
ios_status ios_custom_test_run(
    struct ios_custom_test_application *application,
    ios_u32 adapter_operation,
    const void *input,
    ios_size input_size
);
void ios_custom_test_disconnect(struct ios_custom_test_application *application);

#endif
