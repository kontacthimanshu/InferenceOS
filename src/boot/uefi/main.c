#include "loader.h"

#include <inferenceos/arch/io.h>
#include <inferenceos/boot_info.h>
#include <inferenceos/runtime.h>

const struct efi_guid ios_efi_loaded_image_guid = {
    UINT32_C(0x5b1b31a1), UINT16_C(0x9562), UINT16_C(0x11d2),
    { 0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }
};
const struct efi_guid ios_efi_simple_file_system_guid = {
    UINT32_C(0x964e5b22), UINT16_C(0x6459), UINT16_C(0x11d2),
    { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }
};
const struct efi_guid ios_efi_file_info_guid = {
    UINT32_C(0x09576e92), UINT16_C(0x6d3f), UINT16_C(0x11d2),
    { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }
};
const struct efi_guid ios_efi_graphics_output_guid = {
    UINT32_C(0x9042a9de), UINT16_C(0x23dc), UINT16_C(0x4a38),
    { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a }
};
const struct efi_guid ios_efi_device_path_guid = {
    UINT32_C(0x09576e91), UINT16_C(0x6d3f), UINT16_C(0x11d2),
    { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }
};
const struct efi_guid ios_efi_acpi_20_table_guid = {
    UINT32_C(0x8868e871), UINT16_C(0xe4f1), UINT16_C(0x11d3),
    { 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 }
};
const struct efi_guid ios_efi_acpi_table_guid = {
    UINT32_C(0xeb9d2d30), UINT16_C(0x2d88), UINT16_C(0x11d3),
    { 0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d }
};

static struct efi_system_table *system_table;
typedef void (IOS_SYSV_API *ios_kernel_entry)(const struct ios_boot_info *);

static bool guid_equal(const struct efi_guid *left, const struct efi_guid *right)
{
    return memcmp(left, right, sizeof(*left)) == 0;
}

static void *find_acpi_root(const struct efi_system_table *table)
{
    void *legacy = NULL;

    if (table == NULL || table->configuration_table == NULL) {
        return NULL;
    }
    for (efi_uintn index = 0; index < table->configuration_table_count; ++index) {
        const struct efi_configuration_table *entry = &table->configuration_table[index];
        if (guid_equal(&entry->vendor_guid, &ios_efi_acpi_20_table_guid)) {
            return entry->vendor_table;
        }
        if (guid_equal(&entry->vendor_guid, &ios_efi_acpi_table_guid)) {
            legacy = entry->vendor_table;
        }
    }
    return legacy;
}

static void serial_write_byte(ios_u8 byte)
{
    while ((x86_64_port_read8(UINT16_C(0x3fd)) & UINT8_C(0x20)) == 0) { }
    x86_64_port_write8(UINT16_C(0x3f8), byte);
}

static void diagnostic(const char *message)
{
    static const efi_char16 prefix[] = { 'I','n','f','e','r','e','n','c','e','O','S',':',' ',0 };
    efi_char16 text[160]; ios_size length = 0;
    while (message[length] != '\0' && length + 3 < IOS_ARRAY_COUNT(text)) {
        text[length] = (efi_char16)(ios_u8)message[length]; serial_write_byte((ios_u8)message[length]); ++length;
    }
    text[length++] = '\r'; text[length++] = '\n'; text[length] = 0;
    serial_write_byte('\r'); serial_write_byte('\n');
    if (system_table != NULL && system_table->console_out != NULL) {
        (void)system_table->console_out->output_string(system_table->console_out, prefix);
        (void)system_table->console_out->output_string(system_table->console_out, text);
    }
}

static ios_status status_from_efi(efi_status status)
{
    if (status == EFI_NOT_FOUND) { return IOS_ERROR(IOS_E_NOT_FOUND); }
    if (status == EFI_OUT_OF_RESOURCES) { return IOS_ERROR(IOS_E_NO_MEMORY); }
    if (status == EFI_SECURITY_VIOLATION) { return IOS_ERROR(IOS_E_ACCESS_DENIED); }
    return EFI_STATUS_ERROR(status) ? IOS_ERROR(IOS_E_IO) : IOS_OK;
}

