#ifndef INFERENCEOS_SYSCALL_H
#define INFERENCEOS_SYSCALL_H

#include <inferenceos/arch/context.h>
#include <inferenceos/process.h>
#include <inferenceos/user/abi.h>

struct ios_syscall_frame {
    struct x86_64_user_context context;
    ios_u64 number;
    ios_u64 arguments[6];
};

typedef ios_status (*ios_syscall_handler)(
    struct ios_process *process,
    const ios_u64 arguments[6]
);

ios_status syscall_initialize(void);
void syscall_dispatch_initialize(void);
void syscall_set_kernel_stack(ios_uptr kernel_stack_pointer);
ios_status syscall_activate_process(struct ios_process *process);
ios_status syscall_register(ios_u64 number, ios_syscall_handler handler);
ios_status syscall_ipc_register(void);
ios_status syscall_dispatch(
    struct ios_process *process,
    ios_u64 number,
    const ios_u64 arguments[6]
);
const struct x86_64_user_context *syscall_dispatch_frame(struct ios_syscall_frame *frame);
ios_status syscall_validate_structure_header(
    const struct ios_process *process,
    ios_uptr user_address,
    ios_u16 minimum_size,
    ios_u16 expected_version,
    struct ios_syscall_structure_header *header
);

ios_status user_copy_from(
    const struct ios_process *process,
    void *kernel_destination,
    ios_uptr user_source,
    ios_size byte_count
);
ios_status user_copy_to(
    const struct ios_process *process,
    ios_uptr user_destination,
    const void *kernel_source,
    ios_size byte_count
);
ios_status user_clear(
    const struct ios_process *process,
    ios_uptr user_destination,
    ios_size byte_count
);

#endif
