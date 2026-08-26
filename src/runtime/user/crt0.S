.section .text.entry,"ax",@progbits
.global _start
.type _start,@function
.extern ios_user_main
.extern ios_user_process_exit

_start:
    xorq %rbp, %rbp
    andq $-16, %rsp
    call ios_user_main
    movq %rax, %rdi
    call ios_user_process_exit
    ud2
.size _start, .-_start

.section .note.GNU-stack,"",@progbits
