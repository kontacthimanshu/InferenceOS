.set CONTEXT_R15, 0
.set CONTEXT_R14, 8
.set CONTEXT_R13, 16
.set CONTEXT_R12, 24
.set CONTEXT_R11, 32
.set CONTEXT_R10, 40
.set CONTEXT_R9, 48
.set CONTEXT_R8, 56
.set CONTEXT_RDI, 64
.set CONTEXT_RSI, 72
.set CONTEXT_RBP, 80
.set CONTEXT_RDX, 88
.set CONTEXT_RCX, 96
.set CONTEXT_RBX, 104
.set CONTEXT_RAX, 112
.set CONTEXT_RIP, 120
.set CONTEXT_RFLAGS, 128
.set CONTEXT_RSP, 136

.section .text,"ax",@progbits
.global x86_64_restore_user_context
.type x86_64_restore_user_context,@function
/* RDI = immutable struct x86_64_user_context pointer. */
x86_64_restore_user_context:
    cli
    movw $0x1b, %ax
    movw %ax, %ds
    movw %ax, %es

    movq %rdi, %rax
    pushq $0x1b
    pushq CONTEXT_RSP(%rax)
    pushq CONTEXT_RFLAGS(%rax)
    pushq $0x23
    pushq CONTEXT_RIP(%rax)

    movq CONTEXT_R15(%rax), %r15
    movq CONTEXT_R14(%rax), %r14
    movq CONTEXT_R13(%rax), %r13
    movq CONTEXT_R12(%rax), %r12
    movq CONTEXT_R11(%rax), %r11
    movq CONTEXT_R10(%rax), %r10
    movq CONTEXT_R9(%rax), %r9
    movq CONTEXT_R8(%rax), %r8
    movq CONTEXT_RDI(%rax), %rdi
    movq CONTEXT_RSI(%rax), %rsi
    movq CONTEXT_RBP(%rax), %rbp
    movq CONTEXT_RDX(%rax), %rdx
    movq CONTEXT_RCX(%rax), %rcx
    movq CONTEXT_RBX(%rax), %rbx
    movq CONTEXT_RAX(%rax), %rax
    iretq
.size x86_64_restore_user_context, .-x86_64_restore_user_context

.global x86_64_resume_user_context
.type x86_64_resume_user_context,@function
.extern scheduler_finish_switch
.extern scheduler_idle_loop
.extern __bootstrap_stack_top
/*
 * Resume after a syscall. Switching to the destination kernel stack before
 * finalizing an exit makes it safe to free the departing process's stack.
 * A null context returns control to the kernel scheduler loop.
 */
x86_64_resume_user_context:
    testq %rdi, %rdi
    jz .Lresume_kernel
    movq %rdi, %r12
    movq x86_64_syscall_kernel_stack(%rip), %rsp
    andq $-16, %rsp
    call scheduler_finish_switch
    movq %r12, %rdi
    jmp x86_64_restore_user_context

.Lresume_kernel:
    leaq __bootstrap_stack_top(%rip), %rsp
    andq $-16, %rsp
    call scheduler_finish_switch
    call scheduler_idle_loop
    ud2
.size x86_64_resume_user_context, .-x86_64_resume_user_context

.section .bss,"aw",@nobits
.p2align 3
.global x86_64_syscall_kernel_stack
.type x86_64_syscall_kernel_stack,@object
x86_64_syscall_kernel_stack:
    .quad 0
.size x86_64_syscall_kernel_stack, 8

.section .note.GNU-stack,"",@progbits
