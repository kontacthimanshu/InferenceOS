#include <inferenceos/process.h>

#include <inferenceos/arch/gdt.h>
#include <inferenceos/arch/syscall.h>
#include <inferenceos/runtime.h>

#define USER_STACK_TOP UINT64_C(0x00007fffffff0000)

static struct ios_process process_table[IOS_PROCESS_MAX_COUNT];
static ios_u64 next_process_id;
static bool process_system_ready;

static struct ios_process *allocate_process_slot(void)
{
    for (ios_size index = 0; index < IOS_PROCESS_MAX_COUNT; ++index) {
        if (process_table[index].state == IOS_PROCESS_UNUSED) {
            memset(&process_table[index], 0, sizeof(process_table[index]));
            process_table[index].state = IOS_PROCESS_CREATED;
            return &process_table[index];
        }
    }
    return NULL;
}

ios_status process_system_initialize(void)
{
    if (!physical_memory_is_initialized()) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    memset(process_table, 0, sizeof(process_table));
    next_process_id = 1;
    process_system_ready = true;
    return IOS_OK;
}

ios_status process_create_from_module(
    const struct ios_system_module_descriptor *descriptor,
    struct ios_process **created_process
)
{
    struct ios_process *process;
    ios_uptr user_stack_physical;
    ios_status status;

    if (!process_system_ready || descriptor == NULL || created_process == NULL
        || descriptor->application_identity == 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    for (ios_size index = 0; index < IOS_PROCESS_MAX_COUNT; ++index) {
        if (process_table[index].state != IOS_PROCESS_UNUSED
            && process_table[index].application_identity == descriptor->application_identity) {
            return IOS_ERROR(IOS_E_ALREADY_EXISTS);
        }
    }
    process = allocate_process_slot();
    if (process == NULL) {
        return IOS_ERROR(IOS_E_NO_SPACE);
    }
    process->process_id = next_process_id++;
    if (next_process_id == 0) {
        next_process_id = 1;
    }
    process->application_identity = descriptor->application_identity;
    process->module_role = descriptor->role;
    status = handle_table_initialize(&process->handles, process->process_id);
    if (IOS_FAILED(status)) {
        goto fail;
    }

    status = virtual_address_space_create(&process->address_space);
    if (IOS_FAILED(status)) {
        goto fail;
    }
    status = process_load_static_elf64(
        process, (const void *)descriptor->image_address, (ios_size)descriptor->image_size
    );
    if (IOS_FAILED(status)) {
        goto fail;
    }
    status = physical_allocate_pages(
        IOS_PROCESS_USER_STACK_PAGES, 1, &user_stack_physical
    );
    if (IOS_FAILED(status)) {
        goto fail;
    }
    memset(
        (void *)user_stack_physical,
        0,
        IOS_PROCESS_USER_STACK_PAGES * IOS_PAGE_SIZE
    );
    status = virtual_map_pages(
        &process->address_space,
        USER_STACK_TOP - IOS_PROCESS_USER_STACK_PAGES * IOS_PAGE_SIZE,
        user_stack_physical,
        IOS_PROCESS_USER_STACK_PAGES,
        IOS_VM_USER | IOS_VM_WRITE | IOS_VM_OWNED
    );
    if (IOS_FAILED(status)) {
        (void)physical_free_pages(user_stack_physical, IOS_PROCESS_USER_STACK_PAGES);
        goto fail;
    }
    process->user_stack_pointer = USER_STACK_TOP;
    status = physical_allocate_pages(
        IOS_PROCESS_KERNEL_STACK_PAGES, 1, &process->kernel_stack_address
    );
    if (IOS_FAILED(status)) {
        goto fail;
    }
    process->kernel_stack_pages = IOS_PROCESS_KERNEL_STACK_PAGES;
    memset(
        (void *)process->kernel_stack_address,
        0,
        IOS_PROCESS_KERNEL_STACK_PAGES * IOS_PAGE_SIZE
    );
    memset(&process->user_context, 0, sizeof(process->user_context));
    process->user_context.rdi = process->application_identity;
    process->user_context.instruction_pointer = process->entry_point;
    process->user_context.flags = UINT64_C(0x202);
    process->user_context.stack_pointer = process->user_stack_pointer;
    *created_process = process;
    return IOS_OK;

fail:
    process_destroy(process);
    return status;
}

ios_status process_activate(struct ios_process *process)
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
    x86_64_syscall_set_kernel_stack(kernel_stack_top);
    return IOS_OK;
}

