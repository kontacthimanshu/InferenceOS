#include <inferenceos/boot.h>

#include <inferenceos/runtime.h>

enum efi_memory_type {
    EFI_RESERVED_MEMORY_TYPE = 0,
    EFI_LOADER_CODE = 1,
    EFI_LOADER_DATA = 2,
    EFI_BOOT_SERVICES_CODE = 3,
    EFI_BOOT_SERVICES_DATA = 4,
    EFI_RUNTIME_SERVICES_CODE = 5,
    EFI_RUNTIME_SERVICES_DATA = 6,
    EFI_CONVENTIONAL_MEMORY = 7,
    EFI_UNUSABLE_MEMORY = 8,
    EFI_ACPI_RECLAIM_MEMORY = 9
};

struct efi_memory_descriptor_prefix {
    ios_u32 type;
    ios_u32 padding;
    ios_u64 physical_start;
    ios_u64 virtual_start;
    ios_u64 number_of_pages;
    ios_u64 attributes;
};

IOS_STATIC_ASSERT(
    sizeof(struct efi_memory_descriptor_prefix) == IOS_BOOT_MEMORY_DESCRIPTOR_MINIMUM_SIZE,
    "UEFI memory descriptor prefix size"
);

static bool multiply_overflows(ios_u64 left, ios_u64 right, ios_u64 *result)
{
    if (left != 0 && right > UINT64_MAX / left) {
        return true;
    }
    *result = left * right;
    return false;
}

static bool range_is_valid(ios_uptr base, ios_u64 byte_count)
{
    return base != 0 && byte_count != 0 && byte_count <= UINT64_MAX - base;
}

static bool ranges_overlap(
    ios_uptr left_base,
    ios_u64 left_size,
    ios_uptr right_base,
    ios_u64 right_size
)
{
    return left_base < right_base + right_size && right_base < left_base + left_size;
}

static ios_u8 byte_checksum(const void *data, ios_size byte_count)
{
    const ios_u8 *bytes = data;
    ios_u8 sum = 0;

    for (ios_size index = 0; index < byte_count; ++index) {
        sum = (ios_u8)(sum + bytes[index]);
    }
    return sum;
}

static bool module_role_is_known(ios_u32 role)
{
    return role >= IOS_MODULE_ROLE_SHELL && role <= IOS_MODULE_ROLE_TEST_APPLICATION;
}

static ios_status module_table_validate(const struct ios_boot_info *information)
{
    const struct ios_system_module_descriptor *modules =
        (const void *)information->module_descriptors_address;
    ios_size shell_count = 0;

    for (ios_size index = 0; index < information->module_descriptor_count; ++index) {
        const struct ios_system_module_descriptor *module = &modules[index];

        if (module->structure_size != sizeof(*module)
            || module->structure_version != IOS_SYSTEM_MODULE_ABI_VERSION
            || module->entry_abi_version != IOS_SYSTEM_MODULE_ABI_VERSION) {
            return IOS_ERROR(IOS_E_UNSUPPORTED_VERSION);
        }
        if (module->application_identity == 0 || !module_role_is_known(module->role)
            || (module->flags & ~IOS_SYSTEM_MODULE_REQUIRED) != 0 || module->reserved != 0
            || !range_is_valid(module->image_address, module->image_size)) {
            return IOS_ERROR(IOS_E_PROTOCOL);
        }
        if (module->role == IOS_MODULE_ROLE_SHELL) {
            if ((module->flags & IOS_SYSTEM_MODULE_REQUIRED) == 0) {
                return IOS_ERROR(IOS_E_PROTOCOL);
            }
            ++shell_count;
        }
        for (ios_size other = 0; other < index; ++other) {
            if (module->application_identity == modules[other].application_identity
                || (module->role != IOS_MODULE_ROLE_TEST_APPLICATION
                    && module->role == modules[other].role)) {
                return IOS_ERROR(IOS_E_ALREADY_EXISTS);
            }
            if (ranges_overlap(
                    module->image_address,
                    module->image_size,
                    modules[other].image_address,
                    modules[other].image_size)) {
                return IOS_ERROR(IOS_E_BAD_ADDRESS);
            }
        }
    }
    return shell_count == 1 ? IOS_OK : IOS_ERROR(IOS_E_NOT_FOUND);
}

