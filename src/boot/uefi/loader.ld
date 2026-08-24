OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(efi_main)

/*
 * This first-release loader is a fixed-base PE32+ image. The target-only GNU
 * cross Binutils intentionally has no PE relocation backend, so reserve a
 * deterministic address in the reference VM rather than emit a falsely empty
 * relocation directory. OVMF must reject the image if this range is occupied.
 */
IMAGE_BASE = 0x10000000;
SECTION_ALIGNMENT = 0x1000;

SECTIONS
{
    /* Reserve the first page for PE/COFF headers during image conversion. */
    . = IMAGE_BASE + SECTION_ALIGNMENT;
    __loader_start = .;

    .text ALIGN(SECTION_ALIGNMENT) :
    {
        __loader_text_start = .;
        KEEP(*(.text.entry))
        *(.text .text.*)
        __loader_text_end = .;
    }

    .rodata ALIGN(SECTION_ALIGNMENT) :
    {
        __loader_rodata_start = .;
        *(.rodata .rodata.*)
        __loader_rodata_end = .;
    }

    .data ALIGN(SECTION_ALIGNMENT) :
    {
        __loader_data_start = .;
        *(.data .data.*)
        __loader_data_end = .;
    }

    .bss ALIGN(SECTION_ALIGNMENT) (NOLOAD) :
    {
        __loader_bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __loader_bss_end = .;
    }

    /* Preserve relocation input for the PE32+ packaging step. */
    .reloc ALIGN(SECTION_ALIGNMENT) :
    {
        KEEP(*(.reloc .reloc.*))
    }

    . = ALIGN(SECTION_ALIGNMENT);
    __loader_end = .;

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame .eh_frame.*)
        *(.note .note.*)
    }
}

ASSERT((__loader_start % SECTION_ALIGNMENT) == 0,
       "UEFI loader start must be section aligned")
ASSERT((__loader_end - IMAGE_BASE) < 0x100000000,
       "UEFI loader image exceeds the PE32+ 4 GiB image limit")
