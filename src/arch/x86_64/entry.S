.section .text.entry,"ax",@progbits
.global _start
.type _start,@function
.extern __bss_start
.extern __bss_end
.extern kernel_main
.extern ios_panic_initialize
.extern serial_initialize
.extern x86_64_gdt_initialize
.extern x86_64_interrupts_initialize

/*
 * Kernel entry ABI:
 *   RDI = loader-owned boot-information pointer
 *   interrupts disabled; direction flag is not assumed
 * The loader has already exited boot services and loaded every PT_LOAD range.
 */
_start:
    cli
    cld
    movq %rdi, %r12

    leaq __bss_start(%rip), %rdi
    leaq __bss_end(%rip), %rcx
    subq %rdi, %rcx
    xorl %eax, %eax
    rep stosb

    leaq __bootstrap_stack_top(%rip), %rsp
    andq $-16, %rsp
    xorq %rbp, %rbp

    call serial_initialize
    call x86_64_gdt_initialize
    call x86_64_interrupts_initialize
    call ios_panic_initialize

    movq %r12, %rdi
    call kernel_main
    jmp x86_64_halt_forever
.size _start, .-_start

.section .text,"ax",@progbits
.global x86_64_load_gdt
.type x86_64_load_gdt,@function
x86_64_load_gdt:
    lgdt (%rdi)
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    pushq $0x08
    leaq .Lgdt_loaded(%rip), %rax
    pushq %rax
    lretq
.Lgdt_loaded:
    ret
.size x86_64_load_gdt, .-x86_64_load_gdt

.global x86_64_load_task_register
.type x86_64_load_task_register,@function
x86_64_load_task_register:
    movw %di, %ax
    ltr %ax
    ret
.size x86_64_load_task_register, .-x86_64_load_task_register

.global x86_64_load_idt
.type x86_64_load_idt,@function
x86_64_load_idt:
    lidt (%rdi)
    ret
.size x86_64_load_idt, .-x86_64_load_idt

.global x86_64_interrupt_enable
.type x86_64_interrupt_enable,@function
x86_64_interrupt_enable:
    sti
    ret
.size x86_64_interrupt_enable, .-x86_64_interrupt_enable

.global x86_64_interrupt_disable
.type x86_64_interrupt_disable,@function
x86_64_interrupt_disable:
    cli
    ret
.size x86_64_interrupt_disable, .-x86_64_interrupt_disable

.global x86_64_interrupt_save_disable
.type x86_64_interrupt_save_disable,@function
x86_64_interrupt_save_disable:
    pushfq
    popq %rax
    cli
    ret
.size x86_64_interrupt_save_disable, .-x86_64_interrupt_save_disable

.global x86_64_interrupt_restore
.type x86_64_interrupt_restore,@function
x86_64_interrupt_restore:
    testq $0x200, %rdi
    jz .Linterrupt_restore_done
    sti
.Linterrupt_restore_done:
    ret
.size x86_64_interrupt_restore, .-x86_64_interrupt_restore

.global x86_64_halt_forever
.type x86_64_halt_forever,@function
x86_64_halt_forever:
    cli
.Lhalt:
    hlt
    jmp .Lhalt
.size x86_64_halt_forever, .-x86_64_halt_forever

.macro X86_64_SAVE_REGISTERS
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
.endm

.macro X86_64_RESTORE_REGISTERS
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
.endm

.extern x86_64_interrupt_dispatch
.type x86_64_interrupt_common,@function
x86_64_interrupt_common:
    cld
    X86_64_SAVE_REGISTERS
    movq %rsp, %rdi
    movq %rsp, %rbx
    andq $-16, %rsp
    call x86_64_interrupt_dispatch
    movq %rbx, %rsp
    X86_64_RESTORE_REGISTERS
    addq $16, %rsp
    iretq
.size x86_64_interrupt_common, .-x86_64_interrupt_common

.macro X86_64_ISR_NO_ERROR vector
    .global x86_64_isr_\vector
    .type x86_64_isr_\vector,@function
x86_64_isr_\vector:
    pushq $0
    pushq $\vector
    jmp x86_64_interrupt_common
    .size x86_64_isr_\vector, .-x86_64_isr_\vector
.endm

.macro X86_64_ISR_ERROR vector
    .global x86_64_isr_\vector
    .type x86_64_isr_\vector,@function
x86_64_isr_\vector:
    pushq $\vector
    jmp x86_64_interrupt_common
    .size x86_64_isr_\vector, .-x86_64_isr_\vector
.endm

X86_64_ISR_NO_ERROR 0
X86_64_ISR_NO_ERROR 1
X86_64_ISR_NO_ERROR 2
X86_64_ISR_NO_ERROR 3
X86_64_ISR_NO_ERROR 4
X86_64_ISR_NO_ERROR 5
X86_64_ISR_NO_ERROR 6
X86_64_ISR_NO_ERROR 7
X86_64_ISR_ERROR 8
X86_64_ISR_NO_ERROR 9
X86_64_ISR_ERROR 10
X86_64_ISR_ERROR 11
X86_64_ISR_ERROR 12
X86_64_ISR_ERROR 13
X86_64_ISR_ERROR 14
X86_64_ISR_NO_ERROR 15
X86_64_ISR_NO_ERROR 16
X86_64_ISR_ERROR 17
X86_64_ISR_NO_ERROR 18
X86_64_ISR_NO_ERROR 19
X86_64_ISR_NO_ERROR 20
X86_64_ISR_ERROR 21
X86_64_ISR_NO_ERROR 22
X86_64_ISR_NO_ERROR 23
X86_64_ISR_NO_ERROR 24
X86_64_ISR_NO_ERROR 25
X86_64_ISR_NO_ERROR 26
X86_64_ISR_NO_ERROR 27
X86_64_ISR_NO_ERROR 28
X86_64_ISR_ERROR 29
X86_64_ISR_ERROR 30
X86_64_ISR_NO_ERROR 31

.altmacro
.set x86_64_vector, 32
.rept 224
    X86_64_ISR_NO_ERROR %x86_64_vector
    .set x86_64_vector, x86_64_vector + 1
.endr

.section .rodata,"a",@progbits
.p2align 3
.global x86_64_interrupt_stub_table
.type x86_64_interrupt_stub_table,@object
.macro X86_64_STUB_ADDRESS vector
    .quad x86_64_isr_\vector
.endm
x86_64_interrupt_stub_table:
.set x86_64_vector, 0
.rept 256
    X86_64_STUB_ADDRESS %x86_64_vector
    .set x86_64_vector, x86_64_vector + 1
.endr
.size x86_64_interrupt_stub_table, .-x86_64_interrupt_stub_table
.noaltmacro

.section .bss,"aw",@nobits
.p2align 4
.global __bootstrap_stack_bottom
__bootstrap_stack_bottom:
    .skip 65536
.global __bootstrap_stack_top
__bootstrap_stack_top:

.section .note.GNU-stack,"",@progbits
