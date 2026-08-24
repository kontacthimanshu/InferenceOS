#include <inferenceos/test.h>

#include <inferenceos/arch/interrupts.h>
#include <inferenceos/arch/platform.h>
#include <inferenceos/process.h>
#include <inferenceos/scheduler.h>

#include <string.h>

static ios_u32 release_count;
static ios_u32 freed_stack_count;
static ios_u32 destroyed_address_space_count;
static ios_uptr freed_stack_address;
static ios_u64 freed_stack_pages;

_Noreturn void ios_assertion_failed(const char *condition, const char *file, ios_u32 line)
{
    ios_test_fail(condition, file, line);
}

ios_u64 x86_64_interrupt_save_disable(void)
{
    return 0;
}

void x86_64_interrupt_restore(ios_u64 previous_flags)
{
    (void)previous_flags;
}

void x86_64_interrupt_set_handler(ios_u8 vector, x86_64_interrupt_handler handler)
{
    (void)vector;
    (void)handler;
}

ios_status x86_64_acpi_discover(
    const void *root_system_description_pointer,
    struct x86_64_platform_info *platform
)
{
    (void)root_system_description_pointer;
    (void)platform;
    return IOS_ERROR(IOS_E_NOT_SUPPORTED);
}

void x86_64_pic_mask_and_remap(void)
{
}

ios_status x86_64_apic_timer_initialize(const struct x86_64_platform_info *platform)
{
    (void)platform;
    return IOS_ERROR(IOS_E_NOT_SUPPORTED);
}

void x86_64_apic_timer_acknowledge(void)
{
}

bool physical_memory_is_initialized(void)
{
    return true;
}

ios_status physical_allocate_pages(
    ios_u64 page_count,
    ios_u64 alignment_pages,
    ios_uptr *physical_address
)
{
    (void)page_count;
    (void)alignment_pages;
    (void)physical_address;
    return IOS_ERROR(IOS_E_NO_MEMORY);
}

ios_status physical_free_pages(ios_uptr physical_address, ios_u64 page_count)
{
    ++freed_stack_count;
    freed_stack_address = physical_address;
    freed_stack_pages = page_count;
    return IOS_OK;
}

ios_status virtual_address_space_create(struct ios_address_space *address_space)
{
    (void)address_space;
    return IOS_ERROR(IOS_E_NO_MEMORY);
}

void virtual_address_space_destroy(struct ios_address_space *address_space)
{
    ++destroyed_address_space_count;
    address_space->root_address = 0;
}

ios_status virtual_map_pages(
    struct ios_address_space *address_space,
    ios_uptr virtual_address,
    ios_uptr physical_address,
    ios_u64 page_count,
    ios_u32 flags
)
{
    (void)address_space;
    (void)virtual_address;
    (void)physical_address;
    (void)page_count;
    (void)flags;
    return IOS_ERROR(IOS_E_NO_MEMORY);
}

ios_status process_load_static_elf64(
    struct ios_process *process,
    const void *image,
    ios_size image_size
)
{
    (void)process;
    (void)image;
    (void)image_size;
    return IOS_ERROR(IOS_E_NOT_SUPPORTED);
}

ios_status system_module_set_validate(
    const struct ios_system_module_descriptor *descriptors,
    ios_size descriptor_count,
    const struct ios_system_module_range *forbidden_ranges,
    ios_size forbidden_range_count,
    ios_system_module_digest_verifier verify_digest
)
{
    (void)descriptors;
    (void)descriptor_count;
    (void)forbidden_ranges;
    (void)forbidden_range_count;
    (void)verify_digest;
    return IOS_OK;
}

ios_status system_module_descriptor_validate(
    const struct ios_system_module_descriptor *descriptor,
    const struct ios_system_module_range *forbidden_ranges,
    ios_size forbidden_range_count,
    ios_system_module_digest_verifier verify_digest
)
{
    (void)descriptor;
    (void)forbidden_ranges;
    (void)forbidden_range_count;
    (void)verify_digest;
    return IOS_OK;
}

ios_u32 system_module_startup_rank(ios_u32 role)
{
    return role;
}

static void idle(void *context)
{
    ios_u32 *calls = context;
    ++*calls;
}

static void release_object(void *object)
{
    IOS_TEST_ASSERT(object != NULL);
    ++release_count;
}

