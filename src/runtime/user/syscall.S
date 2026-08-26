.section .text,"ax",@progbits
.global ios_user_syscall6
.type ios_user_syscall6,@function

/* SysV call ABI -> InferenceOS syscall ABI. */
ios_user_syscall6:
    movq %rdi, %rax
    movq %rsi, %rdi
    movq %rdx, %rsi
    movq %rcx, %rdx
    movq %r8, %r10
    movq %r9, %r8
    movq 8(%rsp), %r9
    syscall
    ret
.size ios_user_syscall6, .-ios_user_syscall6

.section .note.GNU-stack,"",@progbits
