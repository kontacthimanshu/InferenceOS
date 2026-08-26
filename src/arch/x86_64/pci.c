#include <inferenceos/arch/pci.h>

#include <inferenceos/arch/interrupts.h>
#include <inferenceos/arch/io.h>

enum {
    PCI_CONFIG_ADDRESS_PORT = 0xcf8,
    PCI_CONFIG_DATA_PORT = 0xcfc,
    PCI_COMMAND_OFFSET = 0x04,
    PCI_CLASS_OFFSET = 0x08,
    PCI_HEADER_OFFSET = 0x0c,
    PCI_BAR_OFFSET = 0x10,
    PCI_CAPABILITY_POINTER_OFFSET = 0x34,
    PCI_STATUS_CAPABILITIES = UINT32_C(1) << 20,
    PCI_HEADER_MULTIFUNCTION = 0x80,
    PCI_HEADER_LAYOUT_MASK = 0x7f,
    PCI_BAR_IO = UINT32_C(1),
    PCI_BAR_MEMORY_TYPE_MASK = UINT32_C(3) << 1,
    PCI_BAR_MEMORY_32 = 0,
    PCI_BAR_MEMORY_64 = UINT32_C(2) << 1
};

static ios_status validate_access(const struct ios_pci_config_access *access)
{
    if (access == NULL || access->read32 == NULL || access->write32 == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    return IOS_OK;
}

static ios_u32 config_address(
    ios_u8 bus,
    ios_u8 device,
    ios_u8 function,
    ios_u16 offset
)
{
    return UINT32_C(0x80000000) | ((ios_u32)bus << 16) | ((ios_u32)device << 11)
        | ((ios_u32)function << 8) | (offset & UINT16_C(0xfc));
}

static ios_status legacy_read32(
    void *context,
    ios_u8 bus,
    ios_u8 device,
    ios_u8 function,
    ios_u16 offset,
    ios_u32 *value
)
{
    ios_u64 flags;

    (void)context;
    if (value == NULL || device >= 32 || function >= 8 || offset > 0xfc
        || (offset & 3U) != 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    flags = x86_64_interrupt_save_disable();
    x86_64_port_write32(PCI_CONFIG_ADDRESS_PORT, config_address(bus, device, function, offset));
    *value = x86_64_port_read32(PCI_CONFIG_DATA_PORT);
    x86_64_interrupt_restore(flags);
    return IOS_OK;
}

static ios_status legacy_write32(
    void *context,
    ios_u8 bus,
    ios_u8 device,
    ios_u8 function,
    ios_u16 offset,
    ios_u32 value
)
{
    ios_u64 flags;

    (void)context;
    if (device >= 32 || function >= 8 || offset > 0xfc || (offset & 3U) != 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    flags = x86_64_interrupt_save_disable();
    x86_64_port_write32(PCI_CONFIG_ADDRESS_PORT, config_address(bus, device, function, offset));
    x86_64_port_write32(PCI_CONFIG_DATA_PORT, value);
    x86_64_interrupt_restore(flags);
    return IOS_OK;
}

static const struct ios_pci_config_access legacy_access = {
    .context = NULL,
    .read32 = legacy_read32,
    .write32 = legacy_write32
};

const struct ios_pci_config_access *x86_64_pci_default_access(void)
{
    return &legacy_access;
}

ios_status x86_64_pci_config_read32(
    const struct ios_pci_config_access *access,
    const struct ios_pci_function *function,
    ios_u16 offset,
    ios_u32 *value
)
{
    if (IOS_FAILED(validate_access(access)) || function == NULL || value == NULL
        || function->segment != 0 || offset > 0xfc || (offset & 3U) != 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    return access->read32(
        access->context,
        function->bus,
        function->device,
        function->function,
        offset,
        value
    );
}

ios_status x86_64_pci_config_write32(
    const struct ios_pci_config_access *access,
    const struct ios_pci_function *function,
    ios_u16 offset,
    ios_u32 value
)
{
    if (IOS_FAILED(validate_access(access)) || function == NULL || function->segment != 0
        || offset > 0xfc || (offset & 3U) != 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    return access->write32(
        access->context,
        function->bus,
        function->device,
        function->function,
        offset,
        value
    );
}

static ios_status read_function(
    const struct ios_pci_config_access *access,
    ios_u8 bus,
    ios_u8 device,
    ios_u8 function_number,
    struct ios_pci_function *function
)
{
    ios_u32 identity;
    ios_u32 class_information;
    ios_u32 header;
    ios_u32 command_status;
    ios_u32 capability = 0;
    ios_status status;

    status = access->read32(
        access->context, bus, device, function_number, 0, &identity
    );
    if (IOS_FAILED(status)) {
        return status;
    }
    if ((ios_u16)identity == UINT16_C(0xffff)) {
        return IOS_ERROR(IOS_E_NOT_FOUND);
    }
    status = access->read32(
        access->context, bus, device, function_number, PCI_CLASS_OFFSET, &class_information
    );
    if (IOS_FAILED(status)) {
        return status;
    }
    status = access->read32(
        access->context, bus, device, function_number, PCI_HEADER_OFFSET, &header
    );
    if (IOS_FAILED(status)) {
        return status;
    }
    status = access->read32(
        access->context, bus, device, function_number, PCI_COMMAND_OFFSET, &command_status
    );
    if (IOS_FAILED(status)) {
        return status;
    }
    if ((command_status & PCI_STATUS_CAPABILITIES) != 0) {
        status = access->read32(
            access->context,
            bus,
            device,
            function_number,
            PCI_CAPABILITY_POINTER_OFFSET,
            &capability
        );
        if (IOS_FAILED(status)) {
            return status;
        }
    }
    *function = (struct ios_pci_function){ 0 };
    function->bus = bus;
    function->device = device;
    function->function = function_number;
    function->vendor_id = (ios_u16)identity;
    function->device_id = (ios_u16)(identity >> 16);
    function->revision = (ios_u8)class_information;
    function->programming_interface = (ios_u8)(class_information >> 8);
    function->subclass = (ios_u8)(class_information >> 16);
    function->class_code = (ios_u8)(class_information >> 24);
    function->header_type = (ios_u8)(header >> 16);
    function->capability_pointer = (ios_u8)(capability & UINT32_C(0xfc));
    return IOS_OK;
}

ios_status x86_64_pci_enumerate_with_access(
    const struct ios_pci_config_access *access,
    struct ios_pci_function *functions,
    ios_size capacity,
    ios_size *function_count
)
{
    ios_size count = 0;
    ios_status status;

    if (IOS_FAILED(validate_access(access)) || functions == NULL || capacity == 0
        || function_count == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    for (ios_u16 bus = 0; bus <= UINT8_MAX; ++bus) {
        for (ios_u8 device = 0; device < 32; ++device) {
            struct ios_pci_function first;
            ios_u8 function_limit;

            status = read_function(access, (ios_u8)bus, device, 0, &first);
            if (status == IOS_ERROR(IOS_E_NOT_FOUND)) {
                continue;
            }
            if (IOS_FAILED(status)) {
                return status;
            }
            if (count == capacity) {
                return IOS_ERROR(IOS_E_NO_SPACE);
            }
            functions[count++] = first;
            function_limit = (first.header_type & PCI_HEADER_MULTIFUNCTION) != 0 ? 8 : 1;
            for (ios_u8 function_number = 1; function_number < function_limit;
                 ++function_number) {
                struct ios_pci_function discovered;

                status = read_function(
                    access, (ios_u8)bus, device, function_number, &discovered
                );
                if (status == IOS_ERROR(IOS_E_NOT_FOUND)) {
                    continue;
                }
                if (IOS_FAILED(status)) {
                    return status;
                }
                if (count == capacity) {
                    return IOS_ERROR(IOS_E_NO_SPACE);
                }
                functions[count++] = discovered;
            }
        }
    }
    *function_count = count;
    return IOS_OK;
}

ios_status x86_64_pci_enumerate(
    struct ios_pci_function *functions,
    ios_size capacity,
    ios_size *function_count
)
{
    return x86_64_pci_enumerate_with_access(
        x86_64_pci_default_access(), functions, capacity, function_count
    );
}

static bool power_of_two(ios_u64 value)
{
    return value != 0 && (value & (value - 1U)) == 0;
}

ios_status x86_64_pci_probe_bars(
    const struct ios_pci_config_access *access,
    struct ios_pci_function *function
)
{
    ios_u32 command_status;
    ios_u8 bar_count;
    ios_status status;

    if (IOS_FAILED(validate_access(access)) || function == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if ((function->header_type & PCI_HEADER_LAYOUT_MASK) == 0) {
        bar_count = 6;
    } else if ((function->header_type & PCI_HEADER_LAYOUT_MASK) == 1) {
        bar_count = 2;
    } else {
        return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    }
    status = x86_64_pci_config_read32(access, function, PCI_COMMAND_OFFSET, &command_status);
    if (IOS_FAILED(status)) {
        return status;
    }
    status = x86_64_pci_config_write32(
        access, function, PCI_COMMAND_OFFSET, command_status & UINT32_C(0xfffc)
    );
    if (IOS_FAILED(status)) {
        return status;
    }
    for (ios_u8 index = 0; index < IOS_PCI_BAR_COUNT; ++index) {
        function->bars[index] = (struct ios_pci_bar){ 0 };
    }
    for (ios_u8 index = 0; index < bar_count; ++index) {
        const ios_u16 offset = (ios_u16)(PCI_BAR_OFFSET + index * 4U);
        struct ios_pci_bar *bar = &function->bars[index];
        ios_u32 original_low;
        ios_u32 mask_low;

        status = x86_64_pci_config_read32(access, function, offset, &original_low);
        if (IOS_FAILED(status)) {
            break;
        }
        if ((original_low & PCI_BAR_IO) == 0
            && (original_low & PCI_BAR_MEMORY_TYPE_MASK) != PCI_BAR_MEMORY_32
            && (original_low & PCI_BAR_MEMORY_TYPE_MASK) != PCI_BAR_MEMORY_64) {
            status = IOS_ERROR(IOS_E_PROTOCOL);
            break;
        }
        if ((original_low & PCI_BAR_IO) == 0
            && (original_low & PCI_BAR_MEMORY_TYPE_MASK) == PCI_BAR_MEMORY_64) {
            ios_u32 original_high;
            ios_u32 mask_high;
            ios_u64 address_mask;
            ios_u64 size;

            if (index + 1U >= bar_count) {
                status = IOS_ERROR(IOS_E_PROTOCOL);
                break;
            }
            status = x86_64_pci_config_read32(access, function, offset + 4U, &original_high);
            if (IOS_FAILED(status)) {
                break;
            }
            status = x86_64_pci_config_write32(access, function, offset, UINT32_MAX);
            if (IOS_FAILED(status)) {
                break;
            }
            status = x86_64_pci_config_write32(
                access, function, offset + 4U, UINT32_MAX
            );
            if (IOS_FAILED(status)) {
                (void)x86_64_pci_config_write32(access, function, offset, original_low);
                break;
            }
            status = x86_64_pci_config_read32(access, function, offset, &mask_low);
            if (IOS_SUCCEEDED(status)) {
                status = x86_64_pci_config_read32(
                    access, function, offset + 4U, &mask_high
                );
            }
            (void)x86_64_pci_config_write32(access, function, offset, original_low);
            (void)x86_64_pci_config_write32(access, function, offset + 4U, original_high);
            if (IOS_FAILED(status)) {
                break;
            }
            if ((mask_low == 0 && mask_high == 0)
                || (mask_low == UINT32_MAX && mask_high == UINT32_MAX)) {
                ++index;
                continue;
            }
            address_mask = ((ios_u64)mask_high << 32) | (mask_low & ~UINT32_C(0xf));
            size = ~address_mask + 1U;
            bar->address = ((ios_u64)original_high << 32)
                | (original_low & ~UINT32_C(0xf));
            bar->byte_count = size;
            bar->memory = true;
            bar->is_64_bit = true;
            bar->prefetchable = (original_low & UINT32_C(8)) != 0;
            ++index;
        } else {
            status = x86_64_pci_config_write32(access, function, offset, UINT32_MAX);
            if (IOS_FAILED(status)) {
                break;
            }
            status = x86_64_pci_config_read32(access, function, offset, &mask_low);
            (void)x86_64_pci_config_write32(access, function, offset, original_low);
            if (IOS_FAILED(status)) {
                break;
            }
            if (mask_low == 0 || mask_low == UINT32_MAX) {
                continue;
            }
        }
        if ((original_low & PCI_BAR_IO) != 0) {
            const ios_u32 address_mask = mask_low & ~UINT32_C(3);
            const ios_u32 size = (ios_u32)(~address_mask + 1U);

            bar->address = original_low & ~UINT32_C(3);
            bar->byte_count = size;
            continue;
        }
        if (!bar->is_64_bit) {
            const ios_u32 address_mask = mask_low & ~UINT32_C(0xf);
            const ios_u32 size = (ios_u32)(~address_mask + 1U);

            bar->address = original_low & ~UINT32_C(0xf);
            bar->byte_count = size;
            bar->memory = true;
            bar->prefetchable = (original_low & UINT32_C(8)) != 0;
        }
        if (!power_of_two(bar->byte_count)
            || (bar->address & (bar->byte_count - 1U)) != 0) {
            status = IOS_ERROR(IOS_E_PROTOCOL);
            break;
        }
    }
    {
        const ios_status restore_status = x86_64_pci_config_write32(
            access, function, PCI_COMMAND_OFFSET, command_status & UINT32_C(0xffff)
        );

        if (IOS_SUCCEEDED(status) && IOS_FAILED(restore_status)) {
            status = restore_status;
        }
    }
    return status;
}
