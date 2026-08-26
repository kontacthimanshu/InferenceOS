#include <inferenceos/user/runtime.h>

IOS_USED const char ios_user_application_name[] = "InferenceOS Proprietary Test";
volatile ios_u64 ios_proprietary_test_shell_endpoint;

ios_i64 ios_user_main(ios_u64 application_identity)
{
    struct ios_user_runtime_state runtime;
    const ios_status status = ios_user_runtime_initialize(
        &runtime, application_identity, true
    );
    if (IOS_FAILED(status)) return status;
    ios_proprietary_test_shell_endpoint = runtime.shell.endpoint_handle;
    ios_user_runtime_idle();
}
