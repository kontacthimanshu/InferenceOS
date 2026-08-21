#include <inferenceos/arch/x86_64/io.h>
#include <inferenceos/ata_pio.h>
#include <inferenceos/memory.h>

#define ATA_REG_DATA 0U
#define ATA_REG_ERROR 1U
#define ATA_REG_SECTOR_COUNT 2U
#define ATA_REG_LBA_LOW 3U
#define ATA_REG_LBA_MIDDLE 4U
#define ATA_REG_LBA_HIGH 5U
#define ATA_REG_DRIVE 6U
#define ATA_REG_STATUS 7U
#define ATA_REG_COMMAND 7U

#define ATA_CONTROL_DISABLE_INTERRUPTS UINT8_C(0x02)
#define ATA_STATUS_ERROR UINT8_C(0x01)
#define ATA_STATUS_DATA_REQUEST UINT8_C(0x08)
#define ATA_STATUS_DEVICE_FAULT UINT8_C(0x20)
#define ATA_STATUS_READY UINT8_C(0x40)
#define ATA_STATUS_BUSY UINT8_C(0x80)

#define ATA_COMMAND_READ_SECTORS UINT8_C(0x20)
#define ATA_COMMAND_WRITE_SECTORS UINT8_C(0x30)
#define ATA_COMMAND_CACHE_FLUSH UINT8_C(0xE7)
#define ATA_COMMAND_IDENTIFY UINT8_C(0xEC)

#define ATA_IDENTIFY_WORD_CAPABILITIES 49U
#define ATA_IDENTIFY_WORD_COMMAND_SETS 83U
#define ATA_IDENTIFY_WORD_LBA28_LOW 60U
#define ATA_IDENTIFY_WORD_LBA28_HIGH 61U
#define ATA_IDENTIFY_WORD_MODEL_START 27U
#define ATA_IDENTIFY_WORD_MODEL_COUNT 20U
#define ATA_IDENTIFY_LBA_SUPPORTED UINT16_C(0x0200)
#define ATA_IDENTIFY_FLUSH_SUPPORTED UINT16_C(0x1000)

static inferenceos_block_outcome ata_read(
    void *context,
    inferenceos_u64 start_lba,
    inferenceos_u32 sector_count,
    void *destination
);
static inferenceos_block_outcome ata_write(
    void *context,
    inferenceos_u64 start_lba,
    inferenceos_u32 sector_count,
    const void *source
);
static inferenceos_block_outcome ata_flush(void *context);
static inferenceos_block_outcome ata_query(
    void *context,
    inferenceos_block_info *info
);

static const inferenceos_block_operations ata_operations = {
    .read = ata_read,
    .write = ata_write,
    .flush = ata_flush,
    .query = ata_query
};

static inferenceos_u8 ata_in8(
    inferenceos_ata_pio_device *device,
    inferenceos_u16 port
)
{
    ++device->port_io_count;
    return inferenceos_arch_in8(port);
}

static inferenceos_u16 ata_in16(
    inferenceos_ata_pio_device *device,
    inferenceos_u16 port
)
{
    ++device->port_io_count;
    return inferenceos_arch_in16(port);
}

static void ata_out8(
    inferenceos_ata_pio_device *device,
    inferenceos_u16 port,
    inferenceos_u8 value
)
{
    ++device->port_io_count;
    inferenceos_arch_out8(port, value);
}

static void ata_out16(
    inferenceos_ata_pio_device *device,
    inferenceos_u16 port,
    inferenceos_u16 value
)
{
    ++device->port_io_count;
    inferenceos_arch_out16(port, value);
}

static inferenceos_u16 ata_port(
    const inferenceos_ata_pio_device *device,
    inferenceos_u16 register_offset
)
{
    return (inferenceos_u16)(device->io_base + register_offset);
}

static void ata_delay_400ns(inferenceos_ata_pio_device *device)
{
    for (inferenceos_u32 read = 0U; read < 4U; ++read) {
        (void)ata_in8(device, device->control_base);
    }
}