static ios_status allocate_image(
    void *context, ios_size size, ios_uptr requested_address, bool fixed, ios_uptr *address
) {
    struct ios_uefi_environment *environment = context;
    efi_physical_address physical = requested_address;
    if (size == 0 || size > UINT64_MAX - 4095U || address == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    efi_status status = environment->services->allocate_pages(
        fixed ? EFI_ALLOCATE_ADDRESS : EFI_ALLOCATE_ANY_PAGES, EFI_LOADER_DATA,
        (size + 4095U) / 4096U, &physical
    );
    if (EFI_STATUS_ERROR(status)) { return status_from_efi(status); }
    *address = (ios_uptr)physical;
    return IOS_OK;
}

static ios_status read_file(
    void *context, const efi_char16 *path, void **data, ios_size *size
) {
    struct ios_uefi_environment *environment = context;
    struct efi_file_protocol *file = NULL;
    struct efi_file_info *information = NULL;
    efi_uintn information_size = 0; efi_uintn read_size; efi_status status;
    if (path == NULL || data == NULL || size == NULL) { return IOS_ERROR(IOS_E_INVALID_ARGUMENT); }
    status = environment->root->open(environment->root, &file, (efi_char16 *)path, EFI_FILE_MODE_READ, 0);
    if (EFI_STATUS_ERROR(status)) { return status_from_efi(status); }
    status = file->get_info(file, (struct efi_guid *)&ios_efi_file_info_guid, &information_size, NULL);
    if (status != EFI_BUFFER_TOO_SMALL || information_size < sizeof(*information)) {
        (void)file->close(file); return IOS_ERROR(IOS_E_IO);
    }
    status = environment->services->allocate_pool(EFI_LOADER_DATA, information_size, (void **)&information);
    if (!EFI_STATUS_ERROR(status)) {
        status = file->get_info(file, (struct efi_guid *)&ios_efi_file_info_guid,
            &information_size, information);
    }
    if (EFI_STATUS_ERROR(status) || information->file_size == 0 || information->file_size > SIZE_MAX) {
        if (information != NULL) { (void)environment->services->free_pool(information); }
        (void)file->close(file); return IOS_ERROR(IOS_E_IO);
    }
    *size = (ios_size)information->file_size;
    (void)environment->services->free_pool(information);
    status = environment->services->allocate_pool(EFI_LOADER_DATA, *size, data);
    if (EFI_STATUS_ERROR(status)) { (void)file->close(file); return status_from_efi(status); }
    read_size = *size;
    status = file->read(file, &read_size, *data);
    (void)file->close(file);
    if (EFI_STATUS_ERROR(status) || read_size != *size) { return IOS_ERROR(IOS_E_IO); }
    return IOS_OK;
}

static bool configure_graphics(struct ios_boot_info *information)
{
    struct efi_graphics_output_protocol *graphics = NULL;
    efi_status status = system_table->boot_services->locate_protocol(
        (struct efi_guid *)&ios_efi_graphics_output_guid, NULL, (void **)&graphics
    );
    if (EFI_STATUS_ERROR(status) || graphics == NULL || graphics->mode == NULL) { return false; }
    for (ios_u32 mode = 0; mode < graphics->mode->max_mode; ++mode) {
        struct efi_graphics_output_mode_information *candidate = NULL; efi_uintn candidate_size = 0;
        status = graphics->query_mode(graphics, mode, &candidate_size, &candidate);
        if (!EFI_STATUS_ERROR(status) && candidate != NULL
            && candidate->horizontal_resolution == 1024 && candidate->vertical_resolution == 768
            && candidate->pixel_format == 1 && candidate->pixels_per_scan_line >= 1024) {
            status = graphics->set_mode(graphics, mode);
            (void)system_table->boot_services->free_pool(candidate);
            if (EFI_STATUS_ERROR(status)) { return false; }
            information->framebuffer_address = graphics->mode->framebuffer_base;
            information->framebuffer_size = graphics->mode->framebuffer_size;
            information->framebuffer_width = 1024; information->framebuffer_height = 768;
            information->framebuffer_stride = graphics->mode->info->pixels_per_scan_line;
            information->framebuffer_format = IOS_BOOT_FRAMEBUFFER_BGRX8888;
            return true;
        }
        if (candidate != NULL) { (void)system_table->boot_services->free_pool(candidate); }
    }
    return false;
}

static void capture_boot_partition_guid(
    struct efi_boot_services *services, efi_handle device_handle,
    ios_u8 partition_guid[16]
)
{
    struct efi_device_path_protocol *node = NULL;

    if (services == NULL || device_handle == NULL || partition_guid == NULL
        || EFI_STATUS_ERROR(services->handle_protocol(device_handle,
            (struct efi_guid *)&ios_efi_device_path_guid, (void **)&node))) {
        return;
    }
    for (ios_size count = 0; node != NULL && count < 64; ++count) {
        const ios_u16 length = (ios_u16)(node->length[0] | ((ios_u16)node->length[1] << 8));

        if (length < sizeof(*node)) return;
        if (node->type == 0x04 && node->subtype == 0x01
            && length >= sizeof(struct efi_hard_drive_device_path)) {
            const struct efi_hard_drive_device_path *hard_drive = (const void *)node;
            if (hard_drive->signature_type == 0x02) {
                memcpy(partition_guid, hard_drive->partition_signature, 16);
                return;
            }
        }
        if (node->type == 0x7f) return;
        node = (void *)((ios_u8 *)node + length);
    }
}

static void seal_boot_info(struct ios_boot_info *information)
{
    ios_u8 sum = 0; information->checksum = 0;
    const ios_u8 *bytes = (const void *)information;
    for (ios_size index = 0; index < information->structure_size; ++index) { sum = (ios_u8)(sum + bytes[index]); }
    information->checksum = (ios_u16)(ios_u8)(0U - sum);
}

efi_status IOS_UEFI_API IOS_SECTION(".text.entry") efi_main(
    efi_handle image_handle, struct efi_system_table *table
) {
    static const efi_char16 kernel_path[] = {
        '\\','I','n','f','e','r','e','n','c','e','O','S','\\','K','e','r','n','e','l','\\',
        'k','e','r','n','e','l','.','e','l','f',0
    };
    static const efi_char16 manifest_path[] = {
        '\\','I','n','f','e','r','e','n','c','e','O','S','\\','S','y','s','t','e','m','\\',
        'm','o','d','u','l','e','s','.','m','a','n','i','f','e','s','t',0
    };
    struct efi_loaded_image_protocol *loaded_image = NULL;
    struct efi_simple_file_system_protocol *filesystem = NULL;
    struct ios_uefi_environment environment; struct ios_boot_info *boot = NULL;
    struct ios_system_module_descriptor *modules = NULL;
    void *kernel_image = NULL; void *manifest = NULL; ios_size kernel_size = 0, manifest_size = 0;
    ios_size module_count = 0; ios_uptr kernel_entry = 0, kernel_base = 0; ios_u64 kernel_bytes = 0;
    bool gui_unavailable = false; efi_status firmware_status; ios_status status;
    efi_uintn map_size = 0, map_key = 0, descriptor_size = 0; ios_u32 descriptor_version = 0;
    struct efi_memory_descriptor *memory_map = NULL;
    system_table = table;
    if (table == NULL || table->boot_services == NULL) { return EFI_INVALID_PARAMETER; }
    environment.services = table->boot_services; environment.root = NULL;
    firmware_status = environment.services->handle_protocol(
        image_handle, (struct efi_guid *)&ios_efi_loaded_image_guid, (void **)&loaded_image);
    if (!EFI_STATUS_ERROR(firmware_status)) firmware_status = environment.services->handle_protocol(
        loaded_image->device_handle, (struct efi_guid *)&ios_efi_simple_file_system_guid,
        (void **)&filesystem);
    if (!EFI_STATUS_ERROR(firmware_status)) firmware_status = filesystem->open_volume(filesystem, &environment.root);
    if (EFI_STATUS_ERROR(firmware_status)) { diagnostic("cannot open the ESP"); return firmware_status; }
    status = read_file(&environment, kernel_path, &kernel_image, &kernel_size);
    if (IOS_FAILED(status)) {
        diagnostic("kernel.elf could not be read from the ESP"); return EFI_NOT_FOUND;
    }
    status = ios_uefi_elf64_load_kernel(kernel_image, kernel_size,
        allocate_image, &environment, &kernel_entry, &kernel_base, &kernel_bytes);
    if (IOS_FAILED(status)) {
        diagnostic("kernel ELF64 validation or fixed-address allocation failed");
        return EFI_LOAD_ERROR;
    }
    status = read_file(&environment, manifest_path, &manifest, &manifest_size);
    if (IOS_FAILED(status)) { diagnostic("system-module manifest is missing"); return EFI_NOT_FOUND; }
    firmware_status = environment.services->allocate_pool(EFI_LOADER_DATA,
        sizeof(*modules) * IOS_SYSTEM_MODULE_MAX_COUNT, (void **)&modules);
    if (EFI_STATUS_ERROR(firmware_status)) { return firmware_status; }
    status = ios_uefi_load_modules(manifest, manifest_size, read_file, allocate_image, &environment,
        modules, IOS_SYSTEM_MODULE_MAX_COUNT, &module_count, &gui_unavailable);
    if (IOS_FAILED(status)) { diagnostic("required system-module validation failed"); return EFI_SECURITY_VIOLATION; }
    firmware_status = environment.services->allocate_pool(EFI_LOADER_DATA, sizeof(*boot), (void **)&boot);
    if (EFI_STATUS_ERROR(firmware_status)) { return firmware_status; }
    memset(boot, 0, sizeof(*boot)); boot->structure_size = sizeof(*boot); boot->version = IOS_BOOT_INFO_VERSION;
    boot->root_system_description_pointer = (ios_uptr)find_acpi_root(table);
    if (boot->root_system_description_pointer == 0) {
        diagnostic("ACPI root table is missing"); return EFI_NOT_FOUND;
    }
    boot->module_descriptors_address = (ios_uptr)modules; boot->module_descriptor_count = (ios_u32)module_count;
    boot->module_descriptor_size = sizeof(*modules); boot->esp_device_handle = (ios_uptr)loaded_image->device_handle;
    boot->runtime_services = (ios_uptr)table->runtime_services;
    capture_boot_partition_guid(
        environment.services, loaded_image->device_handle, boot->boot_partition_guid);
    if (!configure_graphics(boot) || gui_unavailable) {
        boot->flags |= IOS_BOOT_FLAG_GUI_UNAVAILABLE;
        boot->framebuffer_address = 0; boot->framebuffer_size = 0;
        boot->framebuffer_width = 0; boot->framebuffer_height = 0;
        boot->framebuffer_stride = 0; boot->framebuffer_format = 0;
    }
    firmware_status = environment.services->get_memory_map(&map_size, NULL, &map_key,
        &descriptor_size, &descriptor_version);
    if (firmware_status != EFI_BUFFER_TOO_SMALL || descriptor_size < sizeof(struct efi_memory_descriptor)) {
        diagnostic("cannot size final UEFI memory map"); return EFI_LOAD_ERROR;
    }
    map_size += descriptor_size * 8U;
    firmware_status = environment.services->allocate_pool(EFI_LOADER_DATA, map_size, (void **)&memory_map);
    if (EFI_STATUS_ERROR(firmware_status)) { return firmware_status; }
    for (ios_size attempt = 0; attempt < 3; ++attempt) {
        efi_uintn available_map_size = map_size;
        firmware_status = environment.services->get_memory_map(&available_map_size, memory_map,
            &map_key, &descriptor_size, &descriptor_version);
        if (EFI_STATUS_ERROR(firmware_status)) {
            diagnostic("cannot capture final UEFI memory map"); return firmware_status;
        }
        boot->memory_map_address = (ios_uptr)memory_map;
        boot->memory_map_count = available_map_size / descriptor_size;
        boot->memory_descriptor_size = descriptor_size; boot->memory_map_key = map_key;
        boot->memory_descriptor_version = descriptor_version; seal_boot_info(boot);
        firmware_status = environment.services->exit_boot_services(image_handle, map_key);
        if (!EFI_STATUS_ERROR(firmware_status)) { break; }
    }
    if (EFI_STATUS_ERROR(firmware_status)) {
        diagnostic("ExitBootServices rejected final map key"); return firmware_status;
    }
    ((ios_kernel_entry)kernel_entry)(boot);
    for (;;) { }
}
