#include <inferenceos/drivers/hyperv/vmbus.h>

#include <inferenceos/arch/hyperv.h>
#include <inferenceos/arch/io.h>
#include <inferenceos/runtime.h>

struct IOS_PACKED vmbus_initiate_contact {
    struct ios_vmbus_channel_message_header header;
    ios_u32 version_requested;
    ios_u32 target_vp;
    union IOS_PACKED {
        ios_u64 interrupt_page;
        struct IOS_PACKED {
            ios_u8 message_sint;
            ios_u8 reserved[7];
        } modern;
    } interrupt;
    ios_u64 monitor_page1;
    ios_u64 monitor_page2;
};

struct IOS_PACKED vmbus_gpa_range {
    ios_u32 byte_count;
    ios_u32 byte_offset;
    ios_u64 pfn[IOS_HV_MESSAGE_PAYLOAD_SIZE / sizeof(ios_u64) - 4];
};

struct IOS_PACKED vmbus_gpadl_header {
    struct ios_vmbus_channel_message_header header;
    ios_u32 child_relid;
    ios_u32 gpadl_id;
    ios_u16 range_buffer_length;
    ios_u16 range_count;
    struct vmbus_gpa_range range;
};

struct IOS_PACKED vmbus_gpadl_created {
    struct ios_vmbus_channel_message_header header;
    ios_u32 child_relid;
    ios_u32 gpadl_id;
    ios_u32 creation_status;
};

struct IOS_PACKED vmbus_open_channel_message {
    struct ios_vmbus_channel_message_header header;
    ios_u32 child_relid;
    ios_u32 open_id;
    ios_u32 ring_buffer_gpadl_id;
    ios_u32 target_vp;
    ios_u32 downstream_ring_buffer_page_offset;
    ios_u8 user_data[120];
};

struct IOS_PACKED vmbus_open_result {
    struct ios_vmbus_channel_message_header header;
    ios_u32 child_relid;
    ios_u32 open_id;
    ios_u32 status;
};

IOS_STATIC_ASSERT(sizeof(struct vmbus_initiate_contact) == 40,
                  "VMBus initiate-contact size");
IOS_STATIC_ASSERT(sizeof(struct vmbus_open_channel_message) == 148,
                  "VMBus open-channel size");

static ios_status hypercall_status(ios_u64 result)
{
    return (result & UINT64_C(0xffff)) == 0 ? IOS_OK : IOS_ERROR(IOS_E_IO);
}

static ios_status post_message(struct ios_vmbus *bus, const void *message, ios_size size)
{
    struct ios_hv_post_message_input *input;
    ios_u64 result = UINT64_MAX;

    if (bus == NULL || message == NULL || size > IOS_HV_MESSAGE_PAYLOAD_SIZE) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    input = bus->resources.post_message;
    memset(input, 0, sizeof(*input));
    input->connection_id = bus->message_connection_id;
    input->message_type = IOS_HV_MESSAGE_TYPE_VMBUS;
    input->payload_size = (ios_u32)size;
    memcpy(input->payload, message, size);
    x86_64_memory_barrier();
    for (ios_u32 attempt = 0; attempt < 3; ++attempt) {
        result = x86_64_hyperv_hypercall(
            bus->resources.hypercall_page, IOS_HV_CALL_POST_MESSAGE,
            bus->resources.post_message_physical, 0);
        if ((result & UINT64_C(0xffff)) == 0) {
            return IOS_OK;
        }
        for (ios_u32 spin = 0; spin < 1024; ++spin) {
            x86_64_cpu_relax();
        }
    }
    return hypercall_status(result);
}