static inferenceos_block_outcome ata_outcome(
    inferenceos_result result,
    inferenceos_block_error error,
    inferenceos_u8 status,
    inferenceos_u8 detail,
    inferenceos_u32 sectors_completed
)
{
    const inferenceos_block_outcome outcome = {
        .result = result,
        .error = error,
        .driver_detail = ((inferenceos_u32)status << 8U) | detail,
        .sectors_completed = sectors_completed
    };
    return outcome;
}

static inferenceos_block_outcome ata_success(inferenceos_u32 sectors_completed)
{
    return ata_outcome(
        INFERENCEOS_RESULT_OK,
        INFERENCEOS_BLOCK_ERROR_NONE,
        0U,
        0U,
        sectors_completed
    );
}

static inferenceos_block_outcome ata_status_failure(
    inferenceos_ata_pio_device *device,
    inferenceos_u8 status,
    inferenceos_u32 sectors_completed
)
{
    inferenceos_u8 error = 0U;
    inferenceos_block_error block_error = INFERENCEOS_BLOCK_ERROR_CONTROLLER;

    if ((status & ATA_STATUS_ERROR) != 0U) {
        error = ata_in8(device, ata_port(device, ATA_REG_ERROR));
        block_error = INFERENCEOS_BLOCK_ERROR_DEVICE;
    }
    device->last_status = status;
    device->last_error = error;
    return ata_outcome(
        INFERENCEOS_RESULT_IO_ERROR,
        block_error,
        status,
        error,
        sectors_completed
    );
}

static inferenceos_block_outcome ata_wait(
    inferenceos_ata_pio_device *device,
    bool require_data,
    inferenceos_u32 sectors_completed
)
{
    for (inferenceos_u32 poll = 0U; poll < device->poll_limit; ++poll) {
        const inferenceos_u8 status = ata_in8(
            device, ata_port(device, ATA_REG_STATUS));
        device->last_poll_count = poll + 1U;
        device->last_status = status;
        if ((status & ATA_STATUS_BUSY) != 0U) {
            continue;
        }
        if ((status & (ATA_STATUS_ERROR | ATA_STATUS_DEVICE_FAULT)) != 0U) {
            return ata_status_failure(device, status, sectors_completed);
        }
        if ((require_data && (status & ATA_STATUS_DATA_REQUEST) != 0U)
            || (!require_data && (status & ATA_STATUS_READY) != 0U)) {
            return ata_success(sectors_completed);
        }
    }
    return ata_outcome(
        INFERENCEOS_RESULT_TIMEOUT,
        INFERENCEOS_BLOCK_ERROR_TIMEOUT,
        device->last_status,
        device->last_error,
        sectors_completed
    );
}

static void ata_select(
    inferenceos_ata_pio_device *device,
    inferenceos_u8 lba_high_nibble
)
{
    const inferenceos_u8 drive_bit = device->drive == INFERENCEOS_ATA_DRIVE_SLAVE
        ? UINT8_C(0x10) : 0U;
    ata_out8(device, ata_port(device, ATA_REG_DRIVE),
        (inferenceos_u8)(UINT8_C(0xE0) | drive_bit | lba_high_nibble));
    ata_delay_400ns(device);
}

static inferenceos_block_outcome ata_validate_request(
    inferenceos_ata_pio_device *device,
    inferenceos_u64 start_lba,
    inferenceos_u32 sector_count,
    const void *buffer
)
{
    if (device == NULL || buffer == NULL || sector_count == 0U) {
        return inferenceos_block_failure(
            INFERENCEOS_RESULT_INVALID_ARGUMENT,
            INFERENCEOS_BLOCK_ERROR_INVALID_ARGUMENT);
    }
    if (device->status == INFERENCEOS_BLOCK_STATUS_ABSENT) {
        return inferenceos_block_failure(
            INFERENCEOS_RESULT_NOT_READY, INFERENCEOS_BLOCK_ERROR_ABSENT);
    }
    if (device->status != INFERENCEOS_BLOCK_STATUS_READY) {
        return inferenceos_block_failure(
            INFERENCEOS_RESULT_NOT_READY, INFERENCEOS_BLOCK_ERROR_NOT_READY);
    }
    if (start_lba >= device->sector_count
        || (inferenceos_u64)sector_count > device->sector_count - start_lba
        || start_lba >= INFERENCEOS_ATA_PIO_LBA28_SECTOR_LIMIT
        || (inferenceos_u64)sector_count
            > INFERENCEOS_ATA_PIO_LBA28_SECTOR_LIMIT - start_lba) {
        return inferenceos_block_failure(
            INFERENCEOS_RESULT_OUT_OF_RANGE,
            INFERENCEOS_BLOCK_ERROR_OUT_OF_RANGE);
    }
    return ata_success(0U);
}

