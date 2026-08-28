#ifndef INFERENCEOS_UEFI_H
#define INFERENCEOS_UEFI_H

#include <inferenceos/base.h>

typedef ios_u64 efi_status;
typedef void *efi_handle;
typedef ios_u16 efi_char16;
typedef ios_uptr efi_uintn;
typedef ios_u64 efi_physical_address;

#define EFI_ERROR_BIT (UINT64_C(1) << 63)
#define EFI_ERROR(code) (EFI_ERROR_BIT | (code))
#define EFI_SUCCESS UINT64_C(0)
#define EFI_LOAD_ERROR EFI_ERROR(1)
#define EFI_INVALID_PARAMETER EFI_ERROR(2)
#define EFI_UNSUPPORTED EFI_ERROR(3)
#define EFI_BUFFER_TOO_SMALL EFI_ERROR(5)
#define EFI_NOT_READY EFI_ERROR(6)
#define EFI_DEVICE_ERROR EFI_ERROR(7)
#define EFI_OUT_OF_RESOURCES EFI_ERROR(9)
#define EFI_NOT_FOUND EFI_ERROR(14)
#define EFI_SECURITY_VIOLATION EFI_ERROR(26)
#define EFI_STATUS_ERROR(status) (((status) & EFI_ERROR_BIT) != 0)

enum { EFI_ALLOCATE_ANY_PAGES = 0, EFI_ALLOCATE_ADDRESS = 2 };
enum { EFI_LOADER_CODE = 1, EFI_LOADER_DATA = 2 };
enum { EFI_FILE_MODE_READ = UINT64_C(1), EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL = 1 };

struct efi_guid { ios_u32 data1; ios_u16 data2; ios_u16 data3; ios_u8 data4[8]; };
struct efi_configuration_table {
    struct efi_guid vendor_guid;
    void *vendor_table;
};
struct efi_table_header {
    ios_u64 signature;
    ios_u32 revision;
    ios_u32 header_size;
    ios_u32 crc32;
    ios_u32 reserved;
};
struct efi_memory_descriptor {
    ios_u32 type;
    ios_u32 padding;
    efi_physical_address physical_start;
    ios_u64 virtual_start;
    ios_u64 number_of_pages;
    ios_u64 attributes;
};

struct efi_device_path_protocol {
    ios_u8 type;
    ios_u8 subtype;
    ios_u8 length[2];
};

struct IOS_PACKED efi_hard_drive_device_path {
    struct efi_device_path_protocol header;
    ios_u32 partition_number;
    ios_u64 partition_start;
    ios_u64 partition_size;
    ios_u8 partition_signature[16];
    ios_u8 partition_format;
    ios_u8 signature_type;
};

struct efi_simple_text_output_protocol;
typedef efi_status (IOS_UEFI_API *efi_output_string)(
    struct efi_simple_text_output_protocol *, const efi_char16 *
);
struct efi_simple_text_output_protocol {
    void *reset;
    efi_output_string output_string;
};

struct efi_file_protocol;
typedef efi_status (IOS_UEFI_API *efi_file_open)(
    struct efi_file_protocol *, struct efi_file_protocol **, efi_char16 *, ios_u64, ios_u64
);
typedef efi_status (IOS_UEFI_API *efi_file_close)(struct efi_file_protocol *);
typedef efi_status (IOS_UEFI_API *efi_file_read)(
    struct efi_file_protocol *, efi_uintn *, void *
);
typedef efi_status (IOS_UEFI_API *efi_file_get_info)(
    struct efi_file_protocol *, struct efi_guid *, efi_uintn *, void *
);
struct efi_file_protocol {
    ios_u64 revision;
    efi_file_open open;
    efi_file_close close;
    void *delete_file;
    efi_file_read read;
    void *write;
    void *get_position;
    void *set_position;
    efi_file_get_info get_info;
};

struct efi_simple_file_system_protocol;
typedef efi_status (IOS_UEFI_API *efi_open_volume)(
    struct efi_simple_file_system_protocol *, struct efi_file_protocol **
);
struct efi_simple_file_system_protocol { ios_u64 revision; efi_open_volume open_volume; };

struct efi_loaded_image_protocol {
    ios_u32 revision;
    efi_handle parent_handle;
    void *system_table;
    efi_handle device_handle;
    void *file_path;
    void *reserved;
    ios_u32 load_options_size;
    void *load_options;
    void *image_base;
    ios_u64 image_size;
};

