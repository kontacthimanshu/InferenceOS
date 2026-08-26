#ifndef INFERENCEOS_SCHEDULER_H
#define INFERENCEOS_SCHEDULER_H

#include <inferenceos/process.h>
#include <inferenceos/arch/platform.h>

enum {
    IOS_SCHEDULER_MAX_TASKS = 64,
    IOS_SCHEDULER_USER_PRIORITY = 0,
    IOS_SCHEDULER_KERNEL_PRIORITY_LOW = 1,
    IOS_SCHEDULER_KERNEL_PRIORITY_STORAGE = 2,
    IOS_SCHEDULER_KERNEL_PRIORITY_INPUT = 3,
    IOS_SCHEDULER_KERNEL_PRIORITY_RECOVERY = 4,
    IOS_SCHEDULER_QUANTUM_MS = 10
};

enum ios_scheduler_task_kind {
    IOS_TASK_USER_PROCESS = 1,
    IOS_TASK_KERNEL_WORK = 2,
    IOS_TASK_IDLE = 3
};

enum ios_scheduler_task_state {
    IOS_TASK_UNUSED = 0,
    IOS_TASK_RUNNABLE = 1,
    IOS_TASK_RUNNING = 2,
    IOS_TASK_BLOCKED = 3,
    IOS_TASK_EXITED = 4
};

struct ios_scheduler_task;
struct x86_64_interrupt_frame;

struct ios_wait_queue {
    struct ios_scheduler_task *head;
    struct ios_scheduler_task *tail;
};

typedef void (*ios_kernel_work_function)(void *context);
typedef void (*ios_idle_function)(void *context);
typedef void (*ios_scheduler_tick_function)(void *context);

struct ios_scheduler_task {
    ios_u64 task_id;
    enum ios_scheduler_task_kind kind;
    enum ios_scheduler_task_state state;
    ios_u8 priority;
    ios_u8 reserved[7];
    struct ios_process *process;
    ios_kernel_work_function work;
    void *work_context;
    struct ios_wait_queue *wait_queue;
    struct ios_scheduler_task *wait_next;
    ios_i64 exit_status;
};

ios_status scheduler_initialize(ios_idle_function idle, void *idle_context);
ios_status scheduler_set_tick_function(
    ios_scheduler_tick_function tick, void *tick_context
);
ios_status scheduler_platform_initialize(const void *root_system_description_pointer);
ios_status scheduler_add_process(struct ios_process *process);
ios_status scheduler_add_kernel_work(
    ios_u8 priority,
    ios_kernel_work_function work,
    void *context,
    struct ios_scheduler_task **task
);
struct ios_scheduler_task *scheduler_select_next(void);
struct ios_scheduler_task *scheduler_current_task(void);
void scheduler_on_quantum(void);
void scheduler_on_timer_interrupt(struct x86_64_interrupt_frame *frame);
ios_u64 scheduler_tick_count(void);
ios_status scheduler_block_current(struct ios_wait_queue *queue);
ios_status scheduler_wake_task(struct ios_scheduler_task *task);
ios_status scheduler_exit_current(ios_i64 exit_status);
void scheduler_finish_switch(void);
_Noreturn void scheduler_idle_loop(void);
void scheduler_run_current(void);

void wait_queue_initialize(struct ios_wait_queue *queue);
struct ios_scheduler_task *wait_queue_wake_one(struct ios_wait_queue *queue);
ios_size wait_queue_wake_all(struct ios_wait_queue *queue);
void wait_queue_remove(struct ios_wait_queue *queue, struct ios_scheduler_task *task);

#endif