static void ata_program_request(
    inferenceos_ata_pio_device *device,
    inferenceos_u32 lba,
    inferenceos_u16 sector_count,
    inferenceos_u8 command
)
{
    ata_out8(device, ata_port(device, ATA_REG_SECTOR_COUNT),
        sector_count == 256U ? 0U : (inferenceos_u8)sector_count);
    ata_out8(device, ata_port(device, ATA_REG_LBA_LOW), (inferenceos_u8)lba);
    ata_out8(device, ata_port(device, ATA_REG_LBA_MIDDLE),
        (inferenceos_u8)(lba >> 8U));
    ata_out8(device, ata_port(device, ATA_REG_LBA_HIGH),
        (inferenceos_u8)(lba >> 16U));
    ata_out8(device, ata_port(device, ATA_REG_COMMAND), command);
}

static inferenceos_block_outcome ata_transfer(
    inferenceos_ata_pio_device *device,
    inferenceos_u64 start_lba,
    inferenceos_u32 sector_count,
    void *buffer,
    bool write
)
{
    inferenceos_block_outcome outcome = ata_validate_request(
        device, start_lba, sector_count, buffer);
    inferenceos_u8 *bytes = buffer;
    inferenceos_u32 completed = 0U;

    if (!inferenceos_block_outcome_is_success(outcome)) {
        return outcome;
    }
    while (completed < sector_count) {
        const inferenceos_u32 remaining = sector_count - completed;
        const inferenceos_u16 chunk = (inferenceos_u16)(
            remaining > 256U ? 256U : remaining);
        const inferenceos_u32 command_lba = (inferenceos_u32)(
            start_lba + completed);

        ata_select(device,
            (inferenceos_u8)((command_lba >> 24U) & 0x0FU));
        outcome = ata_wait(device, false, completed);
        if (!inferenceos_block_outcome_is_success(outcome)) {
            return outcome;
        }
        ata_program_request(
            device,
            command_lba,
            chunk,
            write ? ATA_COMMAND_WRITE_SECTORS : ATA_COMMAND_READ_SECTORS
        );
        for (inferenceos_u16 sector = 0U; sector < chunk; ++sector) {
            outcome = ata_wait(device, true, completed);
            if (!inferenceos_block_outcome_is_success(outcome)) {
                return outcome;
            }
            for (inferenceos_u16 word = 0U; word < 256U; ++word) {
                const inferenceos_size offset =
                    ((inferenceos_size)completed * INFERENCEOS_ATA_PIO_SECTOR_SIZE)
                    + ((inferenceos_size)word * 2U);
                if (write) {
                    const inferenceos_u16 value = (inferenceos_u16)(
                        (inferenceos_u16)bytes[offset]
                        | ((inferenceos_u16)bytes[offset + 1U] << 8U));
                    ata_out16(device, ata_port(device, ATA_REG_DATA), value);
                } else {
                    const inferenceos_u16 value = ata_in16(
                        device, ata_port(device, ATA_REG_DATA));
                    bytes[offset] = (inferenceos_u8)value;
                    bytes[offset + 1U] = (inferenceos_u8)(value >> 8U);
                }
            }
            ++completed;
        }
        if (write) {
            outcome = ata_wait(device, false, completed);
            if (!inferenceos_block_outcome_is_success(outcome)) {
                return outcome;
            }
        }
    }
    return ata_success(completed);
}

