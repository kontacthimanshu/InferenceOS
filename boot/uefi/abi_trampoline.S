/*
 * x86-64 UEFI ABI boundary.
 *
 * Project C is compiled for the SysV ABI. UEFI firmware uses the Microsoft
 * x64 ABI, so all crossings are kept in this reviewed assembly file rather
 * than relying on a compiler-specific calling-convention annotation.
 */

    .text

/*
 * EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system_table)
 *
 * Firmware entry state (Microsoft x64): RCX=image, RDX=system table.  The
 * SysV C loader expects those arguments in RDI and RSI.  RDI, RSI, and
 * XMM6-XMM15 are nonvolatile in the Microsoft ABI but volatile in SysV, so
 * preserve them in case the loader returns an EFI error.  RSP is 16-byte
 * aligned immediately before the SysV call.  Flags and the remaining
 * volatile registers may be clobbered by either ABI.
 */
    .globl efi_main
    .type efi_main, @function
efi_main:
    pushq %rdi
    pushq %rsi
    subq $168, %rsp
    movdqu %xmm6,   0(%rsp)
    movdqu %xmm7,  16(%rsp)
    movdqu %xmm8,  32(%rsp)
    movdqu %xmm9,  48(%rsp)
    movdqu %xmm10, 64(%rsp)
    movdqu %xmm11, 80(%rsp)
    movdqu %xmm12, 96(%rsp)
    movdqu %xmm13, 112(%rsp)
    movdqu %xmm14, 128(%rsp)
    movdqu %xmm15, 144(%rsp)

    movq %rcx, %rdi
    movq %rdx, %rsi
    call inferenceos_uefi_loader_main

    movdqu   0(%rsp), %xmm6
    movdqu  16(%rsp), %xmm7
    movdqu  32(%rsp), %xmm8
    movdqu  48(%rsp), %xmm9
    movdqu  64(%rsp), %xmm10
    movdqu  80(%rsp), %xmm11
    movdqu  96(%rsp), %xmm12
    movdqu 112(%rsp), %xmm13
    movdqu 128(%rsp), %xmm14
    movdqu 144(%rsp), %xmm15
    addq $168, %rsp
    popq %rsi
    popq %rdi
    ret
    .size efi_main, .-efi_main

/*
 * uint64_t inferenceos_uefi_call(void *function, uint64_t a1, uint64_t a2,
 *                                uint64_t a3, uint64_t a4, uint64_t a5)
 *
 * SysV inputs are RDI=function, RSI=a1, RDX=a2, RCX=a3, R8=a4, R9=a5.
 * Microsoft x64 receives RCX/RDX/R8/R9 for a1-a4 and a5 at [RSP+40] on
 * function entry.  The 40-byte reservation supplies 32 bytes of mandatory
 * shadow space plus the stack argument while keeping RSP aligned before the
 * indirect call.  RAX carries the EFI_STATUS result unchanged.  Both ABIs'
 * volatile integer registers, condition codes, and memory may be clobbered;
 * SysV nonvolatile registers are untouched.
 */
    .globl inferenceos_uefi_call
    .type inferenceos_uefi_call, @function
inferenceos_uefi_call:
    movq %rdi, %rax
    movq %r9, %r10
    movq %rcx, %r11
    movq %r8, %r9
    movq %rsi, %rcx
    movq %r11, %r8
    subq $40, %rsp
    movq %r10, 32(%rsp)
    call *%rax
    addq $40, %rsp
    ret
    .size inferenceos_uefi_call, .-inferenceos_uefi_call

    .section .note.GNU-stack,"",@progbits
