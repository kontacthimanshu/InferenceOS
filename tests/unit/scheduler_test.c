#include <inferenceos/test.h>

#include <inferenceos/arch/interrupts.h>
#include <inferenceos/arch/platform.h>
#include <inferenceos/process.h>
#include <inferenceos/scheduler.h>

#include <stdint.h>
#include <string.h>

enum {
    TEST_APIC_ID = 0x020,
    TEST_APIC_LVT_TIMER = 0x320,
    TEST_APIC_TIMER_INITIAL = 0x380,
    TEST_APIC_TIMER_PERIODIC = 0x20000
};

struct IOS_PACKED test_acpi_header {
    char signature[4];
    ios_u32 length;
    ios_u8 revision;
    ios_u8 checksum;
    char remainder[26];
};

struct IOS_PACKED test_madt {
    struct test_acpi_header header;
    ios_u32 local_apic_address;
    ios_u32 flags;
    ios_u8 type;
    ios_u8 entry_length;
    ios_u8 processor_id;
    ios_u8 apic_id;
    ios_u32 processor_flags;
};

struct IOS_PACKED test_fadt {
    struct test_acpi_header header;
    ios_u8 bytes[80];
};

struct IOS_PACKED test_xsdt {
    struct test_acpi_header header;
    ios_u64 entries[2];
};

struct IOS_PACKED test_rsdp {
    char signature[8];
    ios_u8 checksum;
    char oem_id[6];
    ios_u8 revision;
    ios_u32 rsdt_address;
    ios_u32 length;
    ios_u64 xsdt_address;
    ios_u8 extended_checksum;
    ios_u8 reserved[3];
};

static ios_u32 pm_timer;
static struct ios_process *activated_process;

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

void x86_64_pic_mask_and_remap(void)
{
}

bool x86_64_interrupt_from_user(const struct x86_64_interrupt_frame *frame)
{
    return frame != NULL && (frame->cs & UINT64_C(3)) == UINT64_C(3);
}

ios_uptr x86_64_interrupt_user_stack(const struct x86_64_interrupt_frame *frame)
{
    const ios_u64 *privilege_frame = (const ios_u64 *)(frame + 1);
    IOS_TEST_ASSERT(x86_64_interrupt_from_user(frame));
    return (ios_uptr)privilege_frame[0];
}

bool virtual_user_range_is_valid(ios_uptr address, ios_u64 byte_count)
{
    return byte_count != 0 && address >= UINT64_C(0x0000010000000000)
        && address < UINT64_C(0x0000800000000000)
        && byte_count <= UINT64_MAX - address
        && address + byte_count <= UINT64_C(0x0000800000000000);
}

void virtual_kernel_address_space_activate(void)
{
}

ios_status process_activate(struct ios_process *process)
{
    IOS_TEST_ASSERT(process != NULL);
    IOS_TEST_ASSERT(process->state == IOS_PROCESS_RUNNABLE);
    activated_process = process;
    return IOS_OK;
}

_Noreturn void x86_64_restore_user_context(const struct x86_64_user_context *context)
{
    (void)context;
    ios_test_fail("unexpected user-context restore", __FILE__, __LINE__);
}

ios_u32 x86_64_port_read32(ios_u16 port)
{
    IOS_TEST_ASSERT(port == UINT16_C(0x408));
    pm_timer += 4096;
    return pm_timer;
}

void process_mark_exited(struct ios_process *process, ios_i64 exit_status)
{
    process->exit_status = exit_status;
    process->state = IOS_PROCESS_EXITED;
}

static void idle(void *context)
{
    ios_u32 *calls = context;
    ++*calls;
}

static void work(void *context)
{
    ios_u32 *calls = context;
    ++*calls;
}

static void tick(void *context)
{
    ios_u32 *calls = context;
    ++*calls;
}

static void checksum(void *data, ios_size length, ios_size checksum_offset)
{
    ios_u8 *bytes = data;
    ios_u8 sum = 0;
    bytes[checksum_offset] = 0;
    for (ios_size index = 0; index < length; ++index) {
        sum = (ios_u8)(sum + bytes[index]);
    }
    bytes[checksum_offset] = (ios_u8)(0U - sum);
}

