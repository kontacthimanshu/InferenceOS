/* Minimal x86-64 UEFI loader.  Firmware calls are routed through the assembly
 * Microsoft-x64 bridge implemented by T031; this C file remains SysV C17. */

#include <inferenceos/boot.h>

#define EFI_ERROR_BIT (UINT64_C(1) << 63)
#define EFI_SUCCESS UINT64_C(0)
#define EFI_INVALID_PARAMETER (EFI_ERROR_BIT | UINT64_C(2))
#define EFI_BUFFER_TOO_SMALL (EFI_ERROR_BIT | UINT64_C(5))
#define EFI_NOT_FOUND (EFI_ERROR_BIT | UINT64_C(14))
#define EFI_PAGE_SIZE UINT64_C(4096)
#define EFI_ALLOCATE_ANY_PAGES 0U
#define EFI_ALLOCATE_ADDRESS 2U
#define EFI_LOADER_DATA 2U
#define EFI_FILE_MODE_READ UINT64_C(1)
#define EFI_OPEN_PROTOCOL_GET_PROTOCOL 2U
#define ELF_PROGRAM_LOAD 1U
#define ELF_FLAG_EXECUTE 1U
#define ELF_MAX_PROGRAM_HEADERS 64U
#define MEMORY_MAP_SLACK_DESCRIPTORS 16U
#define EXIT_BOOT_SERVICES_ATTEMPTS 3U

typedef inferenceos_u64 efi_status;
typedef void *efi_handle;

typedef struct efi_guid {
    inferenceos_u32 data1;
    inferenceos_u16 data2;
    inferenceos_u16 data3;
    inferenceos_u8 data4[8];
} efi_guid;

typedef struct efi_table_header {
    inferenceos_u64 signature;
    inferenceos_u32 revision;
    inferenceos_u32 header_size;
    inferenceos_u32 crc32;
    inferenceos_u32 reserved;
} efi_table_header;

typedef struct efi_boot_services {
    efi_table_header header;
    void *raise_tpl; void *restore_tpl;
    void *allocate_pages; void *free_pages; void *get_memory_map;
    void *allocate_pool; void *free_pool;
    void *create_event; void *set_timer; void *wait_for_event; void *signal_event;
    void *close_event; void *check_event;
    void *install_protocol_interface; void *reinstall_protocol_interface;
    void *uninstall_protocol_interface; void *handle_protocol;
    void *reserved; void *register_protocol_notify; void *locate_handle;
    void *locate_device_path; void *install_configuration_table;
    void *load_image; void *start_image; void *exit; void *unload_image;
    void *exit_boot_services; void *get_next_monotonic_count; void *stall;
    void *set_watchdog_timer; void *connect_controller; void *disconnect_controller;
    void *open_protocol; void *close_protocol; void *open_protocol_information;
    void *protocols_per_handle; void *locate_handle_buffer; void *locate_protocol;
    void *install_multiple_protocol_interfaces;
    void *uninstall_multiple_protocol_interfaces; void *calculate_crc32;
    void *copy_mem; void *set_mem; void *create_event_ex;
} efi_boot_services;

typedef struct efi_system_table {
    efi_table_header header;
    inferenceos_u16 *firmware_vendor;
    inferenceos_u32 firmware_revision;
    efi_handle console_in_handle; void *console_in;
    efi_handle console_out_handle; void *console_out;
    efi_handle standard_error_handle; void *standard_error;
    void *runtime_services;
    efi_boot_services *boot_services;
    inferenceos_size configuration_table_count;
    void *configuration_table;
} efi_system_table;

typedef struct efi_loaded_image_protocol {
    inferenceos_u32 revision;
    efi_handle parent_handle;
    efi_system_table *system_table;
    efi_handle device_handle;
    void *file_path;
    void *reserved;
    inferenceos_u32 load_options_size;
    void *load_options;
    void *image_base;
    inferenceos_u64 image_size;
    inferenceos_u32 image_code_type;
    inferenceos_u32 image_data_type;
    void *unload;
} efi_loaded_image_protocol;

