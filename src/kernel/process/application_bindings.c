#include <inferenceos/application_bindings.h>

#include <inferenceos/runtime.h>

void ios_application_bindings_initialize(
    struct ios_application_binding_registry *registry)
{
    if (registry != NULL) memset(registry, 0, sizeof(*registry));
}

ios_status ios_application_bindings_register(
    struct ios_application_binding_registry *registry,
    ios_u64 application_identity,
    ios_u64 internal_type_identity)
{
    if (registry == NULL || application_identity == 0 || internal_type_identity == 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    for (ios_size index = 0; index < IOS_APPLICATION_BINDING_CAPACITY; ++index) {
        const struct ios_application_type_binding *binding = &registry->bindings[index];
        if (binding->occupied
            && binding->application_identity == application_identity
            && binding->internal_type_identity == internal_type_identity) {
            return IOS_ERROR(IOS_E_ALREADY_EXISTS);
        }
    }
    for (ios_size index = 0; index < IOS_APPLICATION_BINDING_CAPACITY; ++index) {
        struct ios_application_type_binding *binding = &registry->bindings[index];
        if (!binding->occupied) {
            binding->application_identity = application_identity;
            binding->internal_type_identity = internal_type_identity;
            binding->occupied = true;
            ++registry->binding_count;
            return IOS_OK;
        }
    }
    return IOS_ERROR(IOS_E_NO_SPACE);
}

ios_status ios_application_bindings_authorize(
    const struct ios_application_binding_registry *registry,
    ios_u64 application_identity,
    ios_u64 internal_type_identity)
{
    if (registry == NULL || application_identity == 0 || internal_type_identity == 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    for (ios_size index = 0; index < IOS_APPLICATION_BINDING_CAPACITY; ++index) {
        const struct ios_application_type_binding *binding = &registry->bindings[index];
        if (binding->occupied
            && binding->application_identity == application_identity
            && binding->internal_type_identity == internal_type_identity) {
            return IOS_OK;
        }
    }
    return IOS_ERROR(IOS_E_ACCESS_DENIED);
}

ios_status ios_application_bindings_enumerate(
    const struct ios_application_binding_registry *registry,
    ios_u64 application_identity,
    ios_u64 *internal_type_identities,
    ios_size capacity,
    ios_size *type_count)
{
    ios_size count = 0;
    if (registry == NULL || application_identity == 0 || internal_type_identities == NULL
        || capacity == 0 || type_count == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *type_count = 0;
    for (ios_size index = 0; index < IOS_APPLICATION_BINDING_CAPACITY; ++index) {
        const struct ios_application_type_binding *binding = &registry->bindings[index];
        if (binding->occupied && binding->application_identity == application_identity) {
            ++count;
        }
    }
    if (count == 0) return IOS_ERROR(IOS_E_NOT_FOUND);
    if (count > capacity) {
        *type_count = count;
        return IOS_ERROR(IOS_E_NO_SPACE);
    }
    count = 0;
    for (ios_size index = 0; index < IOS_APPLICATION_BINDING_CAPACITY; ++index) {
        const struct ios_application_type_binding *binding = &registry->bindings[index];
        if (binding->occupied && binding->application_identity == application_identity) {
            internal_type_identities[count++] = binding->internal_type_identity;
        }
    }
    *type_count = count;
    return IOS_OK;
}

ios_u32 ios_application_bindings_count(
    const struct ios_application_binding_registry *registry)
{
    return registry == NULL ? 0 : registry->binding_count;
}
