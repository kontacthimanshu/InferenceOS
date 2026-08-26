.section .text,"ax",@progbits
.global x86_64_syscall_entry
.type x86_64_syscall_entry,@function
.extern syscall_dispatch_frame

x86_64_syscall_entry:
    movq %rsp, x86_64_syscall_user_stack(%rip)
    movq x86_64_syscall_kernel_stack(%rip), %rsp
    testq %rsp, %rsp
    jz .Lsyscall_halt

    subq $208, %rsp
    movq %r15, 0(%rsp)
    movq %r14, 8(%rsp)
    movq %r13, 16(%rsp)
    movq %r12, 24(%rsp)
    movq %r11, 32(%rsp)
    movq %r10, 40(%rsp)
    movq %r9, 48(%rsp)
    movq %r8, 56(%rsp)
    movq %rdi, 64(%rsp)
    movq %rsi, 72(%rsp)
    movq %rbp, 80(%rsp)
    movq %rdx, 88(%rsp)
    movq %rcx, 96(%rsp)
    movq %rbx, 104(%rsp)
    movq %rax, 112(%rsp)
    movq %rcx, 120(%rsp)
    movq %r11, 128(%rsp)
    movq x86_64_syscall_user_stack(%rip), %rcx
    movq %rcx, 136(%rsp)
    movq %rax, 144(%rsp)
    movq %rdi, 152(%rsp)
    movq %rsi, 160(%rsp)
    movq %rdx, 168(%rsp)
    movq %r10, 176(%rsp)
    movq %r8, 184(%rsp)
    movq %r9, 192(%rsp)

    movq %rsp, %rdi
    call syscall_dispatch_frame
    movq %rax, %rdi
    jmp x86_64_resume_user_context

.Lsyscall_halt:
    cli
1:
    hlt
    jmp 1b
.size x86_64_syscall_entry, .-x86_64_syscall_entry

.section .bss,"aw",@nobits
.p2align 3
x86_64_syscall_user_stack:
    .quad 0
.type x86_64_syscall_user_stack,@object
.size x86_64_syscall_user_stack, 8

.section .note.GNU-stack,"",@progbits