static inferenceos_block_outcome ata_read(
    void *context,
    inferenceos_u64 start_lba,
    inferenceos_u32 sector_count,
    void *destination
)
{
    return ata_transfer(context, start_lba, sector_count, destination, false);
}

static inferenceos_block_outcome ata_write(
    void *context,
    inferenceos_u64 start_lba,
    inferenceos_u32 sector_count,
    const void *source
)
{
    return ata_transfer(
        context, start_lba, sector_count, (void *)(inferenceos_uptr)source, true);
}

static inferenceos_block_outcome ata_flush(void *context)
{
    inferenceos_ata_pio_device *device = context;
    inferenceos_block_outcome outcome;

    if (device == NULL) {
        return inferenceos_block_failure(
            INFERENCEOS_RESULT_INVALID_ARGUMENT,
            INFERENCEOS_BLOCK_ERROR_INVALID_HANDLE);
    }
    if (device->status != INFERENCEOS_BLOCK_STATUS_READY) {
        return inferenceos_block_failure(
            INFERENCEOS_RESULT_NOT_READY, INFERENCEOS_BLOCK_ERROR_NOT_READY);
    }
    if (!device->flush_supported) {
        return inferenceos_block_failure(
            INFERENCEOS_RESULT_UNSUPPORTED,
            INFERENCEOS_BLOCK_ERROR_UNSUPPORTED_FLUSH);
    }
    ata_select(device, 0U);
    outcome = ata_wait(device, false, 0U);
    if (!inferenceos_block_outcome_is_success(outcome)) {
        return outcome;
    }
    ata_out8(device, ata_port(device, ATA_REG_COMMAND), ATA_COMMAND_CACHE_FLUSH);
    return ata_wait(device, false, 0U);
}

static inferenceos_block_outcome ata_query(
    void *context,
    inferenceos_block_info *info
)
{
    inferenceos_ata_pio_device *device = context;

    if (device == NULL || info == NULL) {
        return inferenceos_block_failure(
            INFERENCEOS_RESULT_INVALID_ARGUMENT,
            INFERENCEOS_BLOCK_ERROR_INVALID_ARGUMENT);
    }
    info->identifier = device->identifier;
    info->geometry.logical_sector_size = INFERENCEOS_ATA_PIO_SECTOR_SIZE;
    info->geometry.sector_count = device->sector_count;
    info->status = device->status;
    return ata_success(0U);
}

static void ata_decode_model(
    inferenceos_ata_pio_device *device,
    const inferenceos_u16 identify[256]
)
{
    inferenceos_size length = 0U;

    for (inferenceos_u32 index = 0U; index < ATA_IDENTIFY_WORD_MODEL_COUNT;
         ++index) {
        const inferenceos_u16 word =
            identify[ATA_IDENTIFY_WORD_MODEL_START + index];
        device->identifier[length++] = (char)(inferenceos_u8)(word >> 8U);
        device->identifier[length++] = (char)(inferenceos_u8)word;
    }
    while (length > 0U && device->identifier[length - 1U] == ' ') {
        --length;
    }
    device->identifier[length] = '\0';
    if (length == 0U) {
        static const char fallback[] = "ATA PIO disk";
        (void)memcpy(device->identifier, fallback, sizeof(fallback));
    }
}