struct efi_graphics_output_mode_information {
    ios_u32 version;
    ios_u32 horizontal_resolution;
    ios_u32 vertical_resolution;
    ios_u32 pixel_format;
    ios_u32 pixel_information[4];
    ios_u32 pixels_per_scan_line;
};
struct efi_graphics_output_protocol_mode {
    ios_u32 max_mode;
    ios_u32 mode;
    struct efi_graphics_output_mode_information *info;
    efi_uintn size_of_info;
    efi_physical_address framebuffer_base;
    efi_uintn framebuffer_size;
};
struct efi_graphics_output_protocol;
typedef efi_status (IOS_UEFI_API *efi_gop_query_mode)(
    struct efi_graphics_output_protocol *, ios_u32, efi_uintn *,
    struct efi_graphics_output_mode_information **
);
typedef efi_status (IOS_UEFI_API *efi_gop_set_mode)(
    struct efi_graphics_output_protocol *, ios_u32
);
struct efi_graphics_output_protocol {
    efi_gop_query_mode query_mode;
    efi_gop_set_mode set_mode;
    void *blt;
    struct efi_graphics_output_protocol_mode *mode;
};

struct efi_boot_services;
typedef efi_status (IOS_UEFI_API *efi_allocate_pages)(
    ios_u32, ios_u32, efi_uintn, efi_physical_address *
);
typedef efi_status (IOS_UEFI_API *efi_get_memory_map)(
    efi_uintn *, struct efi_memory_descriptor *, efi_uintn *, efi_uintn *, ios_u32 *
);
typedef efi_status (IOS_UEFI_API *efi_allocate_pool)(ios_u32, efi_uintn, void **);
typedef efi_status (IOS_UEFI_API *efi_free_pool)(void *);
typedef efi_status (IOS_UEFI_API *efi_handle_protocol)(efi_handle, struct efi_guid *, void **);
typedef efi_status (IOS_UEFI_API *efi_locate_protocol)(struct efi_guid *, void *, void **);
typedef efi_status (IOS_UEFI_API *efi_exit_boot_services)(efi_handle, efi_uintn);
struct efi_boot_services {
    struct efi_table_header header;
    void *raise_tpl; void *restore_tpl;
    efi_allocate_pages allocate_pages;
    void *free_pages;
    efi_get_memory_map get_memory_map;
    efi_allocate_pool allocate_pool;
    efi_free_pool free_pool;
    void *create_event; void *set_timer; void *wait_for_event; void *signal_event;
    void *close_event; void *check_event;
    void *install_protocol_interface; void *reinstall_protocol_interface;
    void *uninstall_protocol_interface;
    efi_handle_protocol handle_protocol;
    void *reserved;
    void *register_protocol_notify; void *locate_handle; void *locate_device_path;
    void *install_configuration_table;
    void *load_image; void *start_image; void *exit; void *unload_image;
    efi_exit_boot_services exit_boot_services;
    void *get_next_monotonic_count; void *stall; void *set_watchdog_timer;
    void *connect_controller; void *disconnect_controller;
    void *open_protocol; void *close_protocol; void *open_protocol_information;
    void *protocols_per_handle; void *locate_handle_buffer;
    efi_locate_protocol locate_protocol;
};

typedef void (IOS_UEFI_API *efi_reset_system)(
    ios_u32 reset_type, efi_status reset_status, efi_uintn data_size, void *reset_data
);
struct efi_runtime_services {
    struct efi_table_header header;
    void *get_time;
    void *set_time;
    void *get_wakeup_time;
    void *set_wakeup_time;
    void *set_virtual_address_map;
    void *convert_pointer;
    void *get_variable;
    void *get_next_variable_name;
    void *set_variable;
    void *get_next_high_monotonic_count;
    efi_reset_system reset_system;
    void *update_capsule;
    void *query_capsule_capabilities;
    void *query_variable_info;
};

struct efi_system_table {
    struct efi_table_header header;
    efi_char16 *firmware_vendor;
    ios_u32 firmware_revision;
    efi_handle console_in_handle;
    void *console_in;
    efi_handle console_out_handle;
    struct efi_simple_text_output_protocol *console_out;
    efi_handle standard_error_handle;
    struct efi_simple_text_output_protocol *standard_error;
    struct efi_runtime_services *runtime_services;
    struct efi_boot_services *boot_services;
    efi_uintn configuration_table_count;
    struct efi_configuration_table *configuration_table;
};

struct efi_file_info {
    ios_u64 size;
    ios_u64 file_size;
    ios_u64 physical_size;
    ios_u64 create_time[2];
    ios_u64 last_access_time[2];
    ios_u64 modification_time[2];
    ios_u64 attribute;
    efi_char16 file_name[1];
};

extern const struct efi_guid ios_efi_loaded_image_guid;
extern const struct efi_guid ios_efi_simple_file_system_guid;
extern const struct efi_guid ios_efi_file_info_guid;
extern const struct efi_guid ios_efi_graphics_output_guid;
extern const struct efi_guid ios_efi_device_path_guid;
extern const struct efi_guid ios_efi_acpi_20_table_guid;
extern const struct efi_guid ios_efi_acpi_table_guid;

#endif
