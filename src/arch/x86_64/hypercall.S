.section .text
.global x86_64_hyperv_invoke
.type x86_64_hyperv_invoke, @function
x86_64_hyperv_invoke:
    /* SysV arguments: page, control, input GPA, output GPA.
     * Hyper-V hypercall ABI: RCX, RDX, R8 and execution through the hypercall page. */
    movq %rdi, %r11
    movq %rcx, %r8
    movq %rsi, %rcx
    subq $8, %rsp
    call *%r11
    addq $8, %rsp
    ret
.size x86_64_hyperv_invoke, .-x86_64_hyperv_invoke

.section .note.GNU-stack,"",@progbits
