#ifndef INFERENCEOS_TYPE_CAPABILITY_H
#define INFERENCEOS_TYPE_CAPABILITY_H

#include <inferenceos/application_bindings.h>
#include <inferenceos/process.h>
#include <inferenceos/type_catalog.h>

enum { IOS_PROCESS_TYPE_CAPABILITY_CAPACITY = 128 };

struct ios_type_capability_service;

struct ios_process_type_capability {
    struct ios_type_capability_service *service;
    ios_u64 owner_process_id;
    ios_u64 owner_application_identity;
    ios_u64 internal_type_identity;
    ios_type_icon_capability catalog_capability;
    ios_u32 reference_count;
    bool occupied;
};

struct ios_type_capability_service {
    const struct ios_application_binding_registry *bindings;
    const struct ios_type_catalog *catalog;
    struct ios_process_type_capability capabilities[IOS_PROCESS_TYPE_CAPABILITY_CAPACITY];
    ios_u32 active_count;
};

ios_status ios_type_capability_service_initialize(
    struct ios_type_capability_service *service,
    const struct ios_application_binding_registry *bindings,
    const struct ios_type_catalog *catalog
);
ios_status ios_type_capability_mint(
    struct ios_type_capability_service *service,
    struct ios_process *process,
    ios_type_icon_capability catalog_capability,
    ios_handle *handle
);
ios_status ios_type_capability_resolve(
    const struct ios_type_capability_service *service,
    const struct ios_process *process,
    ios_handle handle,
    ios_u64 *internal_type_identity,
    ios_type_icon_capability *catalog_capability
);
ios_status ios_type_capability_authorize(
    const struct ios_type_capability_service *service,
    const struct ios_process *process,
    ios_handle handle,
    ios_u64 required_internal_type_identity
);
ios_u32 ios_type_capability_active_count(
    const struct ios_type_capability_service *service
);

#endif