typedef struct efi_simple_file_system_protocol {
    inferenceos_u64 revision;
    void *open_volume;
} efi_simple_file_system_protocol;

typedef struct efi_file_protocol {
    inferenceos_u64 revision;
    void *open; void *close; void *delete_file; void *read; void *write;
    void *get_position; void *set_position; void *get_info; void *set_info;
    void *flush; void *open_ex; void *read_ex; void *write_ex; void *flush_ex;
} efi_file_protocol;

typedef struct efi_time {
    inferenceos_u16 year; inferenceos_u8 month; inferenceos_u8 day;
    inferenceos_u8 hour; inferenceos_u8 minute; inferenceos_u8 second;
    inferenceos_u8 pad1; inferenceos_u32 nanosecond; inferenceos_i16 time_zone;
    inferenceos_u8 daylight; inferenceos_u8 pad2;
} efi_time;

typedef struct efi_file_info {
    inferenceos_u64 size;
    inferenceos_u64 file_size;
    inferenceos_u64 physical_size;
    efi_time create_time;
    efi_time last_access_time;
    efi_time modification_time;
    inferenceos_u64 attribute;
    inferenceos_u16 file_name[1];
} efi_file_info;

typedef inferenceos_framebuffer_masks efi_pixel_bitmask;

typedef struct efi_gop_mode_info {
    inferenceos_u32 version;
    inferenceos_u32 horizontal_resolution;
    inferenceos_u32 vertical_resolution;
    inferenceos_u32 pixel_format;
    efi_pixel_bitmask pixel_information;
    inferenceos_u32 pixels_per_scan_line;
} efi_gop_mode_info;

typedef struct efi_gop_mode {
    inferenceos_u32 max_mode; inferenceos_u32 mode;
    efi_gop_mode_info *info; inferenceos_size info_size;
    inferenceos_u64 framebuffer_base; inferenceos_size framebuffer_size;
} efi_gop_mode;

typedef struct efi_graphics_output_protocol {
    void *query_mode; void *set_mode; void *blt; efi_gop_mode *mode;
} efi_graphics_output_protocol;

typedef struct elf64_header {
    inferenceos_u8 identity[16];
    inferenceos_u16 type; inferenceos_u16 machine;
    inferenceos_u32 version;
    inferenceos_u64 entry; inferenceos_u64 program_header_offset;
    inferenceos_u64 section_header_offset;
    inferenceos_u32 flags;
    inferenceos_u16 header_size; inferenceos_u16 program_header_size;
    inferenceos_u16 program_header_count; inferenceos_u16 section_header_size;
    inferenceos_u16 section_header_count; inferenceos_u16 section_name_index;
} elf64_header;

typedef struct elf64_program_header {
    inferenceos_u32 type; inferenceos_u32 flags;
    inferenceos_u64 offset; inferenceos_u64 virtual_address;
    inferenceos_u64 physical_address; inferenceos_u64 file_size;
    inferenceos_u64 memory_size; inferenceos_u64 alignment;
} elf64_program_header;

INFERENCEOS_STATIC_ASSERT(sizeof(efi_table_header) == 24U, "UEFI header layout");
INFERENCEOS_STATIC_ASSERT(sizeof(efi_time) == 16U, "UEFI time layout");
INFERENCEOS_STATIC_ASSERT(INFERENCEOS_OFFSETOF(efi_file_info, file_name) == 80U,
    "UEFI file-info layout");
INFERENCEOS_STATIC_ASSERT(sizeof(elf64_header) == 64U, "ELF64 header layout");
INFERENCEOS_STATIC_ASSERT(sizeof(elf64_program_header) == 56U, "ELF64 phdr layout");

