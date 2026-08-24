#include <inferenceos/syscall.h>

#include <inferenceos/scheduler.h>
#include <inferenceos/arch/gdt.h>
#include <inferenceos/arch/syscall.h>
#include <inferenceos/runtime.h>

static ios_syscall_handler handlers[IOS_SYSCALL_MAX_NUMBER + 1];
static bool dispatch_ready;

IOS_STATIC_ASSERT(sizeof(struct ios_syscall_frame) == 80, "syscall frame assembly size");
IOS_STATIC_ASSERT(offsetof(struct ios_syscall_frame, user_instruction_pointer) == 56,
                  "syscall frame instruction-pointer offset");
IOS_STATIC_ASSERT(offsetof(struct ios_syscall_frame, user_stack_pointer) == 72,
                  "syscall frame stack-pointer offset");

extern void x86_64_syscall_entry(void);
extern ios_uptr x86_64_syscall_kernel_stack;

void syscall_dispatch_initialize(void)
{
    memset(handlers, 0, sizeof(handlers));
    dispatch_ready = true;
}

ios_status syscall_initialize(void)
{
    syscall_dispatch_initialize();
    return x86_64_syscall_configure((ios_uptr)x86_64_syscall_entry);
}

void syscall_set_kernel_stack(ios_uptr kernel_stack_pointer)
{
    IOS_ASSERT(kernel_stack_pointer != 0);
    IOS_ASSERT((kernel_stack_pointer & UINT64_C(0xf)) == 0);
    x86_64_syscall_kernel_stack = kernel_stack_pointer;
}

ios_status syscall_activate_process(struct ios_process *process)
{
    ios_uptr kernel_stack_top;
    if (process == NULL || process->state != IOS_PROCESS_RUNNABLE
        || process->address_space.root_address == 0 || process->kernel_stack_address == 0
        || process->kernel_stack_pages == 0
        || process->kernel_stack_pages > UINT64_MAX / IOS_PAGE_SIZE) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    kernel_stack_top = process->kernel_stack_address
        + process->kernel_stack_pages * IOS_PAGE_SIZE;
    kernel_stack_top &= ~UINT64_C(0xf);
    virtual_address_space_activate(&process->address_space);
    x86_64_gdt_set_kernel_stack(kernel_stack_top);
    syscall_set_kernel_stack(kernel_stack_top);
    return IOS_OK;
}

ios_status syscall_register(ios_u64 number, ios_syscall_handler handler)
{
    if (!dispatch_ready || handler == NULL || number <= IOS_SYSCALL_PROCESS_EXIT
        || number > IOS_SYSCALL_MAX_NUMBER) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (handlers[number] != NULL) {
        return IOS_ERROR(IOS_E_ALREADY_EXISTS);
    }
    handlers[number] = handler;
    return IOS_OK;
}

static ios_status abi_info(
    struct ios_process *process,
    const ios_u64 arguments[6]
)
{
    const struct ios_syscall_abi_info information = {
        .size = sizeof(information),
        .version = IOS_SYSCALL_ABI_MAJOR,
        .major = IOS_SYSCALL_ABI_MAJOR,
        .minor = IOS_SYSCALL_ABI_MINOR,
        .feature_bits = IOS_SYSCALL_FEATURE_VERSIONED_STRUCTURES
            | IOS_SYSCALL_FEATURE_SAFE_USER_COPY | IOS_SYSCALL_FEATURE_PROCESS_HANDLES,
        .reserved = 0
    };
    return user_copy_to(process, (ios_uptr)*arguments, &information, sizeof(information));
}

ios_status syscall_dispatch(
    struct ios_process *process,
    ios_u64 number,
    const ios_u64 arguments[6]
)
{
    if (!dispatch_ready || process == NULL || arguments == NULL
        || process->state == IOS_PROCESS_UNUSED || process->state == IOS_PROCESS_EXITED) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    if (number == IOS_SYSCALL_ABI_INFO) {
        return abi_info(process, arguments);
    }
    if (number == IOS_SYSCALL_PROCESS_EXIT) {
        return scheduler_exit_current((ios_i64)*arguments);
    }
    if (number > IOS_SYSCALL_MAX_NUMBER || handlers[number] == NULL) {
        return IOS_ERROR(IOS_E_UNKNOWN_SYSCALL);
    }
    return handlers[number](process, arguments);
}

ios_status syscall_dispatch_frame(struct ios_syscall_frame *frame)
{
    struct ios_scheduler_task *task = scheduler_current_task();
    if (frame == NULL || task == NULL || task->kind != IOS_TASK_USER_PROCESS
        || task->process == NULL
        || !virtual_user_range_is_valid(frame->user_instruction_pointer, 1)
        || !virtual_user_range_is_valid(frame->user_stack_pointer, 1)) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    return syscall_dispatch(task->process, frame->number, frame->arguments);
}

ios_status syscall_validate_structure_header(
    const struct ios_process *process,
    ios_uptr user_address,
    ios_u16 minimum_size,
    ios_u16 expected_version,
    struct ios_syscall_structure_header *header
)
{
    ios_status status;
    if (header == NULL || minimum_size < sizeof(*header)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    status = user_copy_from(process, header, user_address, sizeof(*header));
    if (IOS_FAILED(status)) {
        return status;
    }
    if (header->version != expected_version) {
        return IOS_ERROR(IOS_E_UNSUPPORTED_VERSION);
    }
    if (header->size < minimum_size) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    return IOS_OK;
}
