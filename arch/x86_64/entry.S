/* SysV x86-64 transition from the UEFI loader into the ELF kernel. */

    .section .text.inferenceos.entry,"ax",@progbits

/*
 * _Noreturn void inferenceos_enter_kernel(uint64_t entry,
 *                                          const void *handoff)
 *
 * SysV inputs are RDI=ELF entry and RSI=validated UEFI handoff.  Firmware
 * boot services have already ended.  Direction is normalized, RBP is cleared
 * to terminate frame walking at the boundary, and the handoff becomes the
 * entry point's first SysV argument.  The current loader stack is retained;
 * T032's fixed-address image and later memory initialization replace it only
 * after the kernel has consumed the memory map.  A returned kernel is fatal.
 */
    .globl inferenceos_enter_kernel
    .type inferenceos_enter_kernel, @function
inferenceos_enter_kernel:
    movq %rdi, %rax
    movq %rsi, %rdi
    cld
    xorl %ebp, %ebp
    andq $-16, %rsp
    call *%rax
.Lkernel_returned:
    cli
    hlt
    jmp .Lkernel_returned
    .size inferenceos_enter_kernel, .-inferenceos_enter_kernel

/*
 * ELF entry shim.  The linker may use either exported spelling; both identify
 * the same address.  RDI contains the handoff and is forwarded to the SysV C
 * kernel initializer.  No C runtime or constructors exist.  If kernel_main
 * returns, remain halted rather than returning into reclaimed loader state.
 */
    .globl _start
    .globl inferenceos_kernel_entry
    .type _start, @function
    .type inferenceos_kernel_entry, @function
_start:
inferenceos_kernel_entry:
    cld
    xorl %ebp, %ebp
    andq $-16, %rsp
    call inferenceos_kernel_main
.Lkernel_main_returned:
    cli
    hlt
    jmp .Lkernel_main_returned
    .size inferenceos_kernel_entry, .-inferenceos_kernel_entry
    .size _start, .-_start

    .section .note.GNU-stack,"",@progbits