ios_status ios_boot_info_validate(const struct ios_boot_info *information)
{
    ios_u64 memory_map_bytes;
    ios_u64 module_table_bytes;
    ios_u64 framebuffer_minimum;

    if (information == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (information->structure_size != sizeof(*information)
        || information->version != IOS_BOOT_INFO_VERSION
        || information->memory_descriptor_version != IOS_BOOT_MEMORY_DESCRIPTOR_VERSION) {
        return IOS_ERROR(IOS_E_UNSUPPORTED_VERSION);
    }
    if (byte_checksum(information, information->structure_size) != 0
        || information->reserved0 != 0 || information->reserved1 != 0
        || (information->flags & ~IOS_BOOT_FLAG_GUI_UNAVAILABLE) != 0) {
        return IOS_ERROR(IOS_E_CORRUPT);
    }
    if (!range_is_valid(information->root_system_description_pointer, 20)) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    if (information->memory_map_count == 0
        || information->memory_map_count > IOS_MEMORY_MAP_MAX_REGIONS
        || information->memory_descriptor_size < IOS_BOOT_MEMORY_DESCRIPTOR_MINIMUM_SIZE
        || information->memory_descriptor_size > IOS_PAGE_SIZE
        || (information->memory_descriptor_size & (sizeof(ios_u64) - 1U)) != 0
        || multiply_overflows(
            information->memory_map_count,
            information->memory_descriptor_size,
            &memory_map_bytes)
        || !range_is_valid(information->memory_map_address, memory_map_bytes)) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    if (information->module_descriptor_count == 0
        || information->module_descriptor_count > IOS_SYSTEM_MODULE_MAX_COUNT
        || information->module_descriptor_size != sizeof(struct ios_system_module_descriptor)
        || multiply_overflows(
            information->module_descriptor_count,
            information->module_descriptor_size,
            &module_table_bytes)
        || !range_is_valid(information->module_descriptors_address, module_table_bytes)) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    if ((information->flags & IOS_BOOT_FLAG_GUI_UNAVAILABLE) != 0) {
        if (information->framebuffer_address != 0 || information->framebuffer_size != 0
            || information->framebuffer_width != 0 || information->framebuffer_height != 0
            || information->framebuffer_stride != 0 || information->framebuffer_format != 0) {
            return IOS_ERROR(IOS_E_PROTOCOL);
        }
    } else {
        if (information->framebuffer_width != 1024 || information->framebuffer_height != 768
            || information->framebuffer_stride < information->framebuffer_width
            || information->framebuffer_format != IOS_BOOT_FRAMEBUFFER_BGRX8888
            || multiply_overflows(
                information->framebuffer_stride,
                information->framebuffer_height,
                &framebuffer_minimum)
            || multiply_overflows(
                framebuffer_minimum,
                sizeof(ios_u32),
                &framebuffer_minimum)
            || !range_is_valid(information->framebuffer_address, information->framebuffer_size)
            || information->framebuffer_size < framebuffer_minimum) {
            return IOS_ERROR(IOS_E_NOT_SUPPORTED);
        }
    }
    return module_table_validate(information);
}

static enum ios_physical_memory_kind memory_kind_from_efi(ios_u32 type)
{
    if (type == EFI_CONVENTIONAL_MEMORY) {
        return IOS_PHYSICAL_USABLE;
    }
    if (type == EFI_ACPI_RECLAIM_MEMORY) {
        return IOS_PHYSICAL_RECLAIMABLE;
    }
    return IOS_PHYSICAL_RESERVED;
}

ios_status ios_boot_build_memory_regions(
    const struct ios_boot_info *information,
    struct ios_physical_memory_region *regions,
    ios_size region_capacity,
    ios_size *region_count
)
{
    ios_status status;

    if (regions == NULL || region_count == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    status = ios_boot_info_validate(information);
    if (IOS_FAILED(status)) {
        return status;
    }
    if (region_capacity < information->memory_map_count) {
        return IOS_ERROR(IOS_E_NO_SPACE);
    }

    for (ios_size index = 0; index < information->memory_map_count; ++index) {
        struct efi_memory_descriptor_prefix descriptor;
        const ios_u8 *source = (const ios_u8 *)information->memory_map_address
            + index * information->memory_descriptor_size;
        ios_u64 byte_count;

        memcpy(&descriptor, source, sizeof(descriptor));
        if (descriptor.padding != 0 || descriptor.number_of_pages == 0
            || (descriptor.physical_start & (IOS_PAGE_SIZE - 1U)) != 0
            || multiply_overflows(descriptor.number_of_pages, IOS_PAGE_SIZE, &byte_count)
            || descriptor.physical_start > UINT64_MAX - byte_count) {
            return IOS_ERROR(IOS_E_PROTOCOL);
        }
        regions[index].base = (ios_uptr)descriptor.physical_start;
        regions[index].page_count = descriptor.number_of_pages;
        regions[index].kind = memory_kind_from_efi(descriptor.type);
    }
    *region_count = (ios_size)information->memory_map_count;
    return IOS_OK;
}

static ios_status append_reservation(
    struct ios_physical_reservation *reservations,
    ios_size capacity,
    ios_size *count,
    ios_uptr base,
    ios_u64 byte_count
)
{
    if (!range_is_valid(base, byte_count)) {
        return IOS_ERROR(IOS_E_BAD_ADDRESS);
    }
    if (*count == capacity) {
        return IOS_ERROR(IOS_E_NO_SPACE);
    }
    reservations[*count].base = base;
    reservations[*count].byte_count = byte_count;
    ++*count;
    return IOS_OK;
}

ios_status ios_boot_build_reservations(
    const struct ios_boot_info *information,
    ios_uptr kernel_base,
    ios_u64 kernel_size,
    struct ios_physical_reservation *reservations,
    ios_size reservation_capacity,
    ios_size *reservation_count
)
{
    const struct ios_system_module_descriptor *modules;
    ios_u64 byte_count;
    ios_size count = 0;
    ios_status status;

    if (reservations == NULL || reservation_count == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    status = ios_boot_info_validate(information);
    if (IOS_FAILED(status)) {
        return status;
    }

#define APPEND_RESERVATION(base, size)                                            \
    do {                                                                           \
        status = append_reservation(                                               \
            reservations, reservation_capacity, &count, (base), (size));          \
        if (IOS_FAILED(status)) {                                                   \
            return status;                                                         \
        }                                                                          \
    } while (0)

    APPEND_RESERVATION(kernel_base, kernel_size);
    APPEND_RESERVATION((ios_uptr)information, information->structure_size);
    byte_count = information->memory_map_count * information->memory_descriptor_size;
    APPEND_RESERVATION(information->memory_map_address, byte_count);
    byte_count = information->module_descriptor_count * information->module_descriptor_size;
    APPEND_RESERVATION(information->module_descriptors_address, byte_count);
    if ((information->flags & IOS_BOOT_FLAG_GUI_UNAVAILABLE) == 0) {
        APPEND_RESERVATION(information->framebuffer_address, information->framebuffer_size);
    }
    modules = (const void *)information->module_descriptors_address;
    for (ios_size index = 0; index < information->module_descriptor_count; ++index) {
        APPEND_RESERVATION(modules[index].image_address, modules[index].image_size);
    }

#undef APPEND_RESERVATION

    *reservation_count = count;
    return IOS_OK;
}
