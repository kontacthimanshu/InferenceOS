OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

/*
 * Generation 2 Hyper-V firmware retains allocations in low memory where the
 * original 2 MiB fixed image was linked. The fixed-base PE loader already
 * requires and successfully occupies its 0x10000000 image range, so place the
 * non-relocatable kernel in a separate, page-aligned range 32 MiB above it.
 */
KERNEL_LOAD_ADDRESS = 0x12000000;
PAGE_SIZE = 0x1000;

PHDRS
{
    text PT_LOAD FLAGS(5);    /* Read + execute. */
    rodata PT_LOAD FLAGS(4);  /* Read only. */
    data PT_LOAD FLAGS(6);    /* Read + write. */
}

SECTIONS
{
    . = KERNEL_LOAD_ADDRESS;
    __kernel_start = .;

    .text ALIGN(PAGE_SIZE) :
    {
        __text_start = .;
        KEEP(*(.text.entry))
        *(.text .text.*)
        __text_end = .;
    } :text

    .rodata ALIGN(PAGE_SIZE) :
    {
        __rodata_start = .;
        *(.rodata .rodata.*)
        . = ALIGN(16);
        __inferenceos_font_start = .;
        KEEP(*(.inferenceos_font))
        __inferenceos_font_end = .;
        __rodata_end = .;
    } :rodata

    .data ALIGN(PAGE_SIZE) :
    {
        __data_start = .;
        *(.data .data.*)
        __data_end = .;
    } :data

    __kernel_file_end = .;

    .bss ALIGN(PAGE_SIZE) (NOLOAD) :
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    } :data

    . = ALIGN(PAGE_SIZE);
    __kernel_end = .;

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame .eh_frame.*)
        *(.note .note.*)
    }
}

ASSERT((KERNEL_LOAD_ADDRESS % 0x200000) == 0,
       "kernel load address must be 2 MiB aligned")
ASSERT((__kernel_end - __kernel_start) < 0x40000000,
       "kernel image exceeds the 1 GiB boot-loader limit")
