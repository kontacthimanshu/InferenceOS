#include <inferenceos/user/runtime.h>

#include <inferenceos/runtime.h>

ios_status ios_user_runtime_initialize(
    struct ios_user_runtime_state *state,
    ios_u64 application_identity,
    bool connect_to_shell
)
{
    const ios_u64 required_features = IOS_SYSCALL_FEATURE_VERSIONED_STRUCTURES
        | IOS_SYSCALL_FEATURE_SAFE_USER_COPY | IOS_SYSCALL_FEATURE_PROCESS_HANDLES
        | IOS_SYSCALL_FEATURE_IPC;
    ios_status status;
    if (state == NULL || application_identity == 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    memset(state, 0, sizeof(*state));
    state->application_identity = application_identity;
    status = ios_user_abi_query(&state->abi);
    if (IOS_FAILED(status)) return status;
    if (state->abi.size != sizeof(state->abi)
        || state->abi.version != IOS_SYSCALL_ABI_MAJOR
        || state->abi.major != IOS_SYSCALL_ABI_MAJOR
        || state->abi.reserved != 0
        || (state->abi.feature_bits & required_features) != required_features) {
        return IOS_ERROR(IOS_E_UNSUPPORTED_VERSION);
    }
    return connect_to_shell
        ? ios_user_ipc_service_connect(IOS_SERVICE_SHELL, &state->shell)
        : IOS_OK;
}

_Noreturn void ios_user_runtime_idle(void)
{
    for (;;) { }
}
