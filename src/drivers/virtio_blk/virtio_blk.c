#include <inferenceos/drivers/virtio_blk.h>

#include <inferenceos/runtime.h>

static bool is_supported_function(const struct ios_pci_function *function)
{
    return function->vendor_id == IOS_VIRTIO_PCI_VENDOR_ID
        && function->device_id == IOS_VIRTIO_BLK_MODERN_DEVICE_ID
        && function->has_common_configuration
        && function->has_notify_configuration
        && function->has_isr_configuration
        && function->has_device_configuration;
}

static ios_status validate_transport(const struct ios_virtio_blk_transport *transport)
{
    if (transport == NULL || transport->reset == NULL || transport->read_features == NULL
        || transport->write_features == NULL || transport->setup_queue == NULL
        || transport->set_driver_ok == NULL || transport->read_capacity == NULL
        || transport->publish == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    return IOS_OK;
}

ios_status virtio_blk_initialize(
    struct ios_virtio_blk_device *device,
    const struct ios_pci_function *functions,
    ios_size function_count,
    const struct ios_virtio_blk_transport *transport,
    struct ios_virtio_blk_request *request_storage,
    ios_u16 queue_capacity
)
{
    const ios_u64 required = IOS_VIRTIO_F_VERSION_1 | IOS_VIRTIO_BLK_F_FLUSH;
    const struct ios_pci_function *selected = NULL;
    ios_u64 offered;
    ios_status status;
    if (device == NULL || functions == NULL || function_count == 0 || request_storage == NULL
        || queue_capacity == 0 || queue_capacity > IOS_VIRTIO_BLK_QUEUE_LIMIT) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    status = validate_transport(transport);
    if (IOS_FAILED(status)) return status;
    for (ios_size index = 0; index < function_count; ++index) {
        if (is_supported_function(&functions[index])) {
            selected = &functions[index];
            break;
        }
    }
    if (selected == NULL) return IOS_ERROR(IOS_E_NOT_FOUND);

    memset(device, 0, sizeof(*device));
    memset(request_storage, 0, (ios_size)queue_capacity * sizeof(*request_storage));
    device->pci = *selected;
    device->transport = *transport;
    device->requests = request_storage;
    device->queue_capacity = queue_capacity;
    status = transport->reset(transport->context, selected);
    if (IOS_FAILED(status)) return status;
    status = transport->read_features(transport->context, &offered);
    if (IOS_FAILED(status)) return status;
    if ((offered & required) != required) return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    status = transport->write_features(transport->context, required);
    if (IOS_FAILED(status)) return status;
    status = transport->setup_queue(transport->context, 0, queue_capacity);
    if (IOS_FAILED(status)) return status;
    status = transport->read_capacity(transport->context, &device->sector_count);
    if (IOS_FAILED(status)) return status;
    if (device->sector_count == 0) return IOS_ERROR(IOS_E_OUT_OF_RANGE);
    status = transport->set_driver_ok(transport->context);
    if (IOS_FAILED(status)) return status;
    device->accepted_features = required;
    device->ready = true;
    return IOS_OK;
}

ios_status virtio_blk_submit(
    struct ios_virtio_blk_device *device,
    ios_u32 type,
    ios_u64 sector,
    void *buffer,
    ios_size byte_count,
    ios_u32 generation,
    ios_u16 *request_id
)
{
    struct ios_virtio_blk_request *request;
    ios_status status;
    if (device == NULL || !device->ready || request_id == NULL) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    if (type != IOS_VIRTIO_BLK_REQUEST_READ && type != IOS_VIRTIO_BLK_REQUEST_WRITE
        && type != IOS_VIRTIO_BLK_REQUEST_FLUSH) {
        return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    }
    if (type == IOS_VIRTIO_BLK_REQUEST_FLUSH) {
        if (buffer != NULL || byte_count != 0) return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    } else if (buffer == NULL || byte_count != IOS_VIRTIO_BLK_SECTOR_SIZE
               || sector >= device->sector_count) {
        return IOS_ERROR(IOS_E_OUT_OF_RANGE);
    }
    for (ios_u16 index = 0; index < device->queue_capacity; ++index) {
        if (device->requests[index].state != IOS_VIRTIO_BLK_REQUEST_FREE) continue;
        request = &device->requests[index];
        *request = (struct ios_virtio_blk_request){
            IOS_VIRTIO_BLK_REQUEST_AVAILABLE, type, sector, buffer, byte_count, generation,
            IOS_ERROR(IOS_E_WOULD_BLOCK)
        };
        status = device->transport.publish(device->transport.context, index, request);
        if (IOS_FAILED(status)) {
            memset(request, 0, sizeof(*request));
            return status;
        }
        ++device->outstanding;
        *request_id = index;
        return IOS_OK;
    }
    return IOS_ERROR(IOS_E_WOULD_BLOCK);
}

ios_status virtio_blk_complete(
    struct ios_virtio_blk_device *device, ios_u16 request_id, ios_u8 device_status
)
{
    struct ios_virtio_blk_request *request;
    if (device == NULL || request_id >= device->queue_capacity) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    request = &device->requests[request_id];
    if (request->state != IOS_VIRTIO_BLK_REQUEST_AVAILABLE) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    if (device_status == IOS_VIRTIO_BLK_STATUS_OK) request->result = IOS_OK;
    else if (device_status == IOS_VIRTIO_BLK_STATUS_UNSUPPORTED) {
        request->result = IOS_ERROR(IOS_E_NOT_SUPPORTED);
    } else request->result = IOS_ERROR(IOS_E_IO);
    request->state = IOS_VIRTIO_BLK_REQUEST_USED;
    return IOS_OK;
}

ios_status virtio_blk_poll(
    struct ios_virtio_blk_device *device, ios_u16 request_id, ios_u32 *generation
)
{
    struct ios_virtio_blk_request *request;
    ios_status result;
    if (device == NULL || request_id >= device->queue_capacity) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    request = &device->requests[request_id];
    if (request->state == IOS_VIRTIO_BLK_REQUEST_AVAILABLE) {
        if (device->transport.service != NULL) {
            result = device->transport.service(device->transport.context, device);
            if (IOS_FAILED(result)) {
                return result;
            }
        }
        if (request->state == IOS_VIRTIO_BLK_REQUEST_AVAILABLE) {
            return IOS_ERROR(IOS_E_WOULD_BLOCK);
        }
    }
    if (request->state != IOS_VIRTIO_BLK_REQUEST_USED) return IOS_ERROR(IOS_E_INVALID_STATE);
    result = request->result;
    if (generation != NULL) *generation = request->generation;
    memset(request, 0, sizeof(*request));
    --device->outstanding;
    return result;
}

void virtio_blk_fail(struct ios_virtio_blk_device *device, ios_status status)
{
    if (device == NULL || IOS_SUCCEEDED(status)) {
        return;
    }
    for (ios_u16 index = 0; index < device->queue_capacity; ++index) {
        if (device->requests[index].state == IOS_VIRTIO_BLK_REQUEST_AVAILABLE) {
            device->requests[index].result = status;
            device->requests[index].state = IOS_VIRTIO_BLK_REQUEST_USED;
        }
    }
    device->ready = false;
}

ios_u64 virtio_blk_capacity_bytes(const struct ios_virtio_blk_device *device)
{
    if (device == NULL || device->sector_count > UINT64_MAX / IOS_VIRTIO_BLK_SECTOR_SIZE) return 0;
    return device->sector_count * IOS_VIRTIO_BLK_SECTOR_SIZE;
}
