#include <inferenceos/power.h>

struct efi_table_header_runtime {
    ios_u64 signature;
    ios_u32 revision;
    ios_u32 header_size;
    ios_u32 crc32;
    ios_u32 reserved;
};

typedef void (IOS_UEFI_API *efi_reset_system_runtime)(
    ios_u32 reset_type, ios_u64 reset_status, ios_uptr data_size, void *reset_data
);

struct efi_runtime_services_minimum {
    struct efi_table_header_runtime header;
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
    efi_reset_system_runtime reset_system;
};

ios_status ios_uefi_power_transition(void *context, enum ios_power_action action)
{
    enum { EFI_RESET_COLD = 0, EFI_RESET_SHUTDOWN = 2 };
    struct efi_runtime_services_minimum *services = context;

    if (services == NULL || services->reset_system == NULL
        || (action != IOS_POWER_REBOOT && action != IOS_POWER_SHUTDOWN)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    services->reset_system(
        action == IOS_POWER_REBOOT ? EFI_RESET_COLD : EFI_RESET_SHUTDOWN,
        0, 0, NULL);
    return IOS_ERROR(IOS_E_IO);
}
