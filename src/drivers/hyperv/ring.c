#include <inferenceos/drivers/hyperv/ring.h>

#include <inferenceos/arch/io.h>
#include <inferenceos/runtime.h>

static ios_u32 ring_used(ios_u32 write_index, ios_u32 read_index, ios_u32 size)
{
    return write_index >= read_index
        ? write_index - read_index
        : size - (read_index - write_index);
}

static void ring_copy_in(struct ios_hv_ring *ring, ios_u32 index, const void *source, ios_size size)
{
    const ios_u8 *bytes = source;
    ios_size first = size;

    if (first > ring->data_size - index) {
        first = ring->data_size - index;
    }
    memcpy(ring->data + index, bytes, first);
    if (size > first) {
        memcpy(ring->data, bytes + first, size - first);
    }
}

static void ring_copy_out(
    const struct ios_hv_ring *ring, ios_u32 index, void *destination, ios_size size
)
{
    ios_u8 *bytes = destination;
    ios_size first = size;

    if (first > ring->data_size - index) {
        first = ring->data_size - index;
    }
    memcpy(bytes, ring->data + index, first);
    if (size > first) {
        memcpy(bytes + first, ring->data, size - first);
    }
}

static ios_u32 ring_advance(const struct ios_hv_ring *ring, ios_u32 index, ios_u32 amount)
{
    return (index + amount) % ring->data_size;
}

ios_status hyperv_ring_initialize(struct ios_hv_ring *ring, void *memory, ios_size byte_count)
{
    if (ring == NULL || memory == NULL || ((ios_uptr)memory & (IOS_HV_PAGE_SIZE - 1U)) != 0
        || byte_count < IOS_HV_PAGE_SIZE + 64U || byte_count > UINT32_MAX
        || ((byte_count - IOS_HV_PAGE_SIZE) % IOS_HV_PACKET_ALIGNMENT) != 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }

    ring->header = memory;
    ring->data = (ios_u8 *)memory + IOS_HV_PAGE_SIZE;
    ring->data_size = (ios_u32)(byte_count - IOS_HV_PAGE_SIZE);
    if (ring->header->write_index >= ring->data_size
        || ring->header->read_index >= ring->data_size) {
        return IOS_ERROR(IOS_E_CORRUPT);
    }
    return IOS_OK;
}