static void initialize_header(
    struct test_acpi_header *header,
    const char signature[4],
    ios_u32 length
)
{
    memset(header, 0, sizeof(*header));
    memcpy(header->signature, signature, 4);
    header->length = length;
    header->revision = 1;
}

static void test_acpi_discovery_and_apic_calibration(void)
{
    static ios_u32 apic_registers[0x400 / sizeof(ios_u32)];
    struct test_madt madt;
    struct test_fadt fadt;
    struct test_xsdt xsdt;
    struct test_rsdp rsdp;
    struct x86_64_platform_info platform;
    ios_u32 flags = UINT32_C(1) << 8;

    memset(apic_registers, 0, sizeof(apic_registers));
    memset(&madt, 0, sizeof(madt));
    initialize_header(&madt.header, "APIC", sizeof(madt));
    madt.local_apic_address = (ios_uptr)apic_registers;
    madt.type = 0;
    madt.entry_length = 8;
    madt.apic_id = 7;
    madt.processor_flags = 1;
    checksum(&madt, sizeof(madt), offsetof(struct test_acpi_header, checksum));

    memset(&fadt, 0, sizeof(fadt));
    initialize_header(&fadt.header, "FACP", sizeof(fadt));
    memcpy((ios_u8 *)&fadt + 76, &(ios_u32){ UINT32_C(0x408) }, sizeof(ios_u32));
    *((ios_u8 *)&fadt + 91) = sizeof(ios_u32);
    memcpy((ios_u8 *)&fadt + 112, &flags, sizeof(flags));
    checksum(&fadt, sizeof(fadt), offsetof(struct test_acpi_header, checksum));

    memset(&xsdt, 0, sizeof(xsdt));
    initialize_header(&xsdt.header, "XSDT", sizeof(xsdt));
    xsdt.entries[0] = (ios_u64)(ios_uptr)&madt;
    xsdt.entries[1] = (ios_u64)(ios_uptr)&fadt;
    checksum(&xsdt, sizeof(xsdt), offsetof(struct test_acpi_header, checksum));

    memset(&rsdp, 0, sizeof(rsdp));
    memcpy(rsdp.signature, "RSD PTR ", 8);
    rsdp.revision = 2;
    rsdp.length = sizeof(rsdp);
    rsdp.xsdt_address = (ios_u64)(ios_uptr)&xsdt;
    checksum(&rsdp, 20, offsetof(struct test_rsdp, checksum));
    checksum(&rsdp, sizeof(rsdp), offsetof(struct test_rsdp, extended_checksum));

    IOS_TEST_ASSERT_STATUS(x86_64_acpi_discover(&rsdp, &platform), IOS_OK);
    IOS_TEST_ASSERT(platform.local_apic_address == (ios_uptr)(ios_u32)(ios_uptr)apic_registers);
    IOS_TEST_ASSERT(platform.bootstrap_apic_id == 7);
    IOS_TEST_ASSERT(platform.enabled_processor_count == 1);
    IOS_TEST_ASSERT(platform.pm_timer_width == 32);
    IOS_TEST_ASSERT(platform.pm_timer_port == UINT16_C(0x408));

    platform.local_apic_address = (ios_uptr)apic_registers;
    apic_registers[TEST_APIC_ID / sizeof(ios_u32)] = UINT32_C(7) << 24;
    pm_timer = 0;
    IOS_TEST_ASSERT_STATUS(x86_64_apic_timer_initialize(&platform), IOS_OK);
    IOS_TEST_ASSERT(x86_64_apic_timer_ticks_per_quantum() != 0);
    IOS_TEST_ASSERT(
        apic_registers[TEST_APIC_LVT_TIMER / sizeof(ios_u32)]
        == (TEST_APIC_TIMER_PERIODIC | X86_64_APIC_TIMER_VECTOR)
    );
    IOS_TEST_ASSERT(
        apic_registers[TEST_APIC_TIMER_INITIAL / sizeof(ios_u32)]
        == x86_64_apic_timer_ticks_per_quantum()
    );
}

