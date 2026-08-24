.section .text,"ax",@progbits
.global x86_64_syscall_entry
.type x86_64_syscall_entry,@function
.extern syscall_dispatch_frame

x86_64_syscall_entry:
    movq %rsp, x86_64_syscall_user_stack(%rip)
    movq x86_64_syscall_kernel_stack(%rip), %rsp
    testq %rsp, %rsp
    jz .Lsyscall_halt

    pushq x86_64_syscall_user_stack(%rip)
    pushq %r11
    pushq %rcx
    pushq %r9
    pushq %r8
    pushq %r10
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rax

    movq %rsp, %rdi
    call syscall_dispatch_frame

    movq 56(%rsp), %rcx
    movq 64(%rsp), %r11
    movq 72(%rsp), %rdx
    addq $80, %rsp
    movq %rdx, %rsp
    sysretq

.Lsyscall_halt:
    cli
1:
    hlt
    jmp 1b
.size x86_64_syscall_entry, .-x86_64_syscall_entry

.section .bss,"aw",@nobits
.p2align 3
.global x86_64_syscall_kernel_stack
x86_64_syscall_kernel_stack:
    .quad 0
.type x86_64_syscall_kernel_stack,@object
.size x86_64_syscall_kernel_stack, 8

.p2align 3
x86_64_syscall_user_stack:
    .quad 0
.type x86_64_syscall_user_stack,@object
.size x86_64_syscall_user_stack, 8

.section .note.GNU-stack,"",@progbits
