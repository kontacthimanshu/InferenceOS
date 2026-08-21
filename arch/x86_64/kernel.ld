/*
 * Fixed-address x86-64 ELF64 kernel layout.
 *
 * The UEFI loader allocates each PT_LOAD segment at p_paddr and requires
 * p_paddr == p_vaddr plus 4 KiB-aligned file offsets and physical addresses.
 * Keep each permission class in a distinct, page-aligned segment so code is
 * never writable and read-only data is never executable.
 */

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(inferenceos_kernel_entry)

PHDRS
{
    text   PT_LOAD FLAGS(5); /* PF_R | PF_X */
    rodata PT_LOAD FLAGS(4); /* PF_R */
    data   PT_LOAD FLAGS(6); /* PF_R | PF_W */
}

KERNEL_LOAD_ADDRESS = 0x00200000;
KERNEL_PAGE_SIZE = 0x1000;

SECTIONS
{
    . = KERNEL_LOAD_ADDRESS;
    __kernel_start = .;
    __kernel_load_start = .;

    .text ALIGN(KERNEL_PAGE_SIZE) :
    {
        __text_start = .;
        KEEP(*(.text.inferenceos.entry))
        *(.text .text.*)
        __text_end = .;
    } :text

    . = ALIGN(KERNEL_PAGE_SIZE);
    .rodata :
    {
        __rodata_start = .;
        *(.rodata .rodata.*)
        __rodata_end = .;
    } :rodata

    . = ALIGN(KERNEL_PAGE_SIZE);
    .data :
    {
        __data_start = .;
        *(.data .data.*)
        __data_end = .;
        __kernel_file_end = .;
    } :data

    .bss ALIGN(KERNEL_PAGE_SIZE) (NOLOAD) :
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    } :data

    . = ALIGN(KERNEL_PAGE_SIZE);
    __kernel_end = .;
    __kernel_load_end = .;

    /* A freestanding, static, non-PIE kernel must not acquire dynamic state. */
    .forbidden :
    {
        *(.dynamic .dynsym .dynstr .hash .gnu.hash)
        *(.got .got.* .plt .plt.* .iplt)
        *(.rel .rel.* .rela .rela.*)
    }

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame .eh_frame_hdr)
        *(.note .note.*)
    }
}

ASSERT(__kernel_start == KERNEL_LOAD_ADDRESS,
       "kernel does not begin at its reviewed fixed address")
ASSERT((__text_start & (KERNEL_PAGE_SIZE - 1)) == 0,
       "text segment is not page aligned")
ASSERT((__rodata_start & (KERNEL_PAGE_SIZE - 1)) == 0,
       "rodata segment is not page aligned")
ASSERT((__data_start & (KERNEL_PAGE_SIZE - 1)) == 0,
       "data segment is not page aligned")
ASSERT((__bss_start & (KERNEL_PAGE_SIZE - 1)) == 0,
       "bss is not page aligned")
ASSERT(SIZEOF(.forbidden) == 0,
       "kernel contains dynamic, relocation, GOT, or PLT sections")
