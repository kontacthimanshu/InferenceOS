#ifndef INFERENCEOS_ARCH_PCI_H
#define INFERENCEOS_ARCH_PCI_H

#include <inferenceos/errors.h>

enum {
    IOS_PCI_BAR_COUNT = 6,
    IOS_PCI_MAX_FUNCTIONS = 256
};

struct ios_pci_config_access {
    void *context;
    ios_status (*read32)(
        void *context,
        ios_u8 bus,
        ios_u8 device,
        ios_u8 function,
        ios_u16 offset,
        ios_u32 *value
    );
    ios_status (*write32)(
        void *context,
        ios_u8 bus,
        ios_u8 device,
        ios_u8 function,
        ios_u16 offset,
        ios_u32 value
    );
};

struct ios_pci_bar {
    ios_u64 address;
    ios_u64 byte_count;
    bool memory;
    bool is_64_bit;
    bool prefetchable;
};

struct ios_pci_function {
    ios_u16 segment;
    ios_u8 bus;
    ios_u8 device;
    ios_u8 function;
    ios_u16 vendor_id;
    ios_u16 device_id;
    ios_u8 class_code;
    ios_u8 subclass;
    ios_u8 programming_interface;
    ios_u8 revision;
    ios_u8 header_type;
    ios_u8 capability_pointer;
    struct ios_pci_bar bars[IOS_PCI_BAR_COUNT];
    bool has_common_configuration;
    bool has_notify_configuration;
    bool has_isr_configuration;
    bool has_device_configuration;
};

const struct ios_pci_config_access *x86_64_pci_default_access(void);
ios_status x86_64_pci_enumerate(
    struct ios_pci_function *functions,
    ios_size capacity,
    ios_size *function_count
);
ios_status x86_64_pci_enumerate_with_access(
    const struct ios_pci_config_access *access,
    struct ios_pci_function *functions,
    ios_size capacity,
    ios_size *function_count
);
ios_status x86_64_pci_probe_bars(
    const struct ios_pci_config_access *access,
    struct ios_pci_function *function
);
ios_status x86_64_pci_config_read32(
    const struct ios_pci_config_access *access,
    const struct ios_pci_function *function,
    ios_u16 offset,
    ios_u32 *value
);
ios_status x86_64_pci_config_write32(
    const struct ios_pci_config_access *access,
    const struct ios_pci_function *function,
    ios_u16 offset,
    ios_u32 value
);

#endif
