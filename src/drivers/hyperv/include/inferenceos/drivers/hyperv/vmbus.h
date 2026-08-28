#ifndef INFERENCEOS_DRIVERS_HYPERV_VMBUS_H
#define INFERENCEOS_DRIVERS_HYPERV_VMBUS_H

#include <inferenceos/drivers/hyperv/ring.h>

enum {
    IOS_VMBUS_MESSAGE_SINT = 2,
    IOS_VMBUS_INTERRUPT_VECTOR = 0x41,
    IOS_VMBUS_MAX_OFFERS = 32,
    IOS_VMBUS_MAX_CHANNELS = 8,
    IOS_VMBUS_DEFAULT_SPIN_LIMIT = 10000000,
    IOS_VMBUS_VERSION_5_3 = (5U << 16) | 3U,
    IOS_VMBUS_VERSION_5_2 = (5U << 16) | 2U,
    IOS_VMBUS_VERSION_5_1 = (5U << 16) | 1U,
    IOS_VMBUS_VERSION_5_0 = (5U << 16),
    IOS_VMBUS_VERSION_4_1 = (4U << 16) | 1U
};

enum ios_vmbus_state {
    IOS_VMBUS_DISCONNECTED,
    IOS_VMBUS_CONNECTING,
    IOS_VMBUS_CONNECTED,
    IOS_VMBUS_FAILED
};

struct ios_vmbus_resources {
    void *hypercall_page;
    ios_uptr hypercall_page_physical;
    struct ios_hv_post_message_input *post_message;
    ios_uptr post_message_physical;
    struct ios_hv_message_page *message_page;
    ios_uptr message_page_physical;
    void *event_page;
    ios_uptr event_page_physical;
};

struct ios_vmbus_offer {
    struct ios_hv_guid interface_type;
    struct ios_hv_guid interface_instance;
    ios_u32 child_relid;
    ios_u32 connection_id;
    ios_u16 subchannel_index;
    bool dedicated_interrupt;
    bool rescinded;
};

struct ios_vmbus_channel {
    struct ios_vmbus_offer offer;
    struct ios_hv_ring outbound;
    struct ios_hv_ring inbound;
    ios_u32 open_id;
    ios_u32 gpadl_id;
    bool open;
};

struct ios_vmbus {
    struct ios_vmbus_resources resources;
    struct ios_vmbus_offer offers[IOS_VMBUS_MAX_OFFERS];
    ios_size offer_count;
    ios_u32 negotiated_version;
    ios_u32 message_connection_id;
    ios_u32 next_open_id;
    ios_u32 next_gpadl_id;
    enum ios_vmbus_state state;
    const char *last_stage;
};

ios_status vmbus_initialize(
    struct ios_vmbus *bus,
    const struct ios_vmbus_resources *resources,
    ios_u64 guest_os_id,
    ios_u32 spin_limit
);
ios_status vmbus_request_offers(struct ios_vmbus *bus, ios_u32 spin_limit);
const struct ios_vmbus_offer *vmbus_find_offer(
    const struct ios_vmbus *bus, const struct ios_hv_guid *interface_type, ios_size occurrence
);
ios_status vmbus_open_channel(
    struct ios_vmbus *bus,
    const struct ios_vmbus_offer *offer,
    struct ios_vmbus_channel *channel,
    void *ring_memory,
    ios_uptr ring_physical,
    ios_u32 total_pages,
    ios_u32 outbound_pages,
    ios_u32 spin_limit
);
ios_status vmbus_channel_write(
    struct ios_vmbus *bus,
    struct ios_vmbus_channel *channel,
    ios_u16 packet_type,
    ios_u16 flags,
    ios_u64 transaction_id,
    const void *payload,
    ios_size payload_size
);
ios_status vmbus_channel_write_gpa_direct(
    struct ios_vmbus *bus,
    struct ios_vmbus_channel *channel,
    ios_u16 flags,
    ios_u64 transaction_id,
    const void *payload,
    ios_size payload_size,
    ios_uptr data_physical,
    ios_size data_size
);
ios_status vmbus_channel_read(
    struct ios_vmbus_channel *channel,
    ios_u16 *packet_type,
    ios_u64 *transaction_id,
    void *payload,
    ios_size payload_capacity,
    ios_size *payload_size
);
const char *vmbus_last_stage(const struct ios_vmbus *bus);

#endif