ios_status hyperv_ring_write(
    struct ios_hv_ring *ring,
    ios_u16 packet_type,
    ios_u16 flags,
    ios_u64 transaction_id,
    const void *payload,
    ios_size payload_size,
    bool *signal_host
)
{
    struct ios_hv_packet_descriptor descriptor;
    ios_u64 trailer;
    ios_size packet_size;
    ios_size aligned_size;
    ios_size required;
    ios_u32 write_index;
    ios_u32 read_index;
    ios_u32 available;
    ios_u8 zeros[IOS_HV_PACKET_ALIGNMENT] = {0};

    if (ring == NULL || ring->header == NULL || signal_host == NULL
        || (payload_size != 0 && payload == NULL)
        || payload_size > UINT16_MAX * IOS_HV_PACKET_ALIGNMENT - sizeof(descriptor)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    packet_size = sizeof(descriptor) + payload_size;
    aligned_size = (packet_size + (IOS_HV_PACKET_ALIGNMENT - 1U))
        & ~(ios_size)(IOS_HV_PACKET_ALIGNMENT - 1U);
    if (aligned_size > SIZE_MAX - IOS_HV_PACKET_TRAILER_SIZE) {
        return IOS_ERROR(IOS_E_OVERFLOW);
    }
    required = aligned_size + IOS_HV_PACKET_TRAILER_SIZE;
    if (required >= ring->data_size) {
        return IOS_ERROR(IOS_E_NO_SPACE);
    }

    write_index = ring->header->write_index;
    x86_64_memory_barrier();
    read_index = ring->header->read_index;
    if (write_index >= ring->data_size || read_index >= ring->data_size) {
        return IOS_ERROR(IOS_E_CORRUPT);
    }
    available = ring->data_size - ring_used(write_index, read_index, ring->data_size) - 1U;
    if (required > available) {
        ring->header->pending_send_size = (ios_u32)required;
        return IOS_ERROR(IOS_E_WOULD_BLOCK);
    }

    descriptor.type = packet_type;
    descriptor.data_offset_8 = sizeof(descriptor) / IOS_HV_PACKET_ALIGNMENT;
    descriptor.length_8 = (ios_u16)(aligned_size / IOS_HV_PACKET_ALIGNMENT);
    descriptor.flags = flags;
    descriptor.transaction_id = transaction_id;
    ring_copy_in(ring, write_index, &descriptor, sizeof(descriptor));
    write_index = ring_advance(ring, write_index, sizeof(descriptor));
    if (payload_size != 0) {
        ring_copy_in(ring, write_index, payload, payload_size);
        write_index = ring_advance(ring, write_index, (ios_u32)payload_size);
    }
    if (aligned_size > packet_size) {
        const ios_size padding = aligned_size - packet_size;
        ring_copy_in(ring, write_index, zeros, padding);
        write_index = ring_advance(ring, write_index, (ios_u32)padding);
    }
    trailer = ((ios_u64)ring->header->write_index << 32) | read_index;
    ring_copy_in(ring, write_index, &trailer, sizeof(trailer));
    write_index = ring_advance(ring, write_index, sizeof(trailer));

    x86_64_memory_barrier();
    ring->header->write_index = write_index;
    x86_64_memory_barrier();
    *signal_host = ring->header->interrupt_mask == 0;
    return IOS_OK;
}

ios_status hyperv_ring_write_gpa_direct(
    struct ios_hv_ring *ring,
    ios_u16 flags,
    ios_u64 transaction_id,
    const void *payload,
    ios_size payload_size,
    ios_uptr data_physical,
    ios_size data_size,
    bool *signal_host
)
{
    enum { MAX_GPA_PAGES = 64, MAX_INLINE_PAYLOAD = IOS_HV_MESSAGE_PAYLOAD_SIZE };
    ios_u8 packet[sizeof(struct ios_hv_packet_descriptor) + 8
                  + 8 + MAX_GPA_PAGES * sizeof(ios_u64) + MAX_INLINE_PAYLOAD];
    struct ios_hv_packet_descriptor *descriptor = (void *)packet;
    ios_u32 *range_header;
    ios_u64 *pfns;
    ios_u32 page_count;
    ios_u32 data_offset;
    ios_u32 packet_size;
    ios_u32 aligned_size;
    ios_u32 required;
    ios_u32 write_index;
    ios_u32 read_index;
    ios_u32 available;
    ios_u64 trailer;

    if (ring == NULL || ring->header == NULL || signal_host == NULL
        || payload == NULL || payload_size == 0 || payload_size > MAX_INLINE_PAYLOAD
        || data_size == 0 || data_size > UINT32_MAX
        || data_physical > UINT64_MAX - data_size) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    page_count = (ios_u32)(((data_physical & (IOS_HV_PAGE_SIZE - 1U))
        + data_size + IOS_HV_PAGE_SIZE - 1U) / IOS_HV_PAGE_SIZE);
    if (page_count == 0 || page_count > MAX_GPA_PAGES) {
        return IOS_ERROR(IOS_E_OUT_OF_RANGE);
    }

    range_header = (ios_u32 *)(packet + sizeof(*descriptor));
    range_header[0] = 0;
    range_header[1] = 1;
    range_header[2] = (ios_u32)data_size;
    range_header[3] = (ios_u32)(data_physical & (IOS_HV_PAGE_SIZE - 1U));
    pfns = (ios_u64 *)(range_header + 4);
    for (ios_u32 index = 0; index < page_count; ++index) {
        pfns[index] = (data_physical / IOS_HV_PAGE_SIZE) + index;
    }
    data_offset = sizeof(*descriptor) + 16U + page_count * sizeof(ios_u64);
    memcpy(packet + data_offset, payload, payload_size);
    packet_size = data_offset + (ios_u32)payload_size;
    aligned_size = (packet_size + IOS_HV_PACKET_ALIGNMENT - 1U)
        & ~(IOS_HV_PACKET_ALIGNMENT - 1U);
    memset(packet + packet_size, 0, aligned_size - packet_size);
    required = aligned_size + IOS_HV_PACKET_TRAILER_SIZE;
    if (required >= ring->data_size) {
        return IOS_ERROR(IOS_E_NO_SPACE);
    }

    write_index = ring->header->write_index;
    x86_64_memory_barrier();
    read_index = ring->header->read_index;
    if (write_index >= ring->data_size || read_index >= ring->data_size) {
        return IOS_ERROR(IOS_E_CORRUPT);
    }
    available = ring->data_size - ring_used(write_index, read_index, ring->data_size) - 1U;
    if (required > available) {
        ring->header->pending_send_size = required;
        return IOS_ERROR(IOS_E_WOULD_BLOCK);
    }
    descriptor->type = IOS_HV_PACKET_TYPE_DATA_USING_GPA_DIRECT;
    descriptor->data_offset_8 = (ios_u16)(data_offset / IOS_HV_PACKET_ALIGNMENT);
    descriptor->length_8 = (ios_u16)(aligned_size / IOS_HV_PACKET_ALIGNMENT);
    descriptor->flags = flags;
    descriptor->transaction_id = transaction_id;
    ring_copy_in(ring, write_index, packet, aligned_size);
    write_index = ring_advance(ring, write_index, aligned_size);
    trailer = ((ios_u64)ring->header->write_index << 32) | read_index;
    ring_copy_in(ring, write_index, &trailer, sizeof(trailer));
    write_index = ring_advance(ring, write_index, sizeof(trailer));
    x86_64_memory_barrier();
    ring->header->write_index = write_index;
    x86_64_memory_barrier();
    *signal_host = ring->header->interrupt_mask == 0;
    return IOS_OK;
}

ios_status hyperv_ring_read(
    struct ios_hv_ring *ring,
    ios_u16 *packet_type,
    ios_u64 *transaction_id,
    void *payload,
    ios_size payload_capacity,
    ios_size *payload_size
)
{
    struct ios_hv_packet_descriptor descriptor;
    ios_u32 read_index;
    ios_u32 write_index;
    ios_u32 used;
    ios_u32 packet_size;
    ios_u32 data_offset;
    ios_u32 data_size;

    if (ring == NULL || ring->header == NULL || packet_type == NULL
        || transaction_id == NULL || payload_size == NULL
        || (payload_capacity != 0 && payload == NULL)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    read_index = ring->header->read_index;
    x86_64_memory_barrier();
    write_index = ring->header->write_index;
    if (write_index >= ring->data_size || read_index >= ring->data_size) {
        return IOS_ERROR(IOS_E_CORRUPT);
    }
    used = ring_used(write_index, read_index, ring->data_size);
    if (used == 0) {
        return IOS_ERROR(IOS_E_WOULD_BLOCK);
    }
    if (used < sizeof(descriptor) + IOS_HV_PACKET_TRAILER_SIZE) {
        return IOS_ERROR(IOS_E_CORRUPT);
    }
    ring_copy_out(ring, read_index, &descriptor, sizeof(descriptor));
    data_offset = (ios_u32)descriptor.data_offset_8 * IOS_HV_PACKET_ALIGNMENT;
    packet_size = (ios_u32)descriptor.length_8 * IOS_HV_PACKET_ALIGNMENT;
    if (data_offset < sizeof(descriptor) || data_offset > packet_size
        || packet_size > used - IOS_HV_PACKET_TRAILER_SIZE
        || (packet_size % IOS_HV_PACKET_ALIGNMENT) != 0) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    data_size = packet_size - data_offset;
    if (data_size > payload_capacity) {
        *payload_size = data_size;
        return IOS_ERROR(IOS_E_NO_SPACE);
    }
    if (data_size != 0) {
        ring_copy_out(ring, ring_advance(ring, read_index, data_offset), payload, data_size);
    }
    *packet_type = descriptor.type;
    *transaction_id = descriptor.transaction_id;
    *payload_size = data_size;

    read_index = ring_advance(ring, read_index,
        packet_size + IOS_HV_PACKET_TRAILER_SIZE);
    x86_64_memory_barrier();
    ring->header->read_index = read_index;
    x86_64_memory_barrier();
    return IOS_OK;
}
