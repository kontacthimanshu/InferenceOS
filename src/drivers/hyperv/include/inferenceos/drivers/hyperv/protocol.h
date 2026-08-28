#ifndef INFERENCEOS_DRIVERS_HYPERV_PROTOCOL_H
#define INFERENCEOS_DRIVERS_HYPERV_PROTOCOL_H

#include <inferenceos/base.h>

enum {
    IOS_HV_PAGE_SIZE = 4096,
    IOS_HV_MESSAGE_PAYLOAD_SIZE = 240,
    IOS_HV_POST_MESSAGE_INPUT_SIZE = 256,
    IOS_HV_PACKET_ALIGNMENT = 8,
    IOS_HV_PACKET_TRAILER_SIZE = 8,
    IOS_HV_CONNECTION_ID_VMBUS_LEGACY = 1,
    IOS_HV_CONNECTION_ID_VMBUS_MODERN = 4,
    IOS_HV_MESSAGE_TYPE_VMBUS = 1,

    IOS_HV_CALL_POST_MESSAGE = 0x005c,
    IOS_HV_CALL_SIGNAL_EVENT = 0x005d,

    IOS_HV_PACKET_TYPE_COMPLETION = 0x000b,
    IOS_HV_PACKET_TYPE_DATA_INBAND = 0x0006,
    IOS_HV_PACKET_TYPE_DATA_USING_GPA_DIRECT = 0x0009,
    IOS_HV_PACKET_FLAG_COMPLETION_REQUESTED = 0x0001
};

enum ios_vmbus_channel_message_type {
    IOS_VMBUS_MESSAGE_INVALID = 0,
    IOS_VMBUS_MESSAGE_OFFER_CHANNEL = 1,
    IOS_VMBUS_MESSAGE_RESCIND_CHANNEL_OFFER = 2,
    IOS_VMBUS_MESSAGE_REQUEST_OFFERS = 3,
    IOS_VMBUS_MESSAGE_ALL_OFFERS_DELIVERED = 4,
    IOS_VMBUS_MESSAGE_OPEN_CHANNEL = 5,
    IOS_VMBUS_MESSAGE_OPEN_CHANNEL_RESULT = 6,
    IOS_VMBUS_MESSAGE_CLOSE_CHANNEL = 7,
    IOS_VMBUS_MESSAGE_GPADL_HEADER = 8,
    IOS_VMBUS_MESSAGE_GPADL_BODY = 9,
    IOS_VMBUS_MESSAGE_GPADL_CREATED = 10,
    IOS_VMBUS_MESSAGE_GPADL_TEARDOWN = 11,
    IOS_VMBUS_MESSAGE_GPADL_TORNDOWN = 12,
    IOS_VMBUS_MESSAGE_RELID_RELEASED = 13,
    IOS_VMBUS_MESSAGE_INITIATE_CONTACT = 14,
    IOS_VMBUS_MESSAGE_VERSION_RESPONSE = 15,
    IOS_VMBUS_MESSAGE_UNLOAD = 16,
    IOS_VMBUS_MESSAGE_UNLOAD_RESPONSE = 17
};

enum ios_vstor_operation {
    IOS_VSTOR_OPERATION_COMPLETE_IO = 1,
    IOS_VSTOR_OPERATION_EXECUTE_SRB = 3,
    IOS_VSTOR_OPERATION_RESET_BUS = 6,
    IOS_VSTOR_OPERATION_BEGIN_INITIALIZATION = 7,
    IOS_VSTOR_OPERATION_END_INITIALIZATION = 8,
    IOS_VSTOR_OPERATION_QUERY_PROTOCOL_VERSION = 9,
    IOS_VSTOR_OPERATION_QUERY_PROPERTIES = 10
};

enum {
    IOS_VSTOR_PROTOCOL_VERSION_6_2 = 0x0602,
    IOS_VSTOR_PROTOCOL_VERSION_6_0 = 0x0600,
    IOS_VSTOR_PROTOCOL_VERSION_5_1 = 0x0501,
    IOS_VSTOR_PACKET_SIZE = 64,
    IOS_VSTOR_SRB_SIZE = 52
};

struct ios_hv_guid {
    ios_u8 bytes[16];
};

struct IOS_PACKED ios_hv_post_message_input {
    ios_u32 connection_id;
    ios_u32 reserved;
    ios_u32 message_type;
    ios_u32 payload_size;
    ios_u8 payload[IOS_HV_MESSAGE_PAYLOAD_SIZE];
};

struct IOS_PACKED ios_hv_packet_descriptor {
    ios_u16 type;
    ios_u16 data_offset_8;
    ios_u16 length_8;
    ios_u16 flags;
    ios_u64 transaction_id;
};

struct IOS_PACKED ios_vmbus_channel_message_header {
    ios_u32 message_type;
    ios_u32 reserved;
};

