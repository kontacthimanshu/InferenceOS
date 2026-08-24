#ifndef INFERENCEOS_SYSCALL_H
#define INFERENCEOS_SYSCALL_H

#include <inferenceos/process.h>

enum {
    IOS_SYSCALL_ABI_MAJOR = 1,
    IOS_SYSCALL_ABI_MINOR = 0,
    IOS_SYSCALL_MAX_NUMBER = 127,
    IOS_SYSCALL_ABI_INFO = 0,
    IOS_SYSCALL_PROCESS_EXIT = 1
};

enum ios_syscall_feature {
    IOS_SYSCALL_FEATURE_VERSIONED_STRUCTURES = UINT64_C(1) << 0,
    IOS_SYSCALL_FEATURE_SAFE_USER_COPY = UINT64_C(1) << 1,
    IOS_SYSCALL_FEATURE_PROCESS_HANDLES = UINT64_C(1) << 2
};

struct ios_syscall_structure_header {
    ios_u16 size;
    ios_u16 version;
};

struct ios_syscall_abi_info {
    ios_u16 size;
    ios_u16 version;
    ios_u16 major;
    ios_u16 minor;
    ios_u64 feature_bits;
    ios_u64 reserved;
};

struct ios_syscall_frame {
    ios_u64 number;
    ios_u64 arguments[6];
    ios_uptr user_instruction_pointer;
    ios_u64 user_flags;
    ios_uptr user_stack_pointer;
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
ios_status syscall_dispatch(
    struct ios_process *process,
    ios_u64 number,
    const ios_u64 arguments[6]
);
ios_status syscall_dispatch_frame(struct ios_syscall_frame *frame);
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
