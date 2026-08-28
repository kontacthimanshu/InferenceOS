#ifndef INFERENCEOS_DRIVERS_HYPERV_RING_H
#define INFERENCEOS_DRIVERS_HYPERV_RING_H

#include <inferenceos/drivers/hyperv/protocol.h>
#include <inferenceos/errors.h>

struct ios_hv_ring_header {
    volatile ios_u32 write_index;
    volatile ios_u32 read_index;
    volatile ios_u32 interrupt_mask;
    volatile ios_u32 pending_send_size;
    ios_u32 reserved[12];
    union {
        volatile ios_u32 value;
        ios_u8 bytes[4];
    } feature_bits;
    ios_u8 padding[IOS_HV_PAGE_SIZE - 68];
};

struct ios_hv_ring {
    struct ios_hv_ring_header *header;
    ios_u8 *data;
    ios_u32 data_size;
};

ios_status hyperv_ring_initialize(struct ios_hv_ring *ring, void *memory, ios_size byte_count);
ios_status hyperv_ring_write(
    struct ios_hv_ring *ring,
    ios_u16 packet_type,
    ios_u16 flags,
    ios_u64 transaction_id,
    const void *payload,
    ios_size payload_size,
    bool *signal_host
);
ios_status hyperv_ring_write_gpa_direct(
    struct ios_hv_ring *ring,
    ios_u16 flags,
    ios_u64 transaction_id,
    const void *payload,
    ios_size payload_size,
    ios_uptr data_physical,
    ios_size data_size,
    bool *signal_host
);
ios_status hyperv_ring_read(
    struct ios_hv_ring *ring,
    ios_u16 *packet_type,
    ios_u64 *transaction_id,
    void *payload,
    ios_size payload_capacity,
    ios_size *payload_size
);

IOS_STATIC_ASSERT(sizeof(struct ios_hv_ring_header) == IOS_HV_PAGE_SIZE,
                  "VMBus ring header occupies one page");

#endif
