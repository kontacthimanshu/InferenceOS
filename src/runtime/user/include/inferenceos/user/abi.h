#ifndef INFERENCEOS_USER_ABI_H
#define INFERENCEOS_USER_ABI_H

#include <inferenceos/base.h>
#include <inferenceos/errors.h>

enum {
    IOS_SYSCALL_ABI_MAJOR = 1,
    IOS_SYSCALL_ABI_MINOR = 0,
    IOS_SYSCALL_MAX_NUMBER = 127,
    IOS_SYSCALL_ABI_INFO = 0,
    IOS_SYSCALL_PROCESS_EXIT = 1,
    IOS_SYSCALL_IPC_SERVICE_CONNECT = 2,
    IOS_SYSCALL_IPC_SEND = 3,
    IOS_SYSCALL_IPC_RECEIVE = 4
};

enum ios_syscall_feature {
    IOS_SYSCALL_FEATURE_VERSIONED_STRUCTURES = UINT64_C(1) << 0,
    IOS_SYSCALL_FEATURE_SAFE_USER_COPY = UINT64_C(1) << 1,
    IOS_SYSCALL_FEATURE_PROCESS_HANDLES = UINT64_C(1) << 2,
    IOS_SYSCALL_FEATURE_IPC = UINT64_C(1) << 3
};

struct ios_syscall_structure_header {
    ios_u16 size;
    ios_u16 version;
};

struct ios_syscall_abi_info {
    ios_u16 size;
    ios_u16 version;
    ios_u16 major;
    ios_u16 minor;
    ios_u64 feature_bits;
    ios_u64 reserved;
};

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

struct ios_user_ipc_connection {
    ios_u64 endpoint_handle;
    ios_u64 service_generation;
};

struct ios_user_ipc_connect_request {
    ios_u16 size;
    ios_u16 version;
    ios_u32 service;
    ios_u32 flags;
    ios_u32 reserved32;
    ios_u64 endpoint_handle;
    ios_u64 service_generation;
};

struct ios_user_ipc_send_request {
    ios_u16 size;
    ios_u16 version;
    ios_u32 flags;
    ios_u64 endpoint_handle;
    ios_u64 request_id;
    ios_u32 operation;
    ios_u32 message_flags;
    ios_u16 item_count;
    ios_u16 payload_size;
    ios_u32 reserved32;
    ios_uptr payload_address;
};

struct ios_user_ipc_receive_request {
    ios_u16 size;
    ios_u16 version;
    ios_u32 flags;
    ios_u64 endpoint_handle;
    ios_uptr message_address;
    ios_u32 message_capacity;
    ios_u32 reserved32;
};

IOS_STATIC_ASSERT(sizeof(struct ios_syscall_abi_info) == 24, "syscall ABI-info size");
IOS_STATIC_ASSERT(sizeof(struct ios_ipc_message_header) == 40, "IPC header size");
IOS_STATIC_ASSERT(sizeof(struct ios_ipc_message) == 552, "IPC message size");
IOS_STATIC_ASSERT(sizeof(struct ios_user_ipc_connect_request) == 32, "IPC connect request size");
IOS_STATIC_ASSERT(sizeof(struct ios_user_ipc_send_request) == 48, "IPC send request size");
IOS_STATIC_ASSERT(sizeof(struct ios_user_ipc_receive_request) == 32, "IPC receive request size");

#endif
