/* x86-64 descriptor loading and uniform exception-entry stubs. */

    .text

/*
 * void inferenceos_x86_64_load_gdt(const descriptor_table_pointer *pointer)
 * SysV input RDI references a packed 10-byte limit/base pair. LGDT consumes
 * those bytes; the far return reloads CS and data selectors are then set to
 * 0x10. RAX and flags are clobbered. No memory is modified.
 */
    .globl inferenceos_x86_64_load_gdt
    .type inferenceos_x86_64_load_gdt, @function
inferenceos_x86_64_load_gdt:
    lgdt (%rdi)
    pushq $0x08
    leaq .Lgdt_loaded(%rip), %rax
    pushq %rax
    lretq
.Lgdt_loaded:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorw %ax, %ax
    movw %ax, %fs
    movw %ax, %gs
    ret
    .size inferenceos_x86_64_load_gdt, .-inferenceos_x86_64_load_gdt

/*
 * void inferenceos_x86_64_load_idt(const descriptor_table_pointer *pointer)
 * SysV input RDI references a packed 10-byte limit/base pair. LIDT changes
 * only architectural IDT state; no general register or memory is modified.
 */
    .globl inferenceos_x86_64_load_idt
    .type inferenceos_x86_64_load_idt, @function
inferenceos_x86_64_load_idt:
    lidt (%rdi)
    ret
    .size inferenceos_x86_64_load_idt, .-inferenceos_x86_64_load_idt

/* Exceptions without a CPU error code synthesize zero. Exceptions with a CPU
 * error code push only their vector, producing the same vector/error layout. */
    .macro EXCEPTION_NO_ERROR vector
    .globl inferenceos_x86_64_exception_\vector
    .type inferenceos_x86_64_exception_\vector, @function
inferenceos_x86_64_exception_\vector:
    pushq $0
    pushq $\vector
    jmp inferenceos_x86_64_exception_common
    .size inferenceos_x86_64_exception_\vector, .-inferenceos_x86_64_exception_\vector
    .endm

    .macro EXCEPTION_ERROR vector
    .globl inferenceos_x86_64_exception_\vector
    .type inferenceos_x86_64_exception_\vector, @function
inferenceos_x86_64_exception_\vector:
    pushq $\vector
    jmp inferenceos_x86_64_exception_common
    .size inferenceos_x86_64_exception_\vector, .-inferenceos_x86_64_exception_\vector
    .endm

    EXCEPTION_NO_ERROR 0
    EXCEPTION_NO_ERROR 1
    EXCEPTION_NO_ERROR 2
    EXCEPTION_NO_ERROR 3
    EXCEPTION_NO_ERROR 4
    EXCEPTION_NO_ERROR 5
    EXCEPTION_NO_ERROR 6
    EXCEPTION_NO_ERROR 7
    EXCEPTION_ERROR    8
    EXCEPTION_NO_ERROR 9
    EXCEPTION_ERROR    10
    EXCEPTION_ERROR    11
    EXCEPTION_ERROR    12
    EXCEPTION_ERROR    13
    EXCEPTION_ERROR    14
    EXCEPTION_NO_ERROR 15
    EXCEPTION_NO_ERROR 16
    EXCEPTION_ERROR    17
    EXCEPTION_NO_ERROR 18
    EXCEPTION_NO_ERROR 19
    EXCEPTION_NO_ERROR 20
    EXCEPTION_ERROR    21
    EXCEPTION_NO_ERROR 22
    EXCEPTION_NO_ERROR 23
    EXCEPTION_NO_ERROR 24
    EXCEPTION_NO_ERROR 25
    EXCEPTION_NO_ERROR 26
    EXCEPTION_NO_ERROR 27
    EXCEPTION_NO_ERROR 28
    EXCEPTION_ERROR    29
    EXCEPTION_ERROR    30
    EXCEPTION_NO_ERROR 31

/* IDT vectors outside 0-31 are not enabled by T033. If one is nevertheless
 * raised, report sentinel vector 255 through the same bounded fatal path. */
    .globl inferenceos_x86_64_unhandled_stub
    .type inferenceos_x86_64_unhandled_stub, @function
inferenceos_x86_64_unhandled_stub:
    pushq $0
    pushq $255
    jmp inferenceos_x86_64_exception_common
    .size inferenceos_x86_64_unhandled_stub, .-inferenceos_x86_64_unhandled_stub

/*
 * Entry stack is vector, error, RIP, CS, RFLAGS. All integer registers are
 * saved in the exact cpu.h frame order. R12 retains the unaligned frame base
 * while RSP is aligned for the SysV C call; the dispatcher is non-returning.
 * Direction is cleared for C. Interrupt gates have already cleared IF.
 */
    .type inferenceos_x86_64_exception_common, @function
inferenceos_x86_64_exception_common:
    pushq %r15
    pushq %r14
    pushq %r13
    pushq %r12
    pushq %r11
    pushq %r10
    pushq %r9
    pushq %r8
    pushq %rdi
    pushq %rsi
    pushq %rbp
    pushq %rbx
    pushq %rdx
    pushq %rcx
    pushq %rax
    cld
    movq %rsp, %rdi
    movq %rsp, %r12
    andq $-16, %rsp
    call inferenceos_arch_exception_dispatch
    movq %r12, %rsp
    ud2
    .size inferenceos_x86_64_exception_common, .-inferenceos_x86_64_exception_common

    .section .rodata
    .globl inferenceos_x86_64_exception_stubs
    .type inferenceos_x86_64_exception_stubs, @object
    .align 8
inferenceos_x86_64_exception_stubs:
    .irp vector,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    .quad inferenceos_x86_64_exception_\vector
    .endr
    .size inferenceos_x86_64_exception_stubs, .-inferenceos_x86_64_exception_stubs

    .globl inferenceos_x86_64_unhandled_stub_address
    .type inferenceos_x86_64_unhandled_stub_address, @object
inferenceos_x86_64_unhandled_stub_address:
    .quad inferenceos_x86_64_unhandled_stub
    .size inferenceos_x86_64_unhandled_stub_address, 8

    .section .note.GNU-stack,"",@progbits
