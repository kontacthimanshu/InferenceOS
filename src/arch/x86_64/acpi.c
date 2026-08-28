#include <inferenceos/arch/platform.h>

struct IOS_PACKED acpi_rsdp {
    char signature[8];
    ios_u8 checksum;
    char oem_id[6];
    ios_u8 revision;
    ios_u32 rsdt_address;
    ios_u32 length;
    ios_u64 xsdt_address;
    ios_u8 extended_checksum;
    ios_u8 reserved[3];
};

struct IOS_PACKED acpi_header {
    char signature[4];
    ios_u32 length;
    ios_u8 revision;
    ios_u8 checksum;
    char oem_id[6];
    char oem_table_id[8];
    ios_u32 oem_revision;
    ios_u32 creator_id;
    ios_u32 creator_revision;
};

struct IOS_PACKED acpi_madt {
    struct acpi_header header;
    ios_u32 local_apic_address;
    ios_u32 flags;
    ios_u8 entries[];
};

struct IOS_PACKED acpi_madt_entry {
    ios_u8 type;
    ios_u8 length;
};

struct IOS_PACKED acpi_madt_local_apic {
    struct acpi_madt_entry header;
    ios_u8 processor_id;
    ios_u8 apic_id;
    ios_u32 flags;
};

struct IOS_PACKED acpi_fadt_prefix {
    struct acpi_header header;
    ios_u32 firmware_control;
    ios_u32 dsdt;
    ios_u8 reserved0;
    ios_u8 preferred_profile;
    ios_u16 sci_interrupt;
    ios_u32 smi_command_port;
    ios_u8 acpi_enable;
    ios_u8 acpi_disable;
    ios_u8 s4bios_request;
    ios_u8 pstate_control;
    ios_u32 pm1a_event_block;
    ios_u32 pm1b_event_block;
    ios_u32 pm1a_control_block;
    ios_u32 pm1b_control_block;
    ios_u32 pm2_control_block;
    ios_u32 pm_timer_block;
    ios_u32 gpe0_block;
    ios_u32 gpe1_block;
    ios_u8 pm1_event_length;
    ios_u8 pm1_control_length;
    ios_u8 pm2_control_length;
    ios_u8 pm_timer_length;
};

static bool checksum_is_valid(const void *data, ios_size length)
{
    const ios_u8 *bytes = data;
    ios_u8 sum = 0;
    for (ios_size index = 0; index < length; ++index) {
        sum = (ios_u8)(sum + bytes[index]);
    }
    return sum == 0;
}

static bool bytes_equal(const void *left_pointer, const void *right_pointer, ios_size length)
{
    const ios_u8 *left = left_pointer;
    const ios_u8 *right = right_pointer;
    for (ios_size index = 0; index < length; ++index) {
        if (left[index] != right[index]) {
            return false;
        }
    }
    return true;
}

static ios_uptr read_root_address(const ios_u8 *entry, ios_size entry_size)
{
    ios_uptr address = 0;
    for (ios_size index = 0; index < entry_size; ++index) {
        address |= (ios_uptr)entry[index] << (index * 8U);
    }
    return address;
}

static ios_u32 read_little_endian_u32(const ios_u8 *bytes)
{
    return (ios_u32)*bytes
        | ((ios_u32)bytes[1] << 8)
        | ((ios_u32)bytes[2] << 16)
        | ((ios_u32)bytes[3] << 24);
}

static bool table_is_valid(const struct acpi_header *header)
{
    return header != NULL && header->length >= sizeof(*header)
        && checksum_is_valid(header, header->length);
}

static ios_status parse_madt(
    const struct acpi_madt *madt,
    struct x86_64_platform_info *platform
)
{
    const ios_u8 *cursor;
    const ios_u8 *end;

    if (!table_is_valid(&madt->header) || madt->header.length < sizeof(*madt)) {
        return IOS_ERROR(IOS_E_CORRUPT);
    }
    platform->local_apic_address = madt->local_apic_address;
    cursor = madt->entries;
    end = (const ios_u8 *)madt + madt->header.length;
    while (cursor < end) {
        const struct acpi_madt_entry *entry = (const void *)cursor;
        if ((ios_size)(end - cursor) < sizeof(*entry) || entry->length < sizeof(*entry)
            || entry->length > (ios_size)(end - cursor)) {
            return IOS_ERROR(IOS_E_CORRUPT);
        }
        if (entry->type == 0 && entry->length >= sizeof(struct acpi_madt_local_apic)) {
            const struct acpi_madt_local_apic *local = (const void *)entry;
            if ((local->flags & UINT32_C(1)) != 0) {
                if (platform->enabled_processor_count == 0) {
                    platform->bootstrap_apic_id = local->apic_id;
                }
                ++platform->enabled_processor_count;
            }
        }
        cursor += entry->length;
    }
    return platform->enabled_processor_count == 1 && platform->local_apic_address != 0
        ? IOS_OK : IOS_ERROR(IOS_E_NOT_SUPPORTED);
}