inferenceos_block_outcome inferenceos_ata_pio_initialize(
    inferenceos_ata_pio_device *device,
    inferenceos_u16 io_base,
    inferenceos_u16 control_base,
    inferenceos_ata_drive drive,
    inferenceos_u32 poll_limit
)
{
    inferenceos_u16 identify[256];
    inferenceos_block_outcome outcome;
    inferenceos_u8 status;

    if (device == NULL || io_base == 0U || control_base == 0U
        || poll_limit == 0U
        || (drive != INFERENCEOS_ATA_DRIVE_MASTER
            && drive != INFERENCEOS_ATA_DRIVE_SLAVE)) {
        return inferenceos_block_failure(
            INFERENCEOS_RESULT_INVALID_ARGUMENT,
            INFERENCEOS_BLOCK_ERROR_INVALID_ARGUMENT);
    }
    (void)memset(device, 0, sizeof(*device));
    device->interface.operations = &ata_operations;
    device->interface.driver_context = device;
    device->io_base = io_base;
    device->control_base = control_base;
    device->drive = drive;
    device->poll_limit = poll_limit;
    device->status = INFERENCEOS_BLOCK_STATUS_BUSY;
    ata_out8(device, control_base, ATA_CONTROL_DISABLE_INTERRUPTS);
    ata_out8(device, ata_port(device, ATA_REG_DRIVE),
        device->drive == INFERENCEOS_ATA_DRIVE_SLAVE
            ? UINT8_C(0xB0) : UINT8_C(0xA0));
    ata_delay_400ns(device);
    ata_out8(device, ata_port(device, ATA_REG_SECTOR_COUNT), 0U);
    ata_out8(device, ata_port(device, ATA_REG_LBA_LOW), 0U);
    ata_out8(device, ata_port(device, ATA_REG_LBA_MIDDLE), 0U);
    ata_out8(device, ata_port(device, ATA_REG_LBA_HIGH), 0U);
    ata_out8(device, ata_port(device, ATA_REG_COMMAND), ATA_COMMAND_IDENTIFY);
    status = ata_in8(device, ata_port(device, ATA_REG_STATUS));
    if (status == 0U) {
        device->status = INFERENCEOS_BLOCK_STATUS_ABSENT;
        return ata_outcome(
            INFERENCEOS_RESULT_NOT_READY,
            INFERENCEOS_BLOCK_ERROR_ABSENT,
            status,
            0U,
            0U);
    }
    outcome = ata_wait(device, false, 0U);
    if (!inferenceos_block_outcome_is_success(outcome)) {
        device->status = INFERENCEOS_BLOCK_STATUS_FAILED;
        return outcome;
    }
    if (ata_in8(device, ata_port(device, ATA_REG_LBA_MIDDLE)) != 0U
        || ata_in8(device, ata_port(device, ATA_REG_LBA_HIGH)) != 0U) {
        device->status = INFERENCEOS_BLOCK_STATUS_FAILED;
        return inferenceos_block_failure(
            INFERENCEOS_RESULT_UNSUPPORTED,
            INFERENCEOS_BLOCK_ERROR_CONTROLLER);
    }
    outcome = ata_wait(device, true, 0U);
    if (!inferenceos_block_outcome_is_success(outcome)) {
        device->status = INFERENCEOS_BLOCK_STATUS_FAILED;
        return outcome;
    }
    for (inferenceos_u32 word = 0U; word < 256U; ++word) {
        identify[word] = ata_in16(device, ata_port(device, ATA_REG_DATA));
    }
    device->lba28_supported =
        (identify[ATA_IDENTIFY_WORD_CAPABILITIES] & ATA_IDENTIFY_LBA_SUPPORTED)
        != 0U;
    device->flush_supported =
        (identify[ATA_IDENTIFY_WORD_COMMAND_SETS] & ATA_IDENTIFY_FLUSH_SUPPORTED)
        != 0U;
    device->sector_count =
        (inferenceos_u64)identify[ATA_IDENTIFY_WORD_LBA28_LOW]
        | ((inferenceos_u64)identify[ATA_IDENTIFY_WORD_LBA28_HIGH] << 16U);
    if (!device->lba28_supported || device->sector_count == 0U
        || device->sector_count > INFERENCEOS_ATA_PIO_LBA28_SECTOR_LIMIT) {
        device->status = INFERENCEOS_BLOCK_STATUS_FAILED;
        return inferenceos_block_failure(
            INFERENCEOS_RESULT_UNSUPPORTED,
            INFERENCEOS_BLOCK_ERROR_DEVICE);
    }
    ata_decode_model(device, identify);
    device->status = INFERENCEOS_BLOCK_STATUS_READY;
    return ata_success(0U);
}

const inferenceos_block_device *inferenceos_ata_pio_interface(
    inferenceos_ata_pio_device *device
)
{
    return device == NULL ? NULL : &device->interface;
}
