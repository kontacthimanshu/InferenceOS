#include <inferenceos/syscall.h>

#include <inferenceos/scheduler.h>
#include <inferenceos/arch/gdt.h>
#include <inferenceos/arch/syscall.h>
#include <inferenceos/runtime.h>

static ios_syscall_handler handlers[IOS_SYSCALL_MAX_NUMBER + 1];
static bool dispatch_ready;

IOS_STATIC_ASSERT(sizeof(struct x86_64_user_context) == 144, "user context assembly size");
IOS_STATIC_ASSERT(sizeof(struct ios_syscall_frame) == 200, "syscall frame assembly size");
IOS_STATIC_ASSERT(offsetof(struct ios_syscall_frame, number) == 144,
                  "syscall frame number offset");
IOS_STATIC_ASSERT(offsetof(struct ios_syscall_frame, arguments) == 152,
                  "syscall frame argument offset");

extern void x86_64_syscall_entry(void);

void syscall_dispatch_initialize(void)
{
    memset(handlers, 0, sizeof(handlers));
    dispatch_ready = true;
}

ios_status syscall_initialize(void)
{
    ios_status status;
    syscall_dispatch_initialize();
    status = syscall_ipc_register();
    if (IOS_FAILED(status)) return status;
    return x86_64_syscall_configure((ios_uptr)x86_64_syscall_entry);
}

void syscall_set_kernel_stack(ios_uptr kernel_stack_pointer)
{
    x86_64_syscall_set_kernel_stack(kernel_stack_pointer);
}

ios_status syscall_activate_process(struct ios_process *process)
{
    return process_activate(process);
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
            | IOS_SYSCALL_FEATURE_SAFE_USER_COPY | IOS_SYSCALL_FEATURE_PROCESS_HANDLES
            | IOS_SYSCALL_FEATURE_IPC,
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

const struct x86_64_user_context *syscall_dispatch_frame(struct ios_syscall_frame *frame)
{
    struct ios_scheduler_task *task = scheduler_current_task();
    struct ios_process *origin;
    ios_status status;

    if (frame == NULL || task == NULL || task->kind != IOS_TASK_USER_PROCESS
        || task->process == NULL) {
        virtual_kernel_address_space_activate();
        return NULL;
    }
    origin = task->process;
    origin->user_context = frame->context;
    if (!virtual_user_range_is_valid(origin->user_context.instruction_pointer, 1)
        || !virtual_user_range_is_valid(origin->user_context.stack_pointer, 1)) {
        (void)scheduler_exit_current(IOS_ERROR(IOS_E_BAD_ADDRESS));
    } else {
        status = syscall_dispatch(origin, frame->number, frame->arguments);
        origin->user_context.rax = (ios_u64)status;
    }

    task = scheduler_current_task();
    if (task != NULL && task->kind == IOS_TASK_USER_PROCESS && task->process != NULL
        && IOS_SUCCEEDED(process_activate(task->process))) {
        return &task->process->user_context;
    }
    virtual_kernel_address_space_activate();
    return NULL;
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
