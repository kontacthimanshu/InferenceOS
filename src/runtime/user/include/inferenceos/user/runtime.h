#ifndef INFERENCEOS_USER_RUNTIME_H
#define INFERENCEOS_USER_RUNTIME_H

#include <inferenceos/user/abi.h>

struct ios_user_runtime_state {
    ios_u64 application_identity;
    struct ios_syscall_abi_info abi;
    struct ios_user_ipc_connection shell;
};

ios_status ios_user_syscall6(
    ios_u64 number,
    ios_u64 argument0,
    ios_u64 argument1,
    ios_u64 argument2,
    ios_u64 argument3,
    ios_u64 argument4,
    ios_u64 argument5
);
ios_status ios_user_abi_query(struct ios_syscall_abi_info *information);
_Noreturn void ios_user_process_exit(ios_i64 exit_status);
ios_status ios_user_ipc_service_connect(
    enum ios_trusted_service service,
    struct ios_user_ipc_connection *connection
);
ios_status ios_user_ipc_send(
    ios_u64 endpoint_handle,
    ios_u64 request_id,
    ios_u32 operation,
    ios_u16 item_count,
    const void *payload,
    ios_u16 payload_size
);
ios_status ios_user_ipc_receive(
    ios_u64 endpoint_handle,
    struct ios_ipc_message *message
);
ios_status ios_user_runtime_initialize(
    struct ios_user_runtime_state *state,
    ios_u64 application_identity,
    bool connect_to_shell
);
_Noreturn void ios_user_runtime_idle(void);

/* Every separately linked application supplies this CRT entry contract. */
ios_i64 ios_user_main(ios_u64 application_identity);

#endif
