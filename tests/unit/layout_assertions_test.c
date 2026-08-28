#include <inferenceos/arch/interrupts.h>
#include <inferenceos/arch/context.h>
#include <inferenceos/arch/platform.h>
#include <inferenceos/base.h>
#include <inferenceos/boot_info.h>
#include <inferenceos/handle_table.h>
#include <inferenceos/process.h>
#include <inferenceos/scheduler.h>
#include <inferenceos/system_module.h>
#include <inferenceos/syscall.h>

#if !defined(__GNUC__) && !defined(__clang__)
#error "The foundational layout test requires a supported GCC-compatible compiler"
#endif

IOS_STATIC_ASSERT(__STDC_VERSION__ >= 201710L, "InferenceOS requires ISO C17");
IOS_STATIC_ASSERT(CHAR_BIT == 8, "serialized layouts require 8-bit bytes");
IOS_STATIC_ASSERT(sizeof(void *) == 8, "the x86-64 ABI requires 64-bit pointers");

IOS_STATIC_ASSERT(sizeof(struct x86_64_interrupt_frame) == 160, "interrupt frame size changed");
IOS_STATIC_ASSERT(sizeof(struct x86_64_user_context) == 144, "user context size changed");
IOS_STATIC_ASSERT(
    offsetof(struct x86_64_user_context, instruction_pointer) == 120,
    "user-context RIP offset must match context assembly"
);
IOS_STATIC_ASSERT(
    offsetof(struct x86_64_user_context, stack_pointer) == 136,
    "user-context RSP offset must match context assembly"
);
IOS_STATIC_ASSERT(sizeof(struct ios_syscall_frame) == 200, "syscall frame size changed");
IOS_STATIC_ASSERT(
    offsetof(struct ios_syscall_frame, number) == 144,
    "syscall number offset must match syscall entry assembly"
);
IOS_STATIC_ASSERT(
    offsetof(struct x86_64_interrupt_frame, vector) == 120,
    "interrupt vector offset must match entry assembly"
);
IOS_STATIC_ASSERT(
    offsetof(struct x86_64_interrupt_frame, error_code) == 128,
    "interrupt error-code offset must match entry assembly"
);
IOS_STATIC_ASSERT(
    offsetof(struct x86_64_interrupt_frame, rip) == 136,
    "interrupt RIP offset must match entry assembly"
);
IOS_STATIC_ASSERT(
    offsetof(struct x86_64_interrupt_frame, rflags) == 152,
    "interrupt RFLAGS offset must match entry assembly"
);

IOS_STATIC_ASSERT(sizeof(struct x86_64_platform_info) == 16, "platform discovery ABI changed");
IOS_STATIC_ASSERT(
    offsetof(struct x86_64_platform_info, pm_timer_port) == 10,
    "PM-timer port offset changed"
);
IOS_STATIC_ASSERT(X86_64_SCHEDULER_QUANTUM_MS == 10, "APIC quantum contract changed");
IOS_STATIC_ASSERT(IOS_SCHEDULER_QUANTUM_MS == 10, "scheduler quantum contract changed");

IOS_STATIC_ASSERT(
    sizeof(struct ios_system_module_descriptor) == 80,
    "system-module descriptor ABI changed"
);
IOS_STATIC_ASSERT(
    offsetof(struct ios_system_module_descriptor, application_identity) == 8,
    "module identity offset changed"
);
IOS_STATIC_ASSERT(
    offsetof(struct ios_system_module_descriptor, image_address) == 24,
    "module image-address offset changed"
);
IOS_STATIC_ASSERT(
    offsetof(struct ios_system_module_descriptor, digest) == 48,
    "module digest offset changed"
);
IOS_STATIC_ASSERT(
    sizeof(((struct ios_system_module_descriptor *)0)->digest)
        == IOS_SYSTEM_MODULE_DIGEST_SIZE,
    "module digest width changed"
);
IOS_STATIC_ASSERT(sizeof(struct ios_boot_info) == 144, "boot-information ABI changed");
IOS_STATIC_ASSERT(
    offsetof(struct ios_boot_info, memory_map_address) == 16,
    "boot memory-map address offset changed"
);
IOS_STATIC_ASSERT(
    offsetof(struct ios_boot_info, framebuffer_address) == 56,
    "boot framebuffer address offset changed"
);
IOS_STATIC_ASSERT(
    offsetof(struct ios_boot_info, module_descriptors_address) == 88,
    "boot module-descriptor address offset changed"
);

IOS_STATIC_ASSERT(IOS_HANDLE_TABLE_CAPACITY == 256, "handle-table capacity changed");
IOS_STATIC_ASSERT(sizeof(ios_handle) == 8, "handle ABI requires 64 bits");
IOS_STATIC_ASSERT(
    offsetof(struct ios_process, address_space) % sizeof(ios_uptr) == 0,
    "process address space lost native alignment"
);
IOS_STATIC_ASSERT(
    offsetof(struct ios_scheduler_task, process) % sizeof(void *) == 0,
    "scheduler process pointer lost native alignment"
);

/* The object target is compile-only; this symbol prevents an empty translation unit. */
const ios_u32 inferenceos_foundational_layout_assertions = 1;
