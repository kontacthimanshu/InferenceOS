#ifndef INFERENCEOS_ARCH_CONTEXT_H
#define INFERENCEOS_ARCH_CONTEXT_H

#include <inferenceos/base.h>

struct x86_64_user_context {
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
    ios_uptr instruction_pointer;
    ios_u64 flags;
    ios_uptr stack_pointer;
};

_Noreturn void x86_64_restore_user_context(const struct x86_64_user_context *context);
_Noreturn void x86_64_resume_user_context(const struct x86_64_user_context *context);

#endif