static void initialize_process(struct ios_process *process)
{
    static ios_u32 object;
    ios_handle handle;

    memset(process, 0, sizeof(*process));
    process->process_id = 42;
    process->state = IOS_PROCESS_RUNNABLE;
    process->kernel_stack_address = (ios_uptr)UINT64_C(0x200000);
    process->kernel_stack_pages = IOS_PROCESS_KERNEL_STACK_PAGES;
    process->address_space.root_address = (ios_uptr)UINT64_C(0x300000);
    IOS_TEST_ASSERT_STATUS(handle_table_initialize(&process->handles, process->process_id), IOS_OK);
    IOS_TEST_ASSERT_STATUS(handle_table_insert(
        &process->handles,
        &object,
        IOS_OBJECT_FILE,
        IOS_RIGHT_READ,
        NULL,
        release_object,
        &handle
    ), IOS_OK);
    IOS_TEST_ASSERT(handle != IOS_INVALID_HANDLE);
}

static void reset_observations(void)
{
    release_count = 0;
    freed_stack_count = 0;
    destroyed_address_space_count = 0;
    freed_stack_address = 0;
    freed_stack_pages = 0;
}

static void test_exit_releases_resources_and_records_collectible_status(void)
{
    struct ios_process process;
    ios_i64 status = 0;
    ios_u32 idle_calls = 0;

    reset_observations();
    initialize_process(&process);
    IOS_TEST_ASSERT_STATUS(scheduler_initialize(idle, &idle_calls), IOS_OK);
    IOS_TEST_ASSERT_STATUS(scheduler_add_process(&process), IOS_OK);
    IOS_TEST_ASSERT(scheduler_select_next()->process == &process);
    IOS_TEST_ASSERT_STATUS(scheduler_exit_current(-37), IOS_OK);

    IOS_TEST_ASSERT(process.state == IOS_PROCESS_EXITED);
    IOS_TEST_ASSERT(process.exit_status == -37);
    IOS_TEST_ASSERT(handle_table_open_count(&process.handles) == 0);
    IOS_TEST_ASSERT(release_count == 1);
    IOS_TEST_ASSERT(freed_stack_count == 1);
    IOS_TEST_ASSERT(freed_stack_address == (ios_uptr)UINT64_C(0x200000));
    IOS_TEST_ASSERT(freed_stack_pages == IOS_PROCESS_KERNEL_STACK_PAGES);
    IOS_TEST_ASSERT(process.kernel_stack_address == 0);
    IOS_TEST_ASSERT(destroyed_address_space_count == 1);
    IOS_TEST_ASSERT(process.address_space.root_address == 0);
    IOS_TEST_ASSERT(scheduler_current_task()->kind == IOS_TASK_IDLE);

    IOS_TEST_ASSERT_STATUS(process_collect(&process, &status), IOS_OK);
    IOS_TEST_ASSERT(status == -37);
    IOS_TEST_ASSERT(process.state == IOS_PROCESS_UNUSED);
}

static void test_blocked_wait_is_cancelled_before_exit(void)
{
    struct ios_process process;
    struct ios_wait_queue queue;
    struct ios_scheduler_task *blocked;
    ios_u32 idle_calls = 0;

    reset_observations();
    initialize_process(&process);
    IOS_TEST_ASSERT_STATUS(scheduler_initialize(idle, &idle_calls), IOS_OK);
    IOS_TEST_ASSERT_STATUS(scheduler_add_process(&process), IOS_OK);
    blocked = scheduler_select_next();
    wait_queue_initialize(&queue);
    IOS_TEST_ASSERT_STATUS(scheduler_block_current(&queue), IOS_OK);
    IOS_TEST_ASSERT(queue.head == blocked && queue.tail == blocked);
    IOS_TEST_ASSERT_STATUS(scheduler_wake_task(blocked), IOS_OK);
    IOS_TEST_ASSERT(queue.head == NULL && queue.tail == NULL);
    IOS_TEST_ASSERT(scheduler_select_next() == blocked);
    IOS_TEST_ASSERT_STATUS(scheduler_exit_current(0), IOS_OK);
    IOS_TEST_ASSERT(blocked->wait_queue == NULL);
    IOS_TEST_ASSERT(queue.head == NULL && queue.tail == NULL);
}

static void test_unexited_process_cannot_be_collected(void)
{
    struct ios_process process = { .state = IOS_PROCESS_RUNNABLE };
    ios_i64 status;
    IOS_TEST_ASSERT_STATUS(
        process_collect(&process, &status),
        IOS_ERROR(IOS_E_INVALID_STATE)
    );
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_exit_releases_resources_and_records_collectible_status),
    IOS_TEST_CASE(test_blocked_wait_is_cancelled_before_exit),
    IOS_TEST_CASE(test_unexited_process_cannot_be_collected)
};

const size_t ios_test_case_count = sizeof(ios_test_cases) / sizeof(ios_test_cases[0]);
