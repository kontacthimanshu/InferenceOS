#include <inferenceos/drivers/hyperv/storvsc.h>

#include <inferenceos/arch/io.h>
#include <inferenceos/runtime.h>

enum {
    VSTOR_FLAG_REQUEST_COMPLETION = 1,
    SCSI_OPERATION_INQUIRY = 0x12,
    SCSI_OPERATION_READ_CAPACITY_10 = 0x25,
    SCSI_OPERATION_READ_10 = 0x28,
    SCSI_OPERATION_WRITE_10 = 0x2a,
    SCSI_OPERATION_SYNCHRONIZE_CACHE_10 = 0x35,
    SCSI_OPERATION_SERVICE_ACTION_IN_16 = 0x9e,
    SCSI_SERVICE_ACTION_READ_CAPACITY_16 = 0x10,
    SRB_FLAGS_DATA_IN = 0x00000040,
    SRB_FLAGS_DATA_OUT = 0x00000080,
    SRB_FLAGS_NO_QUEUE_FREEZE = 0x00000100
};

static ios_u32 read_be32(const ios_u8 *bytes)
{
    return ((ios_u32)bytes[0] << 24) | ((ios_u32)bytes[1] << 16)
        | ((ios_u32)bytes[2] << 8) | bytes[3];
}

static ios_u64 read_be64(const ios_u8 *bytes)
{
    return ((ios_u64)read_be32(bytes) << 32) | read_be32(bytes + 4);
}

static void write_be16(ios_u8 *bytes, ios_u16 value)
{
    bytes[0] = (ios_u8)(value >> 8);
    bytes[1] = (ios_u8)value;
}

static void write_be32(ios_u8 *bytes, ios_u32 value)
{
    bytes[0] = (ios_u8)(value >> 24);
    bytes[1] = (ios_u8)(value >> 16);
    bytes[2] = (ios_u8)(value >> 8);
    bytes[3] = (ios_u8)value;
}

