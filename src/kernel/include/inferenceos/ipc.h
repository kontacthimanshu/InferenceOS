#ifndef INFERENCEOS_IPC_H
#define INFERENCEOS_IPC_H

#include <inferenceos/scheduler.h>

enum {
    IOS_IPC_ABI_VERSION = 1,
    IOS_IPC_MAX_ENDPOINTS = 64,
    IOS_IPC_MAX_QUEUE_DEPTH = 16,
    IOS_IPC_MAX_PAYLOAD_SIZE = 512,
    IOS_IPC_MAX_ITEM_COUNT = 64,
    IOS_IPC_MAX_SERVICES = 16
};

enum ios_trusted_service {
    IOS_SERVICE_SHELL = 1,
    IOS_SERVICE_GUI = 2,
    IOS_SERVICE_FILE_VIEW = 3,
    IOS_SERVICE_PROPRIETARY_ADAPTER = 4
};

struct ios_ipc_message_header {
    ios_u16 size;
    ios_u16 version;
    ios_u32 operation;
    ios_u64 request_id;
    ios_u64 caller_process_id;
    ios_u64 caller_application_identity;
    ios_u32 flags;
    ios_u16 item_count;
    ios_u16 payload_size;
};

struct ios_ipc_message {
    struct ios_ipc_message_header header;
    ios_u8 payload[IOS_IPC_MAX_PAYLOAD_SIZE];
};

struct ios_ipc_endpoint;

ios_status ipc_initialize(void);
ios_status ipc_endpoint_create(
    struct ios_process *owner,
    ios_u16 queue_depth,
    ios_handle *handle
);
ios_status ipc_send(
    struct ios_process *sender,
    ios_handle endpoint_handle,
    ios_u64 request_id,
    ios_u32 operation,
    ios_u32 flags,
    ios_u16 item_count,
    const void *payload,
    ios_u16 payload_size,
    bool block
);
ios_status ipc_receive(
    struct ios_process *receiver,
    ios_handle endpoint_handle,
    struct ios_ipc_message *message,
    bool block
);
ios_status ipc_trust_service(
    enum ios_trusted_service service,
    ios_u64 application_identity
);
ios_status ipc_service_register(
    struct ios_process *provider,
    enum ios_trusted_service service,
    ios_handle endpoint_handle
);
ios_status ipc_service_connect(
    struct ios_process *client,
    enum ios_trusted_service service,
    ios_handle *endpoint_handle
);
ios_u64 ipc_service_generation(enum ios_trusted_service service);
ios_status ipc_service_unregister(
    struct ios_process *provider,
    enum ios_trusted_service service
);

#endif
