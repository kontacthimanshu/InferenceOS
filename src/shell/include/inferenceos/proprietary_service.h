#ifndef INFERENCEOS_PROPRIETARY_SERVICE_H
#define INFERENCEOS_PROPRIETARY_SERVICE_H

#include <inferenceos/shell_protocol.h>
#include <inferenceos/type_capability.h>

struct ios_proprietary_match {
    const char *base_name;
    void *content;
    ios_object_retain_function retain_content;
    ios_object_release_function release_content;
    ios_u64 internal_type_identity;
    ios_u64 byte_size;
    ios_u32 generic_attributes;
};

typedef ios_status (*ios_proprietary_process_resolver)(
    void *context,
    ios_u64 process_id,
    ios_u64 application_identity,
    struct ios_process **process
);

/* The provider must return only files whose authoritative type equals the requested identity. */
typedef ios_status (*ios_proprietary_enumerate_function)(
    void *context,
    ios_u64 directory_handle,
    ios_u64 internal_type_identity,
    ios_u64 continuation,
    struct ios_proprietary_match *matches,
    ios_size capacity,
    ios_size *match_count,
    ios_u64 *next_continuation
);

struct ios_proprietary_service_config {
    const struct ios_type_capability_service *type_capabilities;
    ios_proprietary_process_resolver resolve_process;
    ios_proprietary_enumerate_function enumerate;
    void *context;
};

struct ios_proprietary_service {
    struct ios_proprietary_service_config config;
    bool initialized;
};

ios_status ios_proprietary_service_initialize(
    struct ios_proprietary_service *service,
    const struct ios_proprietary_service_config *config
);

ios_status ios_proprietary_service_dispatch(
    void *context,
    ios_u64 caller_process_id,
    ios_u64 caller_application_identity,
    enum ios_shell_operation operation,
    const struct ios_shell_file_view_request *request,
    struct ios_shell_file_view_reply *reply
);

#endif
