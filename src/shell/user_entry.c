#include <inferenceos/user/runtime.h>

IOS_USED const char ios_user_application_name[] = "InferenceOS Shell";
volatile ios_u64 ios_shell_application_identity;

ios_i64 ios_user_main(ios_u64 application_identity)
{
    struct ios_user_runtime_state runtime;
    const ios_status status = ios_user_runtime_initialize(
        &runtime, application_identity, false
    );
    if (IOS_FAILED(status)) return status;
    ios_shell_application_identity = runtime.application_identity;
    ios_user_runtime_idle();
}