static const efi_guid loaded_image_guid = {
    0x5B1B31A1U, 0x9562U, 0x11D2U, {0x8EU, 0x3FU, 0x00U, 0xA0U, 0xC9U, 0x69U, 0x72U, 0x3BU}
};
static const efi_guid simple_file_system_guid = {
    0x964E5B22U, 0x6459U, 0x11D2U, {0x8EU, 0x39U, 0x00U, 0xA0U, 0xC9U, 0x69U, 0x72U, 0x3BU}
};
static const efi_guid file_info_guid = {
    0x09576E92U, 0x6D3FU, 0x11D2U, {0x8EU, 0x39U, 0x00U, 0xA0U, 0xC9U, 0x69U, 0x72U, 0x3BU}
};
static const efi_guid graphics_output_guid = {
    0x9042A9DEU, 0x23DCU, 0x4A38U, {0x96U, 0xFBU, 0x7AU, 0xDEU, 0xD0U, 0x80U, 0x51U, 0x6AU}
};
static inferenceos_u16 kernel_path[] = {
    '\\', 'k', 'e', 'r', 'n', 'e', 'l', '.', 'e', 'l', 'f', 0U
};

/* T031 supplies these two isolated assembly boundaries. */
extern inferenceos_u64 inferenceos_uefi_call(
    void *function, inferenceos_u64 argument1, inferenceos_u64 argument2,
    inferenceos_u64 argument3, inferenceos_u64 argument4, inferenceos_u64 argument5
);
extern INFERENCEOS_NORETURN void inferenceos_enter_kernel(
    inferenceos_u64 entry, const inferenceos_uefi_handoff *handoff
);

static inferenceos_u64 pointer_value(const void *pointer)
{
    return (inferenceos_u64)(inferenceos_uptr)pointer;
}

