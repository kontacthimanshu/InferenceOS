#ifndef INFERENCEOS_UEFI_LOADER_H
#define INFERENCEOS_UEFI_LOADER_H

#include "uefi.h"
#include <inferenceos/errors.h>
#include <inferenceos/system_module.h>

struct ios_uefi_environment {
    struct efi_boot_services *services;
    struct efi_file_protocol *root;
};

typedef ios_status (*ios_uefi_read_file)(
    void *context, const efi_char16 *path, void **data, ios_size *size
);
typedef ios_status (*ios_uefi_allocate_image)(
    void *context, ios_size size, ios_uptr requested_address, bool fixed, ios_uptr *address
);

ios_status ios_uefi_elf64_load_kernel(
    const void *image, ios_size image_size, ios_uefi_allocate_image allocate_image,
    void *context, ios_uptr *entry_point, ios_uptr *lowest_address, ios_u64 *loaded_size
);
ios_status ios_uefi_load_modules(
    const char *manifest, ios_size manifest_size, ios_uefi_read_file read_file,
    ios_uefi_allocate_image allocate_image, void *context,
    struct ios_system_module_descriptor *descriptors, ios_size capacity, ios_size *count,
    bool *gui_unavailable
);
void ios_sha256(const void *data, ios_size size, ios_u8 digest[32]);

#endif
