#include <inferenceos/scheduler.h>

#include <inferenceos/arch/context.h>
#include <inferenceos/arch/gdt.h>
#include <inferenceos/arch/interrupts.h>
#include <inferenceos/runtime.h>

static struct ios_scheduler_task tasks[IOS_SCHEDULER_MAX_TASKS];
static struct ios_scheduler_task idle_task;
static ios_idle_function idle_callback;
static void *idle_callback_context;
static ios_scheduler_tick_function tick_callback;
static void *tick_callback_context;
static struct ios_scheduler_task *current_task;
static struct ios_scheduler_task *deferred_exit_task;
static ios_size last_selected[IOS_SCHEDULER_KERNEL_PRIORITY_RECOVERY + 1];
static ios_u64 ticks;
static ios_u64 next_task_id;
static bool scheduler_ready;

enum {
    X86_64_USER_FLAGS_REQUIRED = UINT64_C(0x202),
    X86_64_USER_FLAGS_FORBIDDEN = (UINT64_C(3) << 12) | (UINT64_C(1) << 14)
        | (UINT64_C(1) << 17)
};

static struct ios_scheduler_task *allocate_task(void)
{
    for (ios_size index = 0; index < IOS_SCHEDULER_MAX_TASKS; ++index) {
        if (tasks[index].state == IOS_TASK_UNUSED) {
            memset(&tasks[index], 0, sizeof(tasks[index]));
            tasks[index].task_id = next_task_id++;
            return &tasks[index];
        }
    }
    return NULL;
}

static struct ios_scheduler_task *select_runnable(void)
{
    for (ios_i32 priority = IOS_SCHEDULER_KERNEL_PRIORITY_RECOVERY;
         priority >= IOS_SCHEDULER_USER_PRIORITY;
         --priority) {
        const ios_size start = last_selected[priority];
        for (ios_size offset = 1; offset <= IOS_SCHEDULER_MAX_TASKS; ++offset) {
            const ios_size index = (start + offset) % IOS_SCHEDULER_MAX_TASKS;
            if (tasks[index].state == IOS_TASK_RUNNABLE
                && tasks[index].priority == (ios_u8)priority) {
                last_selected[priority] = index;
                return &tasks[index];
            }
        }
    }
    return &idle_task;
}

static struct ios_scheduler_task *select_runnable_user(void)
{
    const ios_size start = last_selected[IOS_SCHEDULER_USER_PRIORITY];

    for (ios_size offset = 1; offset <= IOS_SCHEDULER_MAX_TASKS; ++offset) {
        const ios_size index = (start + offset) % IOS_SCHEDULER_MAX_TASKS;
        if (tasks[index].state == IOS_TASK_RUNNABLE
            && tasks[index].kind == IOS_TASK_USER_PROCESS) {
            last_selected[IOS_SCHEDULER_USER_PRIORITY] = index;
            return &tasks[index];
        }
    }
    return NULL;
}

static ios_u64 user_flags_sanitize(ios_u64 flags)
{
    return (flags & ~X86_64_USER_FLAGS_FORBIDDEN) | X86_64_USER_FLAGS_REQUIRED;
}

static void save_user_interrupt_context(
    struct x86_64_user_context *context,
    const struct x86_64_interrupt_frame *frame
)
{
    context->r15 = frame->r15;
    context->r14 = frame->r14;
    context->r13 = frame->r13;
    context->r12 = frame->r12;
    context->r11 = frame->r11;
    context->r10 = frame->r10;
    context->r9 = frame->r9;
    context->r8 = frame->r8;
    context->rdi = frame->rdi;
    context->rsi = frame->rsi;
    context->rbp = frame->rbp;
    context->rdx = frame->rdx;
    context->rcx = frame->rcx;
    context->rbx = frame->rbx;
    context->rax = frame->rax;
    context->instruction_pointer = frame->rip;
    context->flags = user_flags_sanitize(frame->rflags);
    context->stack_pointer = x86_64_interrupt_user_stack(frame);
}

static void load_user_interrupt_context(
    struct x86_64_interrupt_frame *frame,
    const struct x86_64_user_context *context
)
{
    ios_u64 *privilege_frame = (ios_u64 *)(frame + 1);

    IOS_ASSERT(virtual_user_range_is_valid(context->instruction_pointer, 1));
    IOS_ASSERT(virtual_user_range_is_valid(context->stack_pointer, 1));
    frame->r15 = context->r15;
    frame->r14 = context->r14;
    frame->r13 = context->r13;
    frame->r12 = context->r12;
    frame->r11 = context->r11;
    frame->r10 = context->r10;
    frame->r9 = context->r9;
    frame->r8 = context->r8;
    frame->rdi = context->rdi;
    frame->rsi = context->rsi;
    frame->rbp = context->rbp;
    frame->rdx = context->rdx;
    frame->rcx = context->rcx;
    frame->rbx = context->rbx;
    frame->rax = context->rax;
    frame->rip = context->instruction_pointer;
    frame->cs = X86_64_USER_CODE_SELECTOR;
    frame->rflags = user_flags_sanitize(context->flags);
    privilege_frame[0] = context->stack_pointer;
    privilege_frame[1] = X86_64_USER_DATA_SELECTOR;
}