ios_status storvsc_build_rw10_cdb(
    ios_u8 cdb[16], bool write, ios_u64 first_sector, ios_u16 sector_count
)
{
    if (cdb == NULL || first_sector > UINT32_MAX || sector_count == 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    memset(cdb, 0, 16);
    cdb[0] = write ? SCSI_OPERATION_WRITE_10 : SCSI_OPERATION_READ_10;
    write_be32(cdb + 2, (ios_u32)first_sector);
    write_be16(cdb + 7, sector_count);
    return IOS_OK;
}

ios_status storvsc_parse_capacity10(
    const ios_u8 response[8], ios_u64 *sector_count, ios_u32 *logical_sector_size
)
{
    ios_u32 last_lba;
    ios_u32 block_size;

    if (response == NULL || sector_count == NULL || logical_sector_size == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    last_lba = read_be32(response);
    block_size = read_be32(response + 4);
    if (last_lba == UINT32_MAX) {
        return IOS_ERROR(IOS_E_OUT_OF_RANGE);
    }
    if (block_size == 0) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    *sector_count = (ios_u64)last_lba + 1U;
    *logical_sector_size = block_size;
    return IOS_OK;
}

static ios_status wait_completion(
    struct ios_storvsc *driver, ios_u64 expected_transaction,
    struct ios_vstor_packet *response
)
{
    for (ios_u32 spin = 0; spin < driver->spin_limit; ++spin) {
        ios_u16 packet_type;
        ios_u64 transaction_id;
        ios_size size;
        ios_status status = vmbus_channel_read(driver->channel, &packet_type,
            &transaction_id, response, sizeof(*response), &size);

        if (status == IOS_ERROR(IOS_E_WOULD_BLOCK)) {
            x86_64_cpu_relax();
            continue;
        }
        if (IOS_FAILED(status)) {
            return status;
        }
        if (packet_type != IOS_HV_PACKET_TYPE_COMPLETION
            || transaction_id != expected_transaction || size != sizeof(*response)
            || response->operation != IOS_VSTOR_OPERATION_COMPLETE_IO) {
            return IOS_ERROR(IOS_E_PROTOCOL);
        }
        if (response->status != 0) {
            return IOS_ERROR(IOS_E_IO);
        }
        return IOS_OK;
    }
    driver->ready = false;
    driver->last_stage = "timeout";
    return IOS_ERROR(IOS_E_TIMEOUT);
}

static ios_status transact_inband(
    struct ios_storvsc *driver, struct ios_vstor_packet *packet
)
{
    struct ios_vstor_packet response;
    const ios_u64 transaction = driver->next_transaction_id++;
    ios_status status;

    packet->flags = VSTOR_FLAG_REQUEST_COMPLETION;
    status = vmbus_channel_write(driver->bus, driver->channel,
        IOS_HV_PACKET_TYPE_DATA_INBAND, IOS_HV_PACKET_FLAG_COMPLETION_REQUESTED,
        transaction, packet, sizeof(*packet));
    if (IOS_FAILED(status)) {
        return status;
    }
    status = wait_completion(driver, transaction, &response);
    if (IOS_SUCCEEDED(status)) {
        *packet = response;
    }
    return status;
}

static ios_status execute_scsi(
    struct ios_storvsc *driver, const ios_u8 cdb[16], ios_u8 cdb_length,
    ios_u8 direction, ios_size transfer_size
)
{
    struct ios_vstor_packet packet;
    struct ios_vstor_packet response;
    const ios_u64 transaction = driver->next_transaction_id++;
    ios_status status;

    if (cdb == NULL || cdb_length == 0 || cdb_length > 16
        || transfer_size > driver->bounce_size || transfer_size > UINT32_MAX) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    memset(&packet, 0, sizeof(packet));
    packet.operation = IOS_VSTOR_OPERATION_EXECUTE_SRB;
    packet.flags = VSTOR_FLAG_REQUEST_COMPLETION;
    packet.payload.srb.length = sizeof(packet.payload.srb);
    packet.payload.srb.lun = driver->lun;
    packet.payload.srb.cdb_length = cdb_length;
    packet.payload.srb.sense_info_length = sizeof(packet.payload.srb.request.sense_data);
    packet.payload.srb.data_in = direction;
    packet.payload.srb.transfer_length = (ios_u32)transfer_size;
    packet.payload.srb.srb_flags = SRB_FLAGS_NO_QUEUE_FREEZE;
    if (direction == IOS_STORVSC_DATA_READ) {
        packet.payload.srb.srb_flags |= SRB_FLAGS_DATA_IN;
    } else if (direction == IOS_STORVSC_DATA_WRITE) {
        packet.payload.srb.srb_flags |= SRB_FLAGS_DATA_OUT;
    }
    memcpy(packet.payload.srb.request.cdb, cdb, cdb_length);
    if (transfer_size == 0) {
        status = vmbus_channel_write(driver->bus, driver->channel,
            IOS_HV_PACKET_TYPE_DATA_INBAND, IOS_HV_PACKET_FLAG_COMPLETION_REQUESTED,
            transaction, &packet, sizeof(packet));
    } else {
        status = vmbus_channel_write_gpa_direct(driver->bus, driver->channel,
            IOS_HV_PACKET_FLAG_COMPLETION_REQUESTED, transaction,
            &packet, sizeof(packet), driver->bounce_physical, transfer_size);
    }
    if (IOS_FAILED(status)) {
        return status;
    }
    status = wait_completion(driver, transaction, &response);
    if (IOS_FAILED(status)) {
        return status;
    }
    if (response.payload.srb.srb_status != IOS_STORVSC_SRB_STATUS_SUCCESS
        || response.payload.srb.scsi_status != IOS_STORVSC_SCSI_STATUS_GOOD
        || response.payload.srb.transfer_length != transfer_size) {
        return IOS_ERROR(IOS_E_IO);
    }
    return IOS_OK;
}

static ios_status negotiate_protocol(struct ios_storvsc *driver)
{
    static const ios_u16 versions[] = {
        IOS_VSTOR_PROTOCOL_VERSION_6_2,
        IOS_VSTOR_PROTOCOL_VERSION_6_0,
        IOS_VSTOR_PROTOCOL_VERSION_5_1
    };
    struct ios_vstor_packet packet;
    ios_status status;

    memset(&packet, 0, sizeof(packet));
    packet.operation = IOS_VSTOR_OPERATION_BEGIN_INITIALIZATION;
    status = transact_inband(driver, &packet);
    if (IOS_FAILED(status)) {
        return status;
    }
    for (ios_size index = 0; index < IOS_ARRAY_COUNT(versions); ++index) {
        memset(&packet, 0, sizeof(packet));
        packet.operation = IOS_VSTOR_OPERATION_QUERY_PROTOCOL_VERSION;
        packet.payload.version.major_minor = versions[index];
        status = transact_inband(driver, &packet);
        if (IOS_SUCCEEDED(status)) {
            driver->protocol_version = versions[index];
            break;
        }
    }
    if (driver->protocol_version == 0) {
        return IOS_ERROR(IOS_E_UNSUPPORTED_VERSION);
    }
    memset(&packet, 0, sizeof(packet));
    packet.operation = IOS_VSTOR_OPERATION_QUERY_PROPERTIES;
    status = transact_inband(driver, &packet);
    if (IOS_FAILED(status)) {
        return status;
    }
    memset(&packet, 0, sizeof(packet));
    packet.operation = IOS_VSTOR_OPERATION_END_INITIALIZATION;
    return transact_inband(driver, &packet);
}

static ios_status probe_disk(struct ios_storvsc *driver)
{
    ios_u8 cdb[16] = {0};
    ios_u64 sectors;
    ios_u32 block_size;
    ios_status status;

    memset(driver->bounce, 0, driver->bounce_size);
    cdb[0] = SCSI_OPERATION_INQUIRY;
    cdb[4] = 36;
    status = execute_scsi(driver, cdb, 6, IOS_STORVSC_DATA_READ, 36);
    if (IOS_FAILED(status)) {
        return status;
    }
    if ((((ios_u8 *)driver->bounce)[0] & 0xe0U) != 0
        || (((ios_u8 *)driver->bounce)[0] & 0x1fU) != 0) {
        return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    }

    memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_OPERATION_READ_CAPACITY_10;
    status = execute_scsi(driver, cdb, 10, IOS_STORVSC_DATA_READ, 8);
    if (IOS_FAILED(status)) {
        return status;
    }
    status = storvsc_parse_capacity10(driver->bounce, &sectors, &block_size);
    if (status == IOS_ERROR(IOS_E_OUT_OF_RANGE)) {
        memset(cdb, 0, sizeof(cdb));
        cdb[0] = SCSI_OPERATION_SERVICE_ACTION_IN_16;
        cdb[1] = SCSI_SERVICE_ACTION_READ_CAPACITY_16;
        cdb[13] = 32;
        status = execute_scsi(driver, cdb, 16, IOS_STORVSC_DATA_READ, 32);
        if (IOS_FAILED(status)) {
            return status;
        }
        sectors = read_be64(driver->bounce);
        block_size = read_be32((ios_u8 *)driver->bounce + 8);
        if (sectors == UINT64_MAX || block_size == 0) {
            return IOS_ERROR(IOS_E_PROTOCOL);
        }
        ++sectors;
    } else if (IOS_FAILED(status)) {
        return status;
    }
    if (block_size != IOS_BLOCK_SECTOR_SIZE) {
        return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    }
    driver->sector_count = sectors;
    return IOS_OK;
}

static ios_status storage_transfer(
    struct ios_storvsc *driver, bool write, ios_u64 first_sector,
    ios_size sector_count, void *buffer
)
{
    ios_u8 *bytes = buffer;
    ios_size remaining = sector_count;

    if (driver == NULL || !driver->ready || buffer == NULL) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    while (remaining != 0) {
        ios_u8 cdb[16];
        ios_size batch = driver->bounce_size / IOS_BLOCK_SECTOR_SIZE;
        ios_size byte_count;
        ios_status status;

        if (batch > UINT16_MAX) {
            batch = UINT16_MAX;
        }
        if (batch > remaining) {
            batch = remaining;
        }
        if (first_sector > UINT32_MAX || batch > UINT32_MAX - first_sector + 1U) {
            return IOS_ERROR(IOS_E_OUT_OF_RANGE);
        }
        byte_count = batch * IOS_BLOCK_SECTOR_SIZE;
        status = storvsc_build_rw10_cdb(cdb, write, first_sector, (ios_u16)batch);
        if (IOS_FAILED(status)) {
            return status;
        }
        if (write) {
            memcpy(driver->bounce, bytes, byte_count);
        }
        status = execute_scsi(driver, cdb, 10,
            write ? IOS_STORVSC_DATA_WRITE : IOS_STORVSC_DATA_READ, byte_count);
        if (IOS_FAILED(status)) {
            return status;
        }
        if (!write) {
            memcpy(bytes, driver->bounce, byte_count);
        }
        bytes += byte_count;
        first_sector += batch;
        remaining -= batch;
    }
    return IOS_OK;
}

static ios_status storvsc_read(
    void *context, ios_u64 first_sector, ios_size sector_count, void *buffer
)
{
    return storage_transfer(context, false, first_sector, sector_count, buffer);
}

static ios_status storvsc_write(
    void *context, ios_u64 first_sector, ios_size sector_count, const void *buffer
)
{
    return storage_transfer(context, true, first_sector, sector_count, (void *)buffer);
}

static ios_status storvsc_flush(void *context)
{
    struct ios_storvsc *driver = context;
    ios_u8 cdb[16] = {0};

    if (driver == NULL || !driver->ready) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    cdb[0] = SCSI_OPERATION_SYNCHRONIZE_CACHE_10;
    return execute_scsi(driver, cdb, 10, IOS_STORVSC_DATA_UNKNOWN, 0);
}

ios_status storvsc_initialize(
    struct ios_storvsc *driver,
    struct ios_vmbus *bus,
    struct ios_vmbus_channel *channel,
    void *bounce,
    ios_uptr bounce_physical,
    ios_size bounce_size,
    ios_u8 lun,
    ios_u32 spin_limit
)
{
    ios_status status;

    if (driver == NULL || bus == NULL || channel == NULL || !channel->open
        || bounce == NULL || bounce_size < IOS_BLOCK_SECTOR_SIZE
        || bounce_size > IOS_STORVSC_MAX_TRANSFER_BYTES || spin_limit == 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    memset(driver, 0, sizeof(*driver));
    driver->bus = bus;
    driver->channel = channel;
    driver->bounce = bounce;
    driver->bounce_physical = bounce_physical;
    driver->bounce_size = bounce_size;
    driver->lun = lun;
    driver->spin_limit = spin_limit;
    driver->next_transaction_id = 1;
    driver->last_stage = "protocol";
    status = negotiate_protocol(driver);
    if (IOS_FAILED(status)) {
        return status;
    }
    return storvsc_select_lun(driver, lun);
}

ios_status storvsc_select_lun(struct ios_storvsc *driver, ios_u8 lun)
{
    ios_status status;

    if (driver == NULL || driver->protocol_version == 0) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    driver->ready = false;
    driver->lun = lun;
    driver->last_stage = "inquiry";
    status = probe_disk(driver);
    if (IOS_FAILED(status)) return status;
    driver->ready = true;
    driver->last_stage = "ready";
    return IOS_OK;
}

ios_status storvsc_publish(struct ios_storvsc *driver, struct ios_block_device *device)
{
    static const struct ios_block_device_operations operations = {
        .read = storvsc_read,
        .write = storvsc_write,
        .flush = storvsc_flush
    };

    if (driver == NULL || device == NULL || !driver->ready) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    return block_device_initialize(device, driver, &operations,
        IOS_BLOCK_SECTOR_SIZE, driver->sector_count, IOS_BLOCK_DEVICE_READY);
}

const char *storvsc_last_stage(const struct ios_storvsc *driver)
{
    return driver == NULL || driver->last_stage == NULL ? "uninitialized" : driver->last_stage;
}
