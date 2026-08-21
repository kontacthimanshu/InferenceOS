#ifndef INFERENCEOS_BOOT_H
#define INFERENCEOS_BOOT_H

#include <inferenceos/framebuffer.h>

#define INFERENCEOS_UEFI_HANDOFF_MAGIC UINT64_C(0x31464F444E414849)
#define INFERENCEOS_UEFI_HANDOFF_VERSION UINT32_C(1)
#define INFERENCEOS_UEFI_MEMORY_DESCRIPTOR_VERSION UINT32_C(1)
#define INFERENCEOS_UEFI_CONVENTIONAL_MEMORY UINT32_C(7)

typedef struct inferenceos_uefi_memory_descriptor {
    inferenceos_u32 type;
    inferenceos_u32 padding;
    inferenceos_u64 physical_start;
    inferenceos_u64 virtual_start;
    inferenceos_u64 page_count;
    inferenceos_u64 attributes;
} inferenceos_uefi_memory_descriptor;

typedef struct inferenceos_uefi_handoff {
    inferenceos_u64 magic;
    inferenceos_u32 version;
    inferenceos_u32 size;
    inferenceos_u64 memory_map;
    inferenceos_u64 memory_map_size;
    inferenceos_u64 memory_descriptor_size;
    inferenceos_u32 memory_descriptor_version;
    inferenceos_u32 reserved;
    inferenceos_u64 framebuffer_base;
    inferenceos_u64 framebuffer_size;
    inferenceos_u32 horizontal_resolution;
    inferenceos_u32 vertical_resolution;
    inferenceos_u32 pixels_per_scan_line;
    inferenceos_u32 pixel_format;
    inferenceos_framebuffer_masks pixel_masks;
} inferenceos_uefi_handoff;

INFERENCEOS_STATIC_ASSERT(sizeof(inferenceos_uefi_memory_descriptor) == 40U,
    "UEFI memory descriptor prefix layout");
INFERENCEOS_STATIC_ASSERT(
    INFERENCEOS_OFFSETOF(inferenceos_uefi_memory_descriptor, physical_start) == 8U,
    "UEFI physical-start offset"
);
INFERENCEOS_STATIC_ASSERT(
    INFERENCEOS_OFFSETOF(inferenceos_uefi_memory_descriptor, page_count) == 24U,
    "UEFI page-count offset"
);
INFERENCEOS_STATIC_ASSERT(sizeof(inferenceos_uefi_handoff) == 96U,
    "UEFI handoff layout");
INFERENCEOS_STATIC_ASSERT(
    INFERENCEOS_OFFSETOF(inferenceos_uefi_handoff, framebuffer_base) == 48U,
    "UEFI handoff framebuffer offset"
);

INFERENCEOS_NORETURN void inferenceos_kernel_main(
    const inferenceos_uefi_handoff *handoff
);

#endif
