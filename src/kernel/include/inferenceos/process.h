#ifndef INFERENCEOS_PROCESS_H
#define INFERENCEOS_PROCESS_H

#include <inferenceos/memory.h>
#include <inferenceos/handle_table.h>
#include <inferenceos/system_module.h>
#include <inferenceos/arch/context.h>

enum {
    IOS_PROCESS_MAX_COUNT = 32,
    IOS_PROCESS_USER_STACK_PAGES = 16,
    IOS_PROCESS_KERNEL_STACK_PAGES = 8
};

enum ios_process_state {
    IOS_PROCESS_UNUSED = 0,
    IOS_PROCESS_CREATED = 1,
    IOS_PROCESS_RUNNABLE = 2,
    IOS_PROCESS_EXITED = 3
};

struct ios_process {
    ios_u64 process_id;
    ios_u64 application_identity;
    ios_u32 module_role;
    enum ios_process_state state;
    struct ios_address_space address_space;
    ios_uptr entry_point;
    ios_uptr user_stack_pointer;
    struct x86_64_user_context user_context;
    ios_uptr kernel_stack_address;
    ios_u64 kernel_stack_pages;
    ios_i64 exit_status;
    struct ios_handle_table handles;
};

typedef ios_status (*ios_process_launch_callback)(struct ios_process *process, void *context);

ios_status process_system_initialize(void);
ios_status process_create_from_module(
    const struct ios_system_module_descriptor *descriptor,
    struct ios_process **created_process
);
void process_destroy(struct ios_process *process);
void process_mark_exited(struct ios_process *process, ios_i64 exit_status);
ios_status process_activate(struct ios_process *process);
ios_status process_collect(struct ios_process *process, ios_i64 *exit_status);
ios_status process_start_system_modules(
    const struct ios_system_module_descriptor *descriptors,
    ios_size descriptor_count,
    const struct ios_system_module_range *forbidden_ranges,
    ios_size forbidden_range_count,
    ios_system_module_digest_verifier verify_digest,
    ios_process_launch_callback launch,
    void *launch_context
);
ios_size process_count(void);
const struct ios_process *process_at(ios_size index);

ios_status process_load_static_elf64(
    struct ios_process *process,
    const void *image,
    ios_size image_size
);

#endif