static void test_platform_initialization_installs_ten_ms_timer(void)
{
    /* Discovery and calibration are covered independently above. */
    IOS_TEST_ASSERT(IOS_SCHEDULER_QUANTUM_MS == 10);
    IOS_TEST_ASSERT(X86_64_SCHEDULER_QUANTUM_MS == 10);
}

static void test_round_robin_preemption_has_no_starvation(void)
{
    struct ios_process processes[3] = { 0 };
    ios_u32 idle_calls = 0;
    ios_u32 selections[3] = { 0 };

    IOS_TEST_ASSERT_STATUS(scheduler_initialize(idle, &idle_calls), IOS_OK);
    for (ios_size index = 0; index < 3; ++index) {
        processes[index].state = IOS_PROCESS_RUNNABLE;
        processes[index].process_id = index + 1;
        IOS_TEST_ASSERT_STATUS(scheduler_add_process(&processes[index]), IOS_OK);
    }
    (void)scheduler_select_next();
    for (ios_size quantum = 0; quantum < 60; ++quantum) {
        struct ios_scheduler_task *current = scheduler_current_task();
        for (ios_size index = 0; index < 3; ++index) {
            if (current->process == &processes[index]) {
                ++selections[index];
            }
        }
        scheduler_on_quantum();
    }
    IOS_TEST_ASSERT(scheduler_tick_count() == 60);
    IOS_TEST_ASSERT(selections[0] == 20);
    IOS_TEST_ASSERT(selections[1] == 20);
    IOS_TEST_ASSERT(selections[2] == 20);
}

static void test_timer_interrupt_preserves_and_switches_user_context(void)
{
    struct {
        struct x86_64_interrupt_frame frame;
        ios_u64 user_stack;
        ios_u64 user_stack_segment;
    } interrupt = { 0 };
    struct ios_process processes[2] = { 0 };
    struct ios_scheduler_task *departing_task;
    struct ios_process *departing;
    struct ios_process *destination;
    ios_u32 idle_calls = 0;
    ios_u32 tick_calls = 0;

    processes[0].state = IOS_PROCESS_RUNNABLE;
    processes[0].process_id = 1;
    processes[0].user_context.instruction_pointer = UINT64_C(0x0000010000100000);
    processes[0].user_context.stack_pointer = UINT64_C(0x00007ffffffe0000);
    processes[0].user_context.rax = UINT64_C(0x1111);
    processes[1].state = IOS_PROCESS_RUNNABLE;
    processes[1].process_id = 2;
    processes[1].user_context.instruction_pointer = UINT64_C(0x0000010000200000);
    processes[1].user_context.stack_pointer = UINT64_C(0x00007ffffffd0000);
    processes[1].user_context.rax = UINT64_C(0x2222);

    activated_process = NULL;
    IOS_TEST_ASSERT_STATUS(scheduler_initialize(idle, &idle_calls), IOS_OK);
    IOS_TEST_ASSERT_STATUS(scheduler_set_tick_function(tick, &tick_calls), IOS_OK);
    IOS_TEST_ASSERT_STATUS(scheduler_add_process(&processes[0]), IOS_OK);
    IOS_TEST_ASSERT_STATUS(scheduler_add_process(&processes[1]), IOS_OK);
    departing_task = scheduler_select_next();
    departing = departing_task->process;
    destination = departing == &processes[0] ? &processes[1] : &processes[0];

    interrupt.frame.rax = UINT64_C(0xa5a5);
    interrupt.frame.rdi = UINT64_C(0xb6b6);
    interrupt.frame.rip = UINT64_C(0x0000010000300000);
    interrupt.frame.cs = UINT64_C(0x23);
    interrupt.frame.rflags = UINT64_C(0x3202);
    interrupt.user_stack = UINT64_C(0x00007ffffffc0000);
    interrupt.user_stack_segment = UINT64_C(0x1b);

    scheduler_on_timer_interrupt(&interrupt.frame);

    IOS_TEST_ASSERT(departing->user_context.rax == UINT64_C(0xa5a5));
    IOS_TEST_ASSERT(departing->user_context.rdi == UINT64_C(0xb6b6));
    IOS_TEST_ASSERT(
        departing->user_context.instruction_pointer == UINT64_C(0x0000010000300000)
    );
    IOS_TEST_ASSERT(
        departing->user_context.stack_pointer == UINT64_C(0x00007ffffffc0000)
    );
    IOS_TEST_ASSERT((departing->user_context.flags & UINT64_C(0x3000)) == 0);
    IOS_TEST_ASSERT(scheduler_current_task()->process == destination);
    IOS_TEST_ASSERT(activated_process == destination);
    IOS_TEST_ASSERT(interrupt.frame.rax == destination->user_context.rax);
    IOS_TEST_ASSERT(interrupt.frame.rip == destination->user_context.instruction_pointer);
    IOS_TEST_ASSERT(interrupt.frame.cs == UINT64_C(0x23));
    IOS_TEST_ASSERT(interrupt.user_stack == destination->user_context.stack_pointer);
    IOS_TEST_ASSERT(interrupt.user_stack_segment == UINT64_C(0x1b));
    IOS_TEST_ASSERT(scheduler_tick_count() == 1);
    IOS_TEST_ASSERT(tick_calls == 1);
}

