OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x0000010000000000;

    .text : ALIGN(0x1000)
    {
        KEEP(*(.text.entry))
        *(.text .text.* .ltext .ltext.*)
    } :text

    .rodata : ALIGN(0x1000)
    {
        *(.rodata .rodata.* .lrodata .lrodata.*)
    } :rodata

    .data : ALIGN(0x1000)
    {
        *(.data .data.* .ldata .ldata.*)
    } :data

    .bss : ALIGN(0x1000)
    {
        *(.bss .bss.* .lbss .lbss.*)
        *(COMMON)
    } :data

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame .eh_frame.*)
        *(.note .note.*)
    }
}
