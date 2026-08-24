#ifndef INFERENCEOS_APPLICATION_BINDINGS_H
#define INFERENCEOS_APPLICATION_BINDINGS_H

#include <inferenceos/errors.h>

enum { IOS_APPLICATION_BINDING_CAPACITY = 128 };

struct ios_application_type_binding {
    ios_u64 application_identity;
    ios_u64 internal_type_identity;
    bool occupied;
};

struct ios_application_binding_registry {
    struct ios_application_type_binding bindings[IOS_APPLICATION_BINDING_CAPACITY];
    ios_u32 binding_count;
};

void ios_application_bindings_initialize(
    struct ios_application_binding_registry *registry
);
ios_status ios_application_bindings_register(
    struct ios_application_binding_registry *registry,
    ios_u64 application_identity,
    ios_u64 internal_type_identity
);
ios_status ios_application_bindings_authorize(
    const struct ios_application_binding_registry *registry,
    ios_u64 application_identity,
    ios_u64 internal_type_identity
);
ios_status ios_application_bindings_enumerate(
    const struct ios_application_binding_registry *registry,
    ios_u64 application_identity,
    ios_u64 *internal_type_identities,
    ios_size capacity,
    ios_size *type_count
);
ios_u32 ios_application_bindings_count(
    const struct ios_application_binding_registry *registry
);

#endif