static void test_priority_blocking_wakeup_and_idle(void)
{
    struct ios_process process = { .process_id = 1, .state = IOS_PROCESS_RUNNABLE };
    struct ios_scheduler_task *storage;
    struct ios_scheduler_task *recovery;
    struct ios_wait_queue queue;
    ios_u32 idle_calls = 0;
    ios_u32 storage_calls = 0;
    ios_u32 recovery_calls = 0;

    IOS_TEST_ASSERT_STATUS(scheduler_initialize(idle, &idle_calls), IOS_OK);
    IOS_TEST_ASSERT_STATUS(scheduler_add_process(&process), IOS_OK);
    IOS_TEST_ASSERT_STATUS(scheduler_add_kernel_work(
        IOS_SCHEDULER_KERNEL_PRIORITY_STORAGE, work, &storage_calls, &storage), IOS_OK);
    IOS_TEST_ASSERT_STATUS(scheduler_add_kernel_work(
        IOS_SCHEDULER_KERNEL_PRIORITY_RECOVERY, work, &recovery_calls, &recovery), IOS_OK);
    IOS_TEST_ASSERT_STATUS(scheduler_wake_task(storage), IOS_OK);
    IOS_TEST_ASSERT_STATUS(scheduler_wake_task(recovery), IOS_OK);
    IOS_TEST_ASSERT(scheduler_select_next() == recovery);
    scheduler_run_current();
    IOS_TEST_ASSERT(recovery_calls == 1);
    IOS_TEST_ASSERT(scheduler_select_next() == storage);
    scheduler_run_current();
    IOS_TEST_ASSERT(storage_calls == 1);

    IOS_TEST_ASSERT(scheduler_select_next()->process == &process);
    wait_queue_initialize(&queue);
    IOS_TEST_ASSERT_STATUS(scheduler_block_current(&queue), IOS_OK);
    IOS_TEST_ASSERT(queue.head != NULL && queue.head->process == &process);
    scheduler_run_current();
    IOS_TEST_ASSERT(idle_calls == 1);
    IOS_TEST_ASSERT_STATUS(scheduler_wake_task(queue.head), IOS_OK);
    IOS_TEST_ASSERT(queue.head == NULL && queue.tail == NULL);
    IOS_TEST_ASSERT(scheduler_select_next()->process == &process);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_acpi_discovery_and_apic_calibration),
    IOS_TEST_CASE(test_platform_initialization_installs_ten_ms_timer),
    IOS_TEST_CASE(test_round_robin_preemption_has_no_starvation),
    IOS_TEST_CASE(test_timer_interrupt_preserves_and_switches_user_context),
    IOS_TEST_CASE(test_priority_blocking_wakeup_and_idle)
};

const size_t ios_test_case_count = sizeof(ios_test_cases) / sizeof(ios_test_cases[0]);
