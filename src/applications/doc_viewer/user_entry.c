#include <inferenceos/user/runtime.h>

IOS_USED const char ios_user_application_name[] = "InferenceOS DOC Viewer";
volatile ios_u64 ios_doc_viewer_shell_endpoint;

ios_i64 ios_user_main(ios_u64 application_identity)
{
    struct ios_user_runtime_state runtime;
    const ios_status status = ios_user_runtime_initialize(
        &runtime, application_identity, true
    );
    if (IOS_FAILED(status)) return status;
    ios_doc_viewer_shell_endpoint = runtime.shell.endpoint_handle;
    ios_user_runtime_idle();
}