static efi_status firmware_call(
    void *function, inferenceos_u64 a1, inferenceos_u64 a2,
    inferenceos_u64 a3, inferenceos_u64 a4, inferenceos_u64 a5
)
{
    if (function == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    return inferenceos_uefi_call(function, a1, a2, a3, a4, a5);
}

static bool status_is_error(efi_status status)
{
    return (status & EFI_ERROR_BIT) != 0U;
}

static void zero_bytes(void *destination, inferenceos_u64 count)
{
    inferenceos_u8 *bytes = destination;
    while (count > 0U) {
        *bytes = 0U;
        ++bytes;
        --count;
    }
}

static bool ranges_overlap(
    inferenceos_u64 first_start, inferenceos_u64 first_size,
    inferenceos_u64 second_start, inferenceos_u64 second_size
)
{
    inferenceos_u64 first_end;
    inferenceos_u64 second_end;
    if (!inferenceos_checked_add_u64(first_start, first_size, &first_end)
        || !inferenceos_checked_add_u64(second_start, second_size, &second_end)) {
        return true;
    }
    return first_start < second_end && second_start < first_end;
}

static efi_status read_exact(
    efi_file_protocol *file, inferenceos_u64 offset,
    void *destination, inferenceos_size size
)
{
    efi_status status;
    inferenceos_size actual = size;
    status = firmware_call(file->set_position, pointer_value(file), offset, 0U, 0U, 0U);
    if (status_is_error(status)) {
        return status;
    }
    status = firmware_call(file->read, pointer_value(file), pointer_value(&actual),
        pointer_value(destination), 0U, 0U);
    if (status_is_error(status)) {
        return status;
    }
    return actual == size ? EFI_SUCCESS : EFI_INVALID_PARAMETER;
}

static efi_status open_kernel(
    efi_boot_services *boot, efi_handle image_handle,
    efi_file_protocol **kernel, inferenceos_u64 *file_size
)
{
    efi_loaded_image_protocol *loaded = NULL;
    efi_simple_file_system_protocol *filesystem = NULL;
    efi_file_protocol *root = NULL;
    efi_status status;
    union { inferenceos_u64 alignment; inferenceos_u8 bytes[512]; } info_storage;
    inferenceos_size info_size = sizeof(info_storage.bytes);
    efi_file_info *info = (efi_file_info *)(void *)info_storage.bytes;

    status = firmware_call(boot->handle_protocol, pointer_value(image_handle),
        pointer_value(&loaded_image_guid), pointer_value(&loaded), 0U, 0U);
    if (status_is_error(status) || loaded == NULL) {
        return status_is_error(status) ? status : EFI_NOT_FOUND;
    }
    status = firmware_call(boot->handle_protocol, pointer_value(loaded->device_handle),
        pointer_value(&simple_file_system_guid), pointer_value(&filesystem), 0U, 0U);
    if (status_is_error(status) || filesystem == NULL) {
        return status_is_error(status) ? status : EFI_NOT_FOUND;
    }
    status = firmware_call(filesystem->open_volume, pointer_value(filesystem),
        pointer_value(&root), 0U, 0U, 0U);
    if (status_is_error(status) || root == NULL) {
        return status_is_error(status) ? status : EFI_NOT_FOUND;
    }
    status = firmware_call(root->open, pointer_value(root), pointer_value(kernel),
        pointer_value(kernel_path), EFI_FILE_MODE_READ, 0U);
    if (!status_is_error(status) && *kernel != NULL) {
        status = firmware_call((*kernel)->get_info, pointer_value(*kernel),
            pointer_value(&file_info_guid), pointer_value(&info_size), pointer_value(info), 0U);
    }
    (void)firmware_call(root->close, pointer_value(root), 0U, 0U, 0U, 0U);
    if (status_is_error(status) || *kernel == NULL || info_size < 80U) {
        if (*kernel != NULL) {
            (void)firmware_call((*kernel)->close, pointer_value(*kernel), 0U, 0U, 0U, 0U);
            *kernel = NULL;
        }
        return status_is_error(status) ? status : EFI_INVALID_PARAMETER;
    }
    *file_size = info->file_size;
    return EFI_SUCCESS;
}

static bool valid_elf_header(const elf64_header *header, inferenceos_u64 file_size)
{
    inferenceos_u64 table_size;
    inferenceos_u64 table_end;
    return header->identity[0] == 0x7FU && header->identity[1] == 'E'
        && header->identity[2] == 'L' && header->identity[3] == 'F'
        && header->identity[4] == 2U && header->identity[5] == 1U
        && header->identity[6] == 1U && header->type == 2U
        && header->machine == 62U && header->version == 1U
        && header->header_size == sizeof(*header)
        && header->program_header_size == sizeof(elf64_program_header)
        && header->program_header_count > 0U
        && header->program_header_count <= ELF_MAX_PROGRAM_HEADERS
        && inferenceos_checked_mul_u64(header->program_header_count,
            sizeof(elf64_program_header), &table_size)
        && inferenceos_checked_add_u64(header->program_header_offset, table_size, &table_end)
        && table_end <= file_size;
}

static efi_status load_elf_kernel(
    efi_boot_services *boot, efi_file_protocol *file,
    inferenceos_u64 file_size, inferenceos_u64 *entry
)
{
    elf64_header header;
    elf64_program_header programs[ELF_MAX_PROGRAM_HEADERS];
    inferenceos_size program_bytes;
    bool entry_is_executable = false;
    efi_status status = read_exact(file, 0U, &header, sizeof(header));
    if (status_is_error(status) || !valid_elf_header(&header, file_size)
        || !inferenceos_checked_mul_size(header.program_header_count,
            sizeof(programs[0]), &program_bytes)) {
        return EFI_INVALID_PARAMETER;
    }
    status = read_exact(file, header.program_header_offset, programs, program_bytes);
    if (status_is_error(status)) {
        return status;
    }
    for (inferenceos_u16 index = 0U; index < header.program_header_count; ++index) {
        const elf64_program_header *program = &programs[index];
        inferenceos_u64 file_end;
        inferenceos_u64 memory_end;
        if (program->type != ELF_PROGRAM_LOAD) {
            continue;
        }
        if (program->memory_size == 0U || program->file_size > program->memory_size
            || program->physical_address != program->virtual_address
            || (program->alignment > 1U
                && ((program->alignment & (program->alignment - 1U)) != 0U
                    || (program->virtual_address & (program->alignment - 1U))
                        != (program->offset & (program->alignment - 1U))))
            || (program->physical_address & (EFI_PAGE_SIZE - 1U)) != 0U
            || (program->offset & (EFI_PAGE_SIZE - 1U)) != 0U
            || !inferenceos_checked_add_u64(program->offset, program->file_size, &file_end)
            || file_end > file_size
            || !inferenceos_checked_add_u64(program->physical_address,
                program->memory_size, &memory_end)) {
            return EFI_INVALID_PARAMETER;
        }
        for (inferenceos_u16 prior = 0U; prior < index; ++prior) {
            if (programs[prior].type == ELF_PROGRAM_LOAD
                && ranges_overlap(program->physical_address, program->memory_size,
                    programs[prior].physical_address, programs[prior].memory_size)) {
                return EFI_INVALID_PARAMETER;
            }
        }
        if (header.entry >= program->virtual_address && header.entry < memory_end
            && (program->flags & ELF_FLAG_EXECUTE) != 0U) {
            entry_is_executable = true;
        }
    }
    if (!entry_is_executable) {
        return EFI_INVALID_PARAMETER;
    }
    for (inferenceos_u16 index = 0U; index < header.program_header_count; ++index) {
        const elf64_program_header *program = &programs[index];
        inferenceos_u64 address;
        inferenceos_u64 rounded;
        inferenceos_u64 pages;
        if (program->type != ELF_PROGRAM_LOAD) {
            continue;
        }
        if (!inferenceos_checked_add_u64(program->memory_size, EFI_PAGE_SIZE - 1U, &rounded)) {
            return EFI_INVALID_PARAMETER;
        }
        pages = rounded / EFI_PAGE_SIZE;
        address = program->physical_address;
        status = firmware_call(boot->allocate_pages, EFI_ALLOCATE_ADDRESS, EFI_LOADER_DATA,
            pages, pointer_value(&address), 0U);
        if (status_is_error(status) || address != program->physical_address) {
            return status_is_error(status) ? status : EFI_INVALID_PARAMETER;
        }
        status = read_exact(file, program->offset,
            (void *)(inferenceos_uptr)program->physical_address, (inferenceos_size)program->file_size);
        if (status_is_error(status)) {
            return status;
        }
        zero_bytes((void *)(inferenceos_uptr)(program->physical_address + program->file_size),
            program->memory_size - program->file_size);
    }
    *entry = header.entry;
    return EFI_SUCCESS;
}

static void capture_gop(efi_boot_services *boot, inferenceos_uefi_handoff *handoff)
{
    efi_graphics_output_protocol *gop = NULL;
    efi_status status = firmware_call(boot->locate_protocol,
        pointer_value(&graphics_output_guid), 0U, pointer_value(&gop), 0U, 0U);
    if (status_is_error(status) || gop == NULL || gop->mode == NULL || gop->mode->info == NULL) {
        return;
    }
    handoff->framebuffer_base = gop->mode->framebuffer_base;
    handoff->framebuffer_size = gop->mode->framebuffer_size;
    handoff->horizontal_resolution = gop->mode->info->horizontal_resolution;
    handoff->vertical_resolution = gop->mode->info->vertical_resolution;
    handoff->pixels_per_scan_line = gop->mode->info->pixels_per_scan_line;
    handoff->pixel_format = gop->mode->info->pixel_format;
    handoff->pixel_masks = gop->mode->info->pixel_information;
}

static efi_status build_handoff_and_exit(
    efi_handle image_handle, efi_boot_services *boot,
    inferenceos_uefi_handoff **handoff_result
)
{
    inferenceos_size map_size = 0U;
    inferenceos_size map_key = 0U;
    inferenceos_size descriptor_size = 0U;
    inferenceos_u32 descriptor_version = 0U;
    inferenceos_u64 allocation_size;
    inferenceos_u64 allocation_pages;
    inferenceos_u64 allocation_address = 0U;
    inferenceos_size map_offset;
    efi_status status;
    inferenceos_uefi_handoff *handoff;

    status = firmware_call(boot->get_memory_map, pointer_value(&map_size), 0U,
        pointer_value(&map_key), pointer_value(&descriptor_size),
        pointer_value(&descriptor_version));
    if (status != EFI_BUFFER_TOO_SMALL || descriptor_size == 0U
        || !inferenceos_checked_mul_u64(descriptor_size,
            MEMORY_MAP_SLACK_DESCRIPTORS, &allocation_size)
        || !inferenceos_checked_add_u64(allocation_size, map_size, &allocation_size)
        || !inferenceos_checked_align_up_size(sizeof(*handoff), 16U, &map_offset)
        || !inferenceos_checked_add_u64(allocation_size, map_offset, &allocation_size)
        || !inferenceos_checked_add_u64(allocation_size, EFI_PAGE_SIZE - 1U, &allocation_size)) {
        return EFI_INVALID_PARAMETER;
    }
    allocation_pages = allocation_size / EFI_PAGE_SIZE;
    status = firmware_call(boot->allocate_pages, EFI_ALLOCATE_ANY_PAGES, EFI_LOADER_DATA,
        allocation_pages, pointer_value(&allocation_address), 0U);
    if (status_is_error(status) || allocation_address == 0U) {
        return status_is_error(status) ? status : EFI_INVALID_PARAMETER;
    }
    handoff = (inferenceos_uefi_handoff *)(inferenceos_uptr)allocation_address;
    zero_bytes(handoff, allocation_pages * EFI_PAGE_SIZE);
    handoff->magic = INFERENCEOS_UEFI_HANDOFF_MAGIC;
    handoff->version = INFERENCEOS_UEFI_HANDOFF_VERSION;
    handoff->size = sizeof(*handoff);
    handoff->memory_map = allocation_address + map_offset;
    capture_gop(boot, handoff);

    for (inferenceos_u32 attempt = 0U; attempt < EXIT_BOOT_SERVICES_ATTEMPTS; ++attempt) {
        map_size = (inferenceos_size)(allocation_pages * EFI_PAGE_SIZE - map_offset);
        status = firmware_call(boot->get_memory_map, pointer_value(&map_size),
            handoff->memory_map, pointer_value(&map_key), pointer_value(&descriptor_size),
            pointer_value(&descriptor_version));
        if (status_is_error(status)) {
            return status;
        }
        handoff->memory_map_size = map_size;
        handoff->memory_descriptor_size = descriptor_size;
        handoff->memory_descriptor_version = descriptor_version;
        status = firmware_call(boot->exit_boot_services, pointer_value(image_handle),
            map_key, 0U, 0U, 0U);
        if (status == EFI_SUCCESS) {
            *handoff_result = handoff;
            return EFI_SUCCESS;
        }
        if (status != EFI_INVALID_PARAMETER) {
            return status;
        }
    }
    return EFI_INVALID_PARAMETER;
}

efi_status inferenceos_uefi_loader_main(
    efi_handle image_handle, efi_system_table *system_table
)
{
    efi_boot_services *boot;
    efi_file_protocol *kernel = NULL;
    inferenceos_uefi_handoff *handoff = NULL;
    inferenceos_u64 kernel_file_size = 0U;
    inferenceos_u64 kernel_entry = 0U;
    efi_status status;

    if (image_handle == NULL || system_table == NULL || system_table->boot_services == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    boot = system_table->boot_services;
    status = open_kernel(boot, image_handle, &kernel, &kernel_file_size);
    if (status_is_error(status)) {
        return status;
    }
    status = load_elf_kernel(boot, kernel, kernel_file_size, &kernel_entry);
    (void)firmware_call(kernel->close, pointer_value(kernel), 0U, 0U, 0U, 0U);
    if (status_is_error(status)) {
        return status;
    }
    status = build_handoff_and_exit(image_handle, boot, &handoff);
    if (status_is_error(status)) {
        return status;
    }
    inferenceos_enter_kernel(kernel_entry, handoff);
}