static ios_status parse_fadt(
    const struct acpi_fadt_prefix *fadt,
    struct x86_64_platform_info *platform
)
{
    const ios_u32 timer_32_bit_flag = UINT32_C(1) << 8;
    ios_u32 flags;

    if (!table_is_valid(&fadt->header) || fadt->header.length < 116) {
        return IOS_ERROR(IOS_E_CORRUPT);
    }
    if (fadt->pm_timer_block == 0 || fadt->pm_timer_length == 0) {
        return IOS_ERROR(IOS_E_NOT_FOUND);
    }
    if (fadt->pm_timer_block > UINT16_MAX
        || fadt->pm_timer_length < sizeof(ios_u32)) {
        return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    }
    flags = read_little_endian_u32((const ios_u8 *)fadt + 112);
    platform->pm_timer_port = (ios_u16)fadt->pm_timer_block;
    platform->pm_timer_width = (flags & timer_32_bit_flag) != 0 ? 32 : 24;
    return IOS_OK;
}

ios_status x86_64_acpi_discover(
    const void *root_system_description_pointer,
    struct x86_64_platform_info *platform
)
{
    const struct acpi_rsdp *rsdp = root_system_description_pointer;
    const struct acpi_header *root;
    const ios_u8 *entries;
    ios_size entry_size;
    ios_size entry_count;
    bool found_madt = false;
    bool found_fadt = false;

    if (rsdp == NULL || platform == NULL || !bytes_equal(rsdp->signature, "RSD PTR ", 8)
        || !checksum_is_valid(rsdp, 20)) {
        return IOS_ERROR(IOS_E_CORRUPT);
    }
    *platform = (struct x86_64_platform_info){ 0 };
    if (rsdp->revision >= 2) {
        if (rsdp->length < sizeof(*rsdp) || !checksum_is_valid(rsdp, rsdp->length)
            || rsdp->xsdt_address == 0) {
            return IOS_ERROR(IOS_E_CORRUPT);
        }
        root = (const void *)(ios_uptr)rsdp->xsdt_address;
        entry_size = sizeof(ios_u64);
    } else {
        root = (const void *)(ios_uptr)rsdp->rsdt_address;
        entry_size = sizeof(ios_u32);
    }
    if (!table_is_valid(root) || root->length < sizeof(*root)
        || (root->length - sizeof(*root)) % entry_size != 0) {
        return IOS_ERROR(IOS_E_CORRUPT);
    }
    entries = (const ios_u8 *)root + sizeof(*root);
    entry_count = (root->length - sizeof(*root)) / entry_size;
    for (ios_size index = 0; index < entry_count; ++index) {
        ios_uptr address;
        const struct acpi_header *table;
        address = read_root_address(entries + index * entry_size, entry_size);
        table = (const void *)address;
        if (!table_is_valid(table)) {
            return IOS_ERROR(IOS_E_CORRUPT);
        }
        if (bytes_equal(table->signature, "APIC", 4)) {
            if (IOS_FAILED(parse_madt((const void *)table, platform))) {
                return IOS_ERROR(IOS_E_NOT_SUPPORTED);
            }
            found_madt = true;
        } else if (bytes_equal(table->signature, "FACP", 4)) {
            const ios_status fadt_status = parse_fadt((const void *)table, platform);
            if (fadt_status == IOS_ERROR(IOS_E_NOT_FOUND)) {
                continue;
            }
            if (IOS_FAILED(fadt_status)) {
                return fadt_status;
            }
            found_fadt = true;
        }
    }
    /*
     * A MADT is required to initialize the local APIC.  The FADT PM timer is
     * optional because Hyper-V Generation 2 supplies its architectural
     * partition reference counter instead of a legacy ACPI PM timer.
     * Timer initialization rejects the platform later unless one of those
     * bounded calibration clocks is available.
     */
    (void)found_fadt;
    return found_madt ? IOS_OK : IOS_ERROR(IOS_E_NOT_FOUND);
}
