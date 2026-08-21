#ifndef INFERENCEOS_ARCH_X86_64_CPU_H
#define INFERENCEOS_ARCH_X86_64_CPU_H

#include <inferenceos/base.h>

#define INFERENCEOS_X86_64_EXCEPTION_COUNT 32U

/* Stack image produced by exceptions.S before entering C. RSP and SS are not
 * included because same-privilege exceptions do not push them. */
typedef struct inferenceos_x86_64_exception_frame {
    inferenceos_u64 rax;
    inferenceos_u64 rcx;
    inferenceos_u64 rdx;
    inferenceos_u64 rbx;
    inferenceos_u64 rbp;
    inferenceos_u64 rsi;
    inferenceos_u64 rdi;
    inferenceos_u64 r8;
    inferenceos_u64 r9;
    inferenceos_u64 r10;
    inferenceos_u64 r11;
    inferenceos_u64 r12;
    inferenceos_u64 r13;
    inferenceos_u64 r14;
    inferenceos_u64 r15;
    inferenceos_u64 vector;
    inferenceos_u64 error_code;
    inferenceos_u64 instruction_pointer;
    inferenceos_u64 code_segment;
    inferenceos_u64 flags;
} inferenceos_x86_64_exception_frame;

INFERENCEOS_STATIC_ASSERT(
    sizeof(inferenceos_x86_64_exception_frame) == 160U,
    "exception frame must match exceptions.S"
);
INFERENCEOS_STATIC_ASSERT(
    INFERENCEOS_OFFSETOF(inferenceos_x86_64_exception_frame, vector) == 120U,
    "exception vector offset must match exceptions.S"
);
INFERENCEOS_STATIC_ASSERT(
    INFERENCEOS_OFFSETOF(inferenceos_x86_64_exception_frame, error_code) == 128U,
    "exception error offset must match exceptions.S"
);
INFERENCEOS_STATIC_ASSERT(
    INFERENCEOS_OFFSETOF(inferenceos_x86_64_exception_frame, instruction_pointer) == 136U,
    "exception RIP offset must match exceptions.S"
);

void inferenceos_arch_cpu_initialize(void);
INFERENCEOS_NORETURN void inferenceos_arch_halt(void);
INFERENCEOS_NORETURN void inferenceos_arch_reboot(void);

/* Called only by exceptions.S with interrupts disabled and RDI pointing to
 * the exact frame layout above. It never returns to the faulting context. */
INFERENCEOS_NORETURN void inferenceos_arch_exception_dispatch(
    const inferenceos_x86_64_exception_frame *frame
);

#endif
