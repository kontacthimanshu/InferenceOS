#include <inferenceos/system_module.h>

static bool range_is_valid(ios_uptr base, ios_u64 byte_count)
{
    return byte_count != 0 && byte_count <= UINT64_MAX - base;
}

static bool ranges_overlap(
    ios_uptr left_base,
    ios_u64 left_size,
    ios_uptr right_base,
    ios_u64 right_size
)
{
    return left_base < right_base + right_size && right_base < left_base + left_size;
}

static bool role_is_known(ios_u32 role)
{
    return role >= IOS_MODULE_ROLE_SHELL && role <= IOS_MODULE_ROLE_TEST_APPLICATION;
}

ios_status system_module_descriptor_validate(
    const struct ios_system_module_descriptor *descriptor,
    const struct ios_system_module_range *forbidden_ranges,
    ios_size forbidden_range_count,
    ios_system_module_digest_verifier verify_digest
)
{
    if (descriptor == NULL || verify_digest == NULL
        || (forbidden_range_count != 0 && forbidden_ranges == NULL)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (descriptor->structure_size != sizeof(*descriptor)
        || descriptor->structure_version != IOS_SYSTEM_MODULE_ABI_VERSION
        || descriptor->entry_abi_version != IOS_SYSTEM_MODULE_ABI_VERSION) {
        return IOS_ERROR(IOS_E_UNSUPPORTED_VERSION);
    }
    if (descriptor->application_identity == 0 || !role_is_known(descriptor->role)
        || (descriptor->flags & ~IOS_SYSTEM_MODULE_REQUIRED) != 0
        || descriptor->reserved != 0
        || !range_is_valid(descriptor->image_address, descriptor->image_size)) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    for (ios_size index = 0; index < forbidden_range_count; ++index) {
        if (!range_is_valid(forbidden_ranges[index].base, forbidden_ranges[index].byte_count)) {
            return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
        }
        if (ranges_overlap(
                descriptor->image_address,
                descriptor->image_size,
                forbidden_ranges[index].base,
                forbidden_ranges[index].byte_count)) {
            return IOS_ERROR(IOS_E_BAD_ADDRESS);
        }
    }
    return verify_digest(
        (const void *)descriptor->image_address,
        (ios_size)descriptor->image_size,
        descriptor->digest
    );
}

ios_status system_module_set_validate(
    const struct ios_system_module_descriptor *descriptors,
    ios_size descriptor_count,
    const struct ios_system_module_range *forbidden_ranges,
    ios_size forbidden_range_count,
    ios_system_module_digest_verifier verify_digest
)
{
    bool valid[IOS_SYSTEM_MODULE_MAX_COUNT] = { false };
    ios_size shell_count = 0;

    if (descriptors == NULL || descriptor_count == 0
        || descriptor_count > IOS_SYSTEM_MODULE_MAX_COUNT) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    for (ios_size index = 0; index < descriptor_count; ++index) {
        const ios_status status = system_module_descriptor_validate(
            &descriptors[index], forbidden_ranges, forbidden_range_count, verify_digest
        );
        if (IOS_FAILED(status)) {
            if (descriptors[index].role == IOS_MODULE_ROLE_SHELL
                || (!role_is_known(descriptors[index].role)
                    && (descriptors[index].flags & IOS_SYSTEM_MODULE_REQUIRED) != 0)) {
                return status;
            }
            continue;
        }
        valid[index] = true;
        if (descriptors[index].role == IOS_MODULE_ROLE_SHELL) {
            ++shell_count;
            if ((descriptors[index].flags & IOS_SYSTEM_MODULE_REQUIRED) == 0) {
                return IOS_ERROR(IOS_E_PROTOCOL);
            }
        }
        for (ios_size other = 0; other < index; ++other) {
            if (valid[other]
                && (descriptors[index].application_identity
                    == descriptors[other].application_identity
                || (descriptors[index].role != IOS_MODULE_ROLE_TEST_APPLICATION
                    && descriptors[index].role == descriptors[other].role))) {
                return IOS_ERROR(IOS_E_ALREADY_EXISTS);
            }
            if (valid[other] && ranges_overlap(
                    descriptors[index].image_address,
                    descriptors[index].image_size,
                    descriptors[other].image_address,
                    descriptors[other].image_size)) {
                return IOS_ERROR(IOS_E_BAD_ADDRESS);
            }
        }
    }
    return shell_count == 1 ? IOS_OK : IOS_ERROR(IOS_E_NOT_FOUND);
}

ios_u32 system_module_startup_rank(ios_u32 role)
{
    switch (role) {
    case IOS_MODULE_ROLE_SHELL: return 0;
    case IOS_MODULE_ROLE_GUI_DESKTOP: return 1;
    case IOS_MODULE_ROLE_GUI_TERMINAL: return 2;
    case IOS_MODULE_ROLE_FILE_EXPLORER: return 3;
    default: return 4;
    }
}
