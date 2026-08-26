#ifndef INFERENCEOS_ARCH_INTERRUPTS_H
#define INFERENCEOS_ARCH_INTERRUPTS_H

#include <inferenceos/base.h>

enum {
    X86_64_INTERRUPT_VECTOR_COUNT = 256,
    X86_64_EXCEPTION_VECTOR_COUNT = 32
};

/*
 * The assembly entry path saves registers in this exact order. user_rsp and
 * user_ss follow this structure only when cs indicates a transition from
 * ring 3; handlers must test x86_64_interrupt_from_user() before reading them.
 */
struct x86_64_interrupt_frame {
    ios_u64 r15;
    ios_u64 r14;
    ios_u64 r13;
    ios_u64 r12;
    ios_u64 r11;
    ios_u64 r10;
    ios_u64 r9;
    ios_u64 r8;
    ios_u64 rdi;
    ios_u64 rsi;
    ios_u64 rbp;
    ios_u64 rdx;
    ios_u64 rcx;
    ios_u64 rbx;
    ios_u64 rax;
    ios_u64 vector;
    ios_u64 error_code;
    ios_u64 rip;
    ios_u64 cs;
    ios_u64 rflags;
};

typedef void (*x86_64_interrupt_handler)(struct x86_64_interrupt_frame *frame);

void x86_64_interrupts_initialize(void);
void x86_64_interrupt_set_handler(ios_u8 vector, x86_64_interrupt_handler handler);
void x86_64_interrupt_dispatch(struct x86_64_interrupt_frame *frame);
bool x86_64_interrupt_from_user(const struct x86_64_interrupt_frame *frame);
ios_uptr x86_64_interrupt_user_stack(const struct x86_64_interrupt_frame *frame);

void x86_64_interrupt_enable(void);
void x86_64_interrupt_disable(void);
ios_u64 x86_64_interrupt_save_disable(void);
void x86_64_interrupt_restore(ios_u64 previous_flags);
void x86_64_halt(void);
_Noreturn void x86_64_halt_forever(void);

#endif
