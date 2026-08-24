#ifndef INFERENCEOS_PROPRIETARY_ADAPTER_H
#define INFERENCEOS_PROPRIETARY_ADAPTER_H

#include <inferenceos/process.h>

enum {
    IOS_PROPRIETARY_ADAPTER_VERSION = 1,
    IOS_PROPRIETARY_ADAPTER_PAYLOAD_CAPACITY = 128,
    IOS_PROPRIETARY_ADAPTER_REGISTRATION_CAPACITY = 32,
    IOS_PROPRIETARY_ADAPTER_CAPABILITY_CAPACITY = 128
};

/*
 * The adapter handle is minted from trusted registration policy. This schema intentionally has
 * no extension, hash, or arbitrary native-type selector. Payload meaning belongs exclusively to
 * the documented official adapter identified by that opaque handle.
 */
struct ios_proprietary_adapter_request {
    ios_u16 size;
    ios_u16 version;
    ios_u32 flags;
    ios_handle adapter_handle;
    ios_handle content_handle;
    ios_u32 operation;
    ios_u16 input_size;
    ios_u16 reserved16;
    ios_u8 input[IOS_PROPRIETARY_ADAPTER_PAYLOAD_CAPACITY];
};

struct ios_proprietary_adapter_reply {
    ios_u16 size;
    ios_u16 version;
    ios_u32 flags;
    ios_status status;
    ios_u16 output_size;
    ios_u16 reserved16;
    ios_u32 reserved32;
    ios_u8 output[IOS_PROPRIETARY_ADAPTER_PAYLOAD_CAPACITY];
};

typedef ios_status (*ios_proprietary_adapter_invoke_function)(
    void *context,
    struct ios_process *proprietary_process,
    ios_handle reduced_content_handle,
    ios_u32 operation,
    const ios_u8 *input,
    ios_size input_size,
    ios_u8 *output,
    ios_size output_capacity,
    ios_size *output_size
);

/* Registration is a trusted boot/configuration action, never a caller assertion. */
struct ios_proprietary_adapter_descriptor {
    ios_u16 size;
    ios_u16 version;
    ios_u32 flags;
    ios_u64 adapter_identity;
    ios_u64 proprietary_application_identity;
    ios_u64 authorized_caller_identity;
    ios_u64 allowed_operation_mask;
    ios_u64 required_content_rights;
    ios_proprietary_adapter_invoke_function invoke;
    void *context;
};

struct ios_proprietary_adapter_service;

struct ios_proprietary_adapter_registration {
    struct ios_process *proprietary_process;
    struct ios_proprietary_adapter_descriptor descriptor;
    bool occupied;
};

struct ios_proprietary_adapter_capability {
    struct ios_proprietary_adapter_service *service;
    struct ios_proprietary_adapter_registration *registration;
    ios_u64 owner_process_id;
    ios_u64 owner_application_identity;
    ios_u32 reference_count;
    bool occupied;
};

struct ios_proprietary_adapter_service {
    struct ios_proprietary_adapter_registration
        registrations[IOS_PROPRIETARY_ADAPTER_REGISTRATION_CAPACITY];
    struct ios_proprietary_adapter_capability
        capabilities[IOS_PROPRIETARY_ADAPTER_CAPABILITY_CAPACITY];
    ios_u32 registration_count;
    ios_u32 active_capability_count;
    bool initialized;
};

ios_status ios_proprietary_adapter_service_initialize(
    struct ios_proprietary_adapter_service *service
);
ios_status ios_proprietary_adapter_register(
    struct ios_proprietary_adapter_service *service,
    struct ios_process *proprietary_process,
    const struct ios_proprietary_adapter_descriptor *descriptor
);
ios_status ios_proprietary_adapter_authorize(
    struct ios_proprietary_adapter_service *service,
    struct ios_process *caller_process,
    ios_u64 adapter_identity,
    ios_handle *adapter_handle
);
ios_status ios_proprietary_adapter_invoke(
    struct ios_proprietary_adapter_service *service,
    struct ios_process *caller_process,
    const struct ios_proprietary_adapter_request *request,
    struct ios_proprietary_adapter_reply *reply
);

IOS_STATIC_ASSERT(
    sizeof(struct ios_proprietary_adapter_request) == 160,
    "proprietary adapter request wire size"
);
IOS_STATIC_ASSERT(
    offsetof(struct ios_proprietary_adapter_request, input) == 32,
    "proprietary adapter request payload offset"
);
IOS_STATIC_ASSERT(
    sizeof(struct ios_proprietary_adapter_reply) == 152,
    "proprietary adapter reply wire size"
);
IOS_STATIC_ASSERT(
    offsetof(struct ios_proprietary_adapter_reply, output) == 24,
    "proprietary adapter reply payload offset"
);

#endif