static void timer_interrupt(struct x86_64_interrupt_frame *frame)
{
    x86_64_apic_timer_acknowledge();
    scheduler_on_timer_interrupt(frame);
}

ios_status scheduler_initialize(ios_idle_function idle, void *idle_context)
{
    if (idle == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    memset(tasks, 0, sizeof(tasks));
    memset(last_selected, 0, sizeof(last_selected));
    memset(&idle_task, 0, sizeof(idle_task));
    idle_task.task_id = 0;
    idle_task.kind = IOS_TASK_IDLE;
    idle_task.state = IOS_TASK_RUNNABLE;
    idle_task.priority = IOS_SCHEDULER_USER_PRIORITY;
    idle_callback = idle;
    idle_callback_context = idle_context;
    tick_callback = NULL;
    tick_callback_context = NULL;
    current_task = NULL;
    deferred_exit_task = NULL;
    ticks = 0;
    next_task_id = 1;
    scheduler_ready = true;
    return IOS_OK;
}

ios_status scheduler_set_tick_function(
    ios_scheduler_tick_function tick, void *tick_context
)
{
    if (!scheduler_ready || tick == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    tick_callback = tick;
    tick_callback_context = tick_context;
    return IOS_OK;
}

ios_status scheduler_platform_initialize(const void *root_system_description_pointer)
{
    struct x86_64_platform_info platform;
    ios_status status;

    if (!scheduler_ready) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    status = x86_64_acpi_discover(root_system_description_pointer, &platform);
    if (IOS_FAILED(status)) {
        return status;
    }
    x86_64_pic_mask_and_remap();
    status = x86_64_apic_timer_initialize(&platform);
    if (IOS_FAILED(status)) {
        return status;
    }
    x86_64_interrupt_set_handler(X86_64_APIC_TIMER_VECTOR, timer_interrupt);
    return IOS_OK;
}

ios_status scheduler_add_process(struct ios_process *process)
{
    struct ios_scheduler_task *task;
    ios_u64 flags;

    if (!scheduler_ready || process == NULL || process->state != IOS_PROCESS_RUNNABLE) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    flags = x86_64_interrupt_save_disable();
    task = allocate_task();
    if (task == NULL) {
        x86_64_interrupt_restore(flags);
        return IOS_ERROR(IOS_E_NO_SPACE);
    }
    task->kind = IOS_TASK_USER_PROCESS;
    task->state = IOS_TASK_RUNNABLE;
    task->priority = IOS_SCHEDULER_USER_PRIORITY;
    task->process = process;
    x86_64_interrupt_restore(flags);
    return IOS_OK;
}

ios_status scheduler_add_kernel_work(
    ios_u8 priority,
    ios_kernel_work_function work,
    void *context,
    struct ios_scheduler_task **task
)
{
    struct ios_scheduler_task *created;
    ios_u64 flags;

    if (!scheduler_ready || work == NULL || task == NULL
        || priority < IOS_SCHEDULER_KERNEL_PRIORITY_LOW
        || priority > IOS_SCHEDULER_KERNEL_PRIORITY_RECOVERY) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    flags = x86_64_interrupt_save_disable();
    created = allocate_task();
    if (created != NULL) {
        created->kind = IOS_TASK_KERNEL_WORK;
        created->state = IOS_TASK_BLOCKED;
        created->priority = priority;
        created->work = work;
        created->work_context = context;
        *task = created;
    }
    x86_64_interrupt_restore(flags);
    return created == NULL ? IOS_ERROR(IOS_E_NO_SPACE) : IOS_OK;
}

struct ios_scheduler_task *scheduler_select_next(void)
{
    ios_u64 flags;
    struct ios_scheduler_task *selected;

    IOS_ASSERT(scheduler_ready);
    flags = x86_64_interrupt_save_disable();
    if (current_task != NULL && current_task->state == IOS_TASK_RUNNING) {
        current_task->state = IOS_TASK_RUNNABLE;
    }
    selected = select_runnable();
    selected->state = IOS_TASK_RUNNING;
    current_task = selected;
    x86_64_interrupt_restore(flags);
    return selected;
}

struct ios_scheduler_task *scheduler_current_task(void)
{
    return current_task;
}

void scheduler_on_quantum(void)
{
    IOS_ASSERT(scheduler_ready);
    ++ticks;
    (void)scheduler_select_next();
}

void scheduler_on_timer_interrupt(struct x86_64_interrupt_frame *frame)
{
    struct ios_scheduler_task *selected;
    ios_status status;

    IOS_ASSERT(scheduler_ready);
    IOS_ASSERT(frame != NULL);
    ++ticks;
    if (tick_callback != NULL) tick_callback(tick_callback_context);
    if (!x86_64_interrupt_from_user(frame) || current_task == NULL
        || current_task->kind != IOS_TASK_USER_PROCESS
        || current_task->state != IOS_TASK_RUNNING || current_task->process == NULL) {
        return;
    }

    save_user_interrupt_context(&current_task->process->user_context, frame);
    current_task->state = IOS_TASK_RUNNABLE;
    selected = select_runnable_user();
    IOS_ASSERT(selected != NULL);
    selected->state = IOS_TASK_RUNNING;
    current_task = selected;
    status = process_activate(selected->process);
    IOS_ASSERT(IOS_SUCCEEDED(status));
    load_user_interrupt_context(frame, &selected->process->user_context);
}

ios_u64 scheduler_tick_count(void)
{
    return ticks;
}

ios_status scheduler_block_current(struct ios_wait_queue *queue)
{
    ios_u64 flags;
    struct ios_scheduler_task *task;

    if (!scheduler_ready || queue == NULL || current_task == NULL
        || current_task == &idle_task || current_task->state != IOS_TASK_RUNNING
        || current_task->wait_queue != NULL) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    flags = x86_64_interrupt_save_disable();
    task = current_task;
    task->state = IOS_TASK_BLOCKED;
    task->wait_queue = queue;
    task->wait_next = NULL;
    if (queue->tail == NULL) {
        queue->head = task;
    } else {
        queue->tail->wait_next = task;
    }
    queue->tail = task;
    current_task = select_runnable();
    current_task->state = IOS_TASK_RUNNING;
    x86_64_interrupt_restore(flags);
    return IOS_OK;
}

ios_status scheduler_wake_task(struct ios_scheduler_task *task)
{
    ios_u64 flags;
    if (!scheduler_ready || task == NULL || task->state != IOS_TASK_BLOCKED) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    flags = x86_64_interrupt_save_disable();
    if (task->wait_queue != NULL) {
        wait_queue_remove(task->wait_queue, task);
    }
    task->state = IOS_TASK_RUNNABLE;
    x86_64_interrupt_restore(flags);
    return IOS_OK;
}

ios_status scheduler_exit_current(ios_i64 exit_status)
{
    struct ios_scheduler_task *task;
    ios_u64 flags;

    if (!scheduler_ready || current_task == NULL || current_task == &idle_task
        || current_task->kind != IOS_TASK_USER_PROCESS || deferred_exit_task != NULL) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    flags = x86_64_interrupt_save_disable();
    task = current_task;
    if (task->wait_queue != NULL) {
        wait_queue_remove(task->wait_queue, task);
    }
    task->exit_status = exit_status;
    task->state = IOS_TASK_EXITED;
    task->process->exit_status = exit_status;
    deferred_exit_task = task;
    current_task = select_runnable();
    current_task->state = IOS_TASK_RUNNING;
    x86_64_interrupt_restore(flags);
    return IOS_OK;
}

void scheduler_finish_switch(void)
{
    struct ios_scheduler_task *exited;
    ios_u64 flags;

    if (!scheduler_ready) {
        return;
    }
    flags = x86_64_interrupt_save_disable();
    exited = deferred_exit_task;
    deferred_exit_task = NULL;
    if (exited != NULL) {
        process_mark_exited(exited->process, exited->exit_status);
    }
    x86_64_interrupt_restore(flags);
}

void scheduler_run_current(void)
{
    IOS_ASSERT(scheduler_ready);
    if (current_task == NULL) {
        (void)scheduler_select_next();
    }
    if (current_task == &idle_task) {
        idle_callback(idle_callback_context);
        return;
    }
    if (current_task->kind == IOS_TASK_USER_PROCESS) {
        const ios_status status = process_activate(current_task->process);
        IOS_ASSERT(IOS_SUCCEEDED(status));
        x86_64_restore_user_context(&current_task->process->user_context);
    }
    if (current_task->kind == IOS_TASK_KERNEL_WORK) {
        struct ios_scheduler_task *work_task = current_task;
        work_task->work(work_task->work_context);
        if (work_task->state == IOS_TASK_RUNNING) {
            work_task->state = IOS_TASK_BLOCKED;
        }
        current_task = NULL;
    }
}

_Noreturn void scheduler_idle_loop(void)
{
    virtual_kernel_address_space_activate();
    for (;;) {
        scheduler_run_current();
        (void)scheduler_select_next();
    }
}