static ios_status receive_message(
    struct ios_vmbus *bus, void *message, ios_size capacity, ios_size *size, ios_u32 spin_limit
)
{
    struct ios_hv_message *slot;

    if (bus == NULL || message == NULL || size == NULL || spin_limit == 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    slot = &bus->resources.message_page->slots[IOS_VMBUS_MESSAGE_SINT];
    for (ios_u32 spin = 0; spin < spin_limit; ++spin) {
        ios_u32 type = slot->message_type;

        if (type != 0) {
            ios_u8 flags;

            x86_64_memory_barrier();
            if (slot->payload_size > IOS_HV_MESSAGE_PAYLOAD_SIZE
                || slot->payload_size > capacity) {
                return IOS_ERROR(IOS_E_PROTOCOL);
            }
            *size = slot->payload_size;
            memcpy(message, slot->payload, *size);
            flags = slot->message_flags;
            x86_64_memory_barrier();
            slot->message_type = 0;
            x86_64_memory_barrier();
            if ((flags & 1U) != 0) {
                x86_64_hyperv_end_of_message();
            }
            return IOS_OK;
        }
        x86_64_cpu_relax();
    }
    return IOS_ERROR(IOS_E_TIMEOUT);
}

static ios_status wait_for_type(
    struct ios_vmbus *bus, ios_u32 expected, void *message, ios_size capacity,
    ios_size *size, ios_u32 spin_limit
)
{
    for (ios_u32 received = 0; received < IOS_VMBUS_MAX_OFFERS + 8U; ++received) {
        struct ios_vmbus_channel_message_header *header = message;
        ios_status status = receive_message(bus, message, capacity, size, spin_limit);

        if (IOS_FAILED(status)) {
            return status;
        }
        if (*size < sizeof(*header)) {
            return IOS_ERROR(IOS_E_PROTOCOL);
        }
        if (header->message_type == expected) {
            return IOS_OK;
        }
        if (header->message_type == IOS_VMBUS_MESSAGE_RESCIND_CHANNEL_OFFER
            || header->message_type == IOS_VMBUS_MESSAGE_OFFER_CHANNEL) {
            /* Offer changes are consumed by the explicit enumeration pass. A host that
             * interleaves them with a synchronous transaction must be retried there. */
            continue;
        }
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    return IOS_ERROR(IOS_E_TIMEOUT);
}

ios_status vmbus_initialize(
    struct ios_vmbus *bus,
    const struct ios_vmbus_resources *resources,
    ios_u64 guest_os_id,
    ios_u32 spin_limit
)
{
    static const ios_u32 versions[] = {
        IOS_VMBUS_VERSION_5_3, IOS_VMBUS_VERSION_5_2, IOS_VMBUS_VERSION_5_1,
        IOS_VMBUS_VERSION_5_0, IOS_VMBUS_VERSION_4_1
    };
    ios_status status;

    if (bus == NULL || resources == NULL || resources->hypercall_page == NULL
        || resources->post_message == NULL || resources->message_page == NULL
        || resources->event_page == NULL || spin_limit == 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    memset(bus, 0, sizeof(*bus));
    bus->resources = *resources;
    bus->state = IOS_VMBUS_CONNECTING;
    bus->last_stage = "hypercall";
    bus->message_connection_id = IOS_HV_CONNECTION_ID_VMBUS_LEGACY;
    bus->next_open_id = 1;
    bus->next_gpadl_id = 1;
    memset(resources->post_message, 0, IOS_HV_PAGE_SIZE);
    memset(resources->message_page, 0, IOS_HV_PAGE_SIZE);
    memset(resources->event_page, 0, IOS_HV_PAGE_SIZE);
    status = x86_64_hyperv_enable_hypercall(guest_os_id, resources->hypercall_page_physical);
    if (IOS_FAILED(status)) {
        bus->state = IOS_VMBUS_FAILED;
        return status;
    }
    bus->last_stage = "synic";
    status = x86_64_hyperv_enable_synic(
        resources->message_page_physical, resources->event_page_physical,
        IOS_VMBUS_MESSAGE_SINT, IOS_VMBUS_INTERRUPT_VECTOR);
    if (IOS_FAILED(status)) {
        bus->state = IOS_VMBUS_FAILED;
        return status;
    }

    bus->last_stage = "version";
    for (ios_size index = 0; index < IOS_ARRAY_COUNT(versions); ++index) {
        struct vmbus_initiate_contact contact;
        struct ios_vmbus_version_response response;
        ios_size response_size = 0;

        memset(&contact, 0, sizeof(contact));
        contact.header.message_type = IOS_VMBUS_MESSAGE_INITIATE_CONTACT;
        contact.version_requested = versions[index];
        contact.interrupt.modern.message_sint = IOS_VMBUS_MESSAGE_SINT;
        bus->message_connection_id = versions[index] >= IOS_VMBUS_VERSION_5_0
            ? IOS_HV_CONNECTION_ID_VMBUS_MODERN
            : IOS_HV_CONNECTION_ID_VMBUS_LEGACY;
        status = post_message(bus, &contact, sizeof(contact));
        if (IOS_FAILED(status)) {
            continue;
        }
        status = wait_for_type(bus, IOS_VMBUS_MESSAGE_VERSION_RESPONSE,
            &response, sizeof(response), &response_size, spin_limit);
        if (IOS_SUCCEEDED(status) && response_size >= 10 && response.version_supported != 0) {
            bus->negotiated_version = versions[index];
            if (versions[index] >= IOS_VMBUS_VERSION_5_0
                && response_size >= sizeof(response)
                && response.message_connection_id != 0) {
                bus->message_connection_id = response.message_connection_id;
            }
            bus->state = IOS_VMBUS_CONNECTED;
            bus->last_stage = "connected";
            return IOS_OK;
        }
    }
    bus->state = IOS_VMBUS_FAILED;
    return IOS_ERROR(IOS_E_UNSUPPORTED_VERSION);
}

ios_status vmbus_request_offers(struct ios_vmbus *bus, ios_u32 spin_limit)
{
    struct ios_vmbus_request_offers request = {{IOS_VMBUS_MESSAGE_REQUEST_OFFERS, 0}};
    ios_status status;

    if (bus == NULL || bus->state != IOS_VMBUS_CONNECTED || spin_limit == 0) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    bus->last_stage = "offers";
    bus->offer_count = 0;
    status = post_message(bus, &request, sizeof(request));
    if (IOS_FAILED(status)) {
        return status;
    }
    for (;;) {
        struct ios_vmbus_offer_channel message;
        struct ios_vmbus_channel_message_header *header = (void *)&message;
        ios_size size = 0;

        status = receive_message(bus, &message, sizeof(message), &size, spin_limit);
        if (IOS_FAILED(status)) {
            return status;
        }
        if (size < sizeof(*header)) {
            return IOS_ERROR(IOS_E_PROTOCOL);
        }
        if (header->message_type == IOS_VMBUS_MESSAGE_ALL_OFFERS_DELIVERED) {
            bus->last_stage = "ready";
            return IOS_OK;
        }
        if (header->message_type != IOS_VMBUS_MESSAGE_OFFER_CHANNEL
            || size < sizeof(message)) {
            return IOS_ERROR(IOS_E_PROTOCOL);
        }
        if (bus->offer_count == IOS_VMBUS_MAX_OFFERS) {
            return IOS_ERROR(IOS_E_NO_SPACE);
        }
        bus->offers[bus->offer_count++] = (struct ios_vmbus_offer) {
            .interface_type = message.offer.interface_type,
            .interface_instance = message.offer.interface_instance,
            .child_relid = message.child_relid,
            .connection_id = message.connection_id,
            .subchannel_index = message.offer.subchannel_index,
            .dedicated_interrupt = message.is_dedicated_interrupt != 0,
            .rescinded = false
        };
    }
}

const struct ios_vmbus_offer *vmbus_find_offer(
    const struct ios_vmbus *bus, const struct ios_hv_guid *interface_type, ios_size occurrence
)
{
    if (bus == NULL || interface_type == NULL) {
        return NULL;
    }
    for (ios_size index = 0; index < bus->offer_count; ++index) {
        const struct ios_vmbus_offer *offer = &bus->offers[index];

        if (!offer->rescinded && offer->subchannel_index == 0
            && hyperv_guid_equal(&offer->interface_type, interface_type)) {
            if (occurrence == 0) {
                return offer;
            }
            --occurrence;
        }
    }
    return NULL;
}

ios_status vmbus_open_channel(
    struct ios_vmbus *bus,
    const struct ios_vmbus_offer *offer,
    struct ios_vmbus_channel *channel,
    void *ring_memory,
    ios_uptr ring_physical,
    ios_u32 total_pages,
    ios_u32 outbound_pages,
    ios_u32 spin_limit
)
{
    struct vmbus_gpadl_header gpadl;
    struct vmbus_gpadl_created created;
    struct vmbus_open_channel_message open;
    struct vmbus_open_result result;
    ios_size gpadl_size;
    ios_size response_size;
    ios_status status;

    if (bus == NULL || offer == NULL || channel == NULL || ring_memory == NULL
        || (ring_physical & (IOS_HV_PAGE_SIZE - 1U)) != 0 || total_pages < 4
        || outbound_pages < 2 || outbound_pages > total_pages - 2
        || total_pages > IOS_ARRAY_COUNT(gpadl.range.pfn) || spin_limit == 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    memset(channel, 0, sizeof(*channel));
    memset(ring_memory, 0, (ios_size)total_pages * IOS_HV_PAGE_SIZE);
    channel->offer = *offer;
    channel->gpadl_id = bus->next_gpadl_id++;
    channel->open_id = bus->next_open_id++;

    memset(&gpadl, 0, sizeof(gpadl));
    gpadl.header.message_type = IOS_VMBUS_MESSAGE_GPADL_HEADER;
    gpadl.child_relid = offer->child_relid;
    gpadl.gpadl_id = channel->gpadl_id;
    gpadl.range_buffer_length = (ios_u16)(8U + total_pages * sizeof(ios_u64));
    gpadl.range_count = 1;
    gpadl.range.byte_count = total_pages * IOS_HV_PAGE_SIZE;
    for (ios_u32 index = 0; index < total_pages; ++index) {
        gpadl.range.pfn[index] = (ring_physical / IOS_HV_PAGE_SIZE) + index;
    }
    gpadl_size = offsetof(struct vmbus_gpadl_header, range.pfn)
        + total_pages * sizeof(ios_u64);
    status = post_message(bus, &gpadl, gpadl_size);
    if (IOS_FAILED(status)) {
        return status;
    }
    status = wait_for_type(bus, IOS_VMBUS_MESSAGE_GPADL_CREATED,
        &created, sizeof(created), &response_size, spin_limit);
    if (IOS_FAILED(status) || response_size < sizeof(created)
        || created.child_relid != offer->child_relid
        || created.gpadl_id != channel->gpadl_id || created.creation_status != 0) {
        return IOS_FAILED(status) ? status : IOS_ERROR(IOS_E_PROTOCOL);
    }

    memset(&open, 0, sizeof(open));
    open.header.message_type = IOS_VMBUS_MESSAGE_OPEN_CHANNEL;
    open.child_relid = offer->child_relid;
    open.open_id = channel->open_id;
    open.ring_buffer_gpadl_id = channel->gpadl_id;
    open.downstream_ring_buffer_page_offset = outbound_pages;
    status = post_message(bus, &open, sizeof(open));
    if (IOS_FAILED(status)) {
        return status;
    }
    status = wait_for_type(bus, IOS_VMBUS_MESSAGE_OPEN_CHANNEL_RESULT,
        &result, sizeof(result), &response_size, spin_limit);
    if (IOS_FAILED(status) || response_size < sizeof(result)
        || result.child_relid != offer->child_relid || result.open_id != channel->open_id
        || result.status != 0) {
        return IOS_FAILED(status) ? status : IOS_ERROR(IOS_E_PROTOCOL);
    }
    status = hyperv_ring_initialize(&channel->outbound, ring_memory,
        (ios_size)outbound_pages * IOS_HV_PAGE_SIZE);
    if (IOS_FAILED(status)) {
        return status;
    }
    status = hyperv_ring_initialize(&channel->inbound,
        (ios_u8 *)ring_memory + (ios_size)outbound_pages * IOS_HV_PAGE_SIZE,
        (ios_size)(total_pages - outbound_pages) * IOS_HV_PAGE_SIZE);
    if (IOS_FAILED(status)) {
        return status;
    }
    channel->open = true;
    return IOS_OK;
}

ios_status vmbus_channel_write(
    struct ios_vmbus *bus,
    struct ios_vmbus_channel *channel,
    ios_u16 packet_type,
    ios_u16 flags,
    ios_u64 transaction_id,
    const void *payload,
    ios_size payload_size
)
{
    struct ios_hv_signal_event_input *signal_input;
    bool signal_host;
    ios_status status;

    if (bus == NULL || channel == NULL || !channel->open) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    status = hyperv_ring_write(&channel->outbound, packet_type, flags,
        transaction_id, payload, payload_size, &signal_host);
    if (IOS_FAILED(status) || !signal_host) {
        return status;
    }
    signal_input = (struct ios_hv_signal_event_input *)bus->resources.post_message;
    memset(signal_input, 0, sizeof(*signal_input));
    signal_input->connection_id = channel->offer.connection_id;
    x86_64_memory_barrier();
    return hypercall_status(x86_64_hyperv_hypercall(
        bus->resources.hypercall_page, IOS_HV_CALL_SIGNAL_EVENT,
        bus->resources.post_message_physical, 0));
}

ios_status vmbus_channel_write_gpa_direct(
    struct ios_vmbus *bus,
    struct ios_vmbus_channel *channel,
    ios_u16 flags,
    ios_u64 transaction_id,
    const void *payload,
    ios_size payload_size,
    ios_uptr data_physical,
    ios_size data_size
)
{
    struct ios_hv_signal_event_input *signal_input;
    bool signal_host;
    ios_status status;

    if (bus == NULL || channel == NULL || !channel->open) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    status = hyperv_ring_write_gpa_direct(&channel->outbound, flags, transaction_id,
        payload, payload_size, data_physical, data_size, &signal_host);
    if (IOS_FAILED(status) || !signal_host) {
        return status;
    }
    signal_input = (struct ios_hv_signal_event_input *)bus->resources.post_message;
    memset(signal_input, 0, sizeof(*signal_input));
    signal_input->connection_id = channel->offer.connection_id;
    x86_64_memory_barrier();
    return hypercall_status(x86_64_hyperv_hypercall(
        bus->resources.hypercall_page, IOS_HV_CALL_SIGNAL_EVENT,
        bus->resources.post_message_physical, 0));
}

ios_status vmbus_channel_read(
    struct ios_vmbus_channel *channel,
    ios_u16 *packet_type,
    ios_u64 *transaction_id,
    void *payload,
    ios_size payload_capacity,
    ios_size *payload_size
)
{
    if (channel == NULL || !channel->open) {
        return IOS_ERROR(IOS_E_INVALID_STATE);
    }
    return hyperv_ring_read(&channel->inbound, packet_type, transaction_id,
        payload, payload_capacity, payload_size);
}

const char *vmbus_last_stage(const struct ios_vmbus *bus)
{
    return bus == NULL || bus->last_stage == NULL ? "uninitialized" : bus->last_stage;
}