struct IOS_PACKED ios_vmbus_version_response {
    struct ios_vmbus_channel_message_header header;
    ios_u8 version_supported;
    ios_u8 connection_state;
    ios_u16 padding;
    ios_u32 message_connection_id;
};

struct IOS_PACKED ios_vmbus_request_offers {
    struct ios_vmbus_channel_message_header header;
};

struct IOS_PACKED ios_vmbus_channel_offer {
    struct ios_hv_guid interface_type;
    struct ios_hv_guid interface_instance;
    ios_u64 reserved[2];
    ios_u16 flags;
    ios_u16 mmio_megabytes;
    ios_u8 user_defined[120];
    ios_u16 subchannel_index;
    ios_u16 reserved2;
};

struct IOS_PACKED ios_hv_message {
    volatile ios_u32 message_type;
    ios_u8 payload_size;
    ios_u8 message_flags;
    ios_u16 reserved;
    ios_u64 sender;
    ios_u8 payload[IOS_HV_MESSAGE_PAYLOAD_SIZE];
};

struct ios_hv_message_page {
    struct ios_hv_message slots[16];
};

struct IOS_PACKED ios_hv_signal_event_input {
    ios_u32 connection_id;
    ios_u16 flag_number;
    ios_u16 reserved;
};

struct IOS_PACKED ios_vmbus_offer_channel {
    struct ios_vmbus_channel_message_header header;
    struct ios_vmbus_channel_offer offer;
    ios_u32 child_relid;
    ios_u8 monitor_id;
    ios_u8 monitor_allocated;
    ios_u16 is_dedicated_interrupt;
    ios_u32 connection_id;
};

struct IOS_PACKED ios_vstor_srb {
    ios_u16 length;
    ios_u8 srb_status;
    ios_u8 scsi_status;
    ios_u8 port_number;
    ios_u8 path_id;
    ios_u8 target_id;
    ios_u8 lun;
    ios_u8 cdb_length;
    ios_u8 sense_info_length;
    ios_u8 data_in;
    ios_u8 reserved;
    ios_u32 transfer_length;
    union IOS_PACKED {
        ios_u8 cdb[16];
        ios_u8 sense_data[20];
    } request;
    ios_u16 reserved2;
    ios_u8 queue_tag;
    ios_u8 queue_action;
    ios_u32 srb_flags;
    ios_u32 timeout_value;
    ios_u32 queue_sort_key;
};

struct IOS_PACKED ios_vstor_packet {
    ios_u32 operation;
    ios_u32 flags;
    ios_u32 status;
    union IOS_PACKED {
        struct ios_vstor_srb srb;
        struct IOS_PACKED {
            ios_u16 major_minor;
            ios_u16 revision;
            ios_u32 reserved;
        } version;
        struct IOS_PACKED {
            ios_u32 port_number;
            ios_u32 path_id;
            ios_u32 target_id;
            ios_u32 flags;
            ios_u32 maximum_transfer_bytes;
            ios_u32 reserved[8];
        } properties;
        ios_u8 bytes[52];
    } payload;
};

extern const struct ios_hv_guid IOS_HV_GUID_SYNTHETIC_SCSI;
extern const struct ios_hv_guid IOS_HV_GUID_SYNTHETIC_KEYBOARD;
extern const struct ios_hv_guid IOS_HV_GUID_SYNTHETIC_MOUSE;

bool hyperv_guid_equal(const struct ios_hv_guid *left, const struct ios_hv_guid *right);

IOS_STATIC_ASSERT(sizeof(struct ios_hv_guid) == 16, "Hyper-V GUID wire size");
IOS_STATIC_ASSERT(sizeof(struct ios_hv_post_message_input) == 256, "PostMessage input size");
IOS_STATIC_ASSERT(sizeof(struct ios_hv_packet_descriptor) == 16, "VMBus packet descriptor size");
IOS_STATIC_ASSERT(sizeof(struct ios_hv_message) == 256, "SynIC message slot size");
IOS_STATIC_ASSERT(sizeof(struct ios_hv_message_page) == IOS_HV_PAGE_SIZE,
                  "SynIC message page size");
IOS_STATIC_ASSERT(sizeof(struct ios_hv_signal_event_input) == 8,
                  "SignalEvent input size");
IOS_STATIC_ASSERT(sizeof(struct ios_vmbus_channel_message_header) == 8,
                  "VMBus channel message header size");
IOS_STATIC_ASSERT(sizeof(struct ios_vstor_srb) == IOS_VSTOR_SRB_SIZE, "vstor SRB size");
IOS_STATIC_ASSERT(offsetof(struct ios_vstor_srb, request) == 16, "vstor SRB CDB offset");
IOS_STATIC_ASSERT(offsetof(struct ios_vstor_srb, srb_flags) == 40,
                  "vstor SRB flags offset");
IOS_STATIC_ASSERT(sizeof(struct ios_vstor_packet) == IOS_VSTOR_PACKET_SIZE,
                  "vstor packet size");

#endif