void process_destroy(struct ios_process *process)
{
    if (process == NULL || process->state == IOS_PROCESS_UNUSED) {
        return;
    }
    if (process->kernel_stack_address != 0) {
        (void)physical_free_pages(process->kernel_stack_address, process->kernel_stack_pages);
    }
    handle_table_close_all(&process->handles);
    if (process->address_space.root_address != 0) {
        virtual_address_space_destroy(&process->address_space);
    }
    memset(process, 0, sizeof(*process));
}

void process_mark_exited(struct ios_process *process, ios_i64 exit_status)
{
    IOS_ASSERT(process != NULL);
    IOS_ASSERT(process->state != IOS_PROCESS_UNUSED);
    handle_table_close_all(&process->handles);
    if (process->kernel_stack_address != 0) {
        (void)physical_free_pages(process->kernel_stack_address, process->kernel_stack_pages);
        process->kernel_stack_address = 0;
        process->kernel_stack_pages = 0;
    }
    if (process->address_space.root_address != 0) {
        virtual_address_space_destroy(&process->address_space);
    }
    process->exit_status = exit_status;
    process->state = IOS_PROCESS_EXITED;
}

ios_status process_collect(struct ios_process *process, ios_i64 *exit_status)
{
    if (process == NULL || exit_status == NULL || process->state != IOS_PROCESS_EXITED) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    *exit_status = process->exit_status;
    memset(process, 0, sizeof(*process));
    return IOS_OK;
}

ios_status process_start_system_modules(
    const struct ios_system_module_descriptor *descriptors,
    ios_size descriptor_count,
    const struct ios_system_module_range *forbidden_ranges,
    ios_size forbidden_range_count,
    ios_system_module_digest_verifier verify_digest,
    ios_process_launch_callback launch,
    void *launch_context
)
{
    ios_status status;

    if (!process_system_ready || launch == NULL) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    status = system_module_set_validate(
        descriptors, descriptor_count, forbidden_ranges, forbidden_range_count, verify_digest
    );
    if (IOS_FAILED(status)) {
        return status;
    }
    for (ios_u32 rank = 0; rank <= 4; ++rank) {
        for (ios_size index = 0; index < descriptor_count; ++index) {
            struct ios_process *process = NULL;

            if (system_module_startup_rank(descriptors[index].role) != rank) {
                continue;
            }
            status = system_module_descriptor_validate(
                &descriptors[index], forbidden_ranges, forbidden_range_count, verify_digest
            );
            if (IOS_FAILED(status)) {
                continue;
            }
            status = process_create_from_module(&descriptors[index], &process);
            if (IOS_SUCCEEDED(status)) {
                process->state = IOS_PROCESS_RUNNABLE;
                status = launch(process, launch_context);
            }
            if (IOS_FAILED(status)) {
                if (process != NULL) {
                    process_destroy(process);
                }
                if (descriptors[index].role == IOS_MODULE_ROLE_SHELL
                    || (descriptors[index].flags & IOS_SYSTEM_MODULE_REQUIRED) != 0) {
                    return status;
                }
                continue;
            }
        }
    }
    return IOS_OK;
}

ios_size process_count(void)
{
    ios_size count = 0;
    for (ios_size index = 0; index < IOS_PROCESS_MAX_COUNT; ++index) {
        if (process_table[index].state != IOS_PROCESS_UNUSED) {
            ++count;
        }
    }
    return count;
}

const struct ios_process *process_at(ios_size requested_index)
{
    ios_size present_index = 0;
    for (ios_size index = 0; index < IOS_PROCESS_MAX_COUNT; ++index) {
        if (process_table[index].state == IOS_PROCESS_UNUSED) {
            continue;
        }
        if (present_index == requested_index) {
            return &process_table[index];
        }
        ++present_index;
    }
    return NULL;
}
