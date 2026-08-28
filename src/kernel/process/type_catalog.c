#include <inferenceos/type_catalog.h>

#include <inferenceos/runtime.h>

#define TYPE_CATALOG_SLOT_BITS 16U
#define TYPE_CATALOG_SLOT_MASK UINT64_C(0xffff)

static bool valid_file_icon(enum ios_presentation_icon icon)
{
    return icon == IOS_ICON_GENERIC_FILE || icon == IOS_ICON_TEXT
        || icon == IOS_ICON_IMAGE || icon == IOS_ICON_APPLICATION;
}

static ios_u32 generation_from_boot_identity(ios_u64 identity)
{
    ios_u32 generation = (ios_u32)(identity ^ (identity >> 32));
    return generation == 0 ? 1U : generation;
}

static ios_type_icon_capability encode_capability(ios_size index, ios_u32 generation)
{
    return ((ios_u64)generation << TYPE_CATALOG_SLOT_BITS) | (ios_u64)(index + 1U);
}

ios_status ios_type_catalog_initialize(
    struct ios_type_catalog *catalog,
    ios_u64 kernel_boot_identity
)
{
    if (catalog == NULL || kernel_boot_identity == 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    memset(catalog, 0, sizeof(*catalog));
    catalog->generation_seed = generation_from_boot_identity(kernel_boot_identity);
    return IOS_OK;
}

ios_status ios_type_catalog_register(
    struct ios_type_catalog *catalog,
    ios_u64 internal_type_identity,
    enum ios_presentation_icon icon,
    ios_type_icon_capability *capability
)
{
    if (catalog == NULL || capability == NULL || catalog->generation_seed == 0
        || internal_type_identity == 0 || !valid_file_icon(icon)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    for (ios_size index = 0; index < IOS_TYPE_CATALOG_CAPACITY; ++index) {
        const struct ios_type_catalog_slot *slot = &catalog->slots[index];
        if (slot->occupied && slot->internal_type_identity == internal_type_identity) {
            if (slot->icon != icon) return IOS_ERROR(IOS_E_ALREADY_EXISTS);
            *capability = encode_capability(index, slot->generation);
            return IOS_OK;
        }
    }
    for (ios_size index = 0; index < IOS_TYPE_CATALOG_CAPACITY; ++index) {
        struct ios_type_catalog_slot *slot = &catalog->slots[index];
        if (!slot->occupied) {
            slot->internal_type_identity = internal_type_identity;
            slot->generation = catalog->generation_seed + (ios_u32)index;
            if (slot->generation == 0) slot->generation = 1;
            slot->icon = icon;
            slot->occupied = true;
            ++catalog->entry_count;
            *capability = encode_capability(index, slot->generation);
            return IOS_OK;
        }
    }
    return IOS_ERROR(IOS_E_NO_SPACE);
}

ios_status ios_type_catalog_resolve_icon(
    const struct ios_type_catalog *catalog,
    ios_type_icon_capability capability,
    enum ios_type_catalog_object_kind object_kind,
    enum ios_presentation_icon *icon
)
{
    if (catalog == NULL || icon == NULL || catalog->generation_seed == 0
        || (object_kind != IOS_TYPE_CATALOG_REGULAR_FILE
            && object_kind != IOS_TYPE_CATALOG_DIRECTORY)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    if (object_kind == IOS_TYPE_CATALOG_DIRECTORY) {
        *icon = IOS_ICON_FOLDER;
        return IOS_OK;
    }

    *icon = IOS_ICON_GENERIC_FILE;
    const ios_u64 encoded_slot = capability & TYPE_CATALOG_SLOT_MASK;
    const ios_u64 generation = capability >> TYPE_CATALOG_SLOT_BITS;
    if (encoded_slot == 0 || encoded_slot > IOS_TYPE_CATALOG_CAPACITY || generation == 0) {
        return IOS_OK;
    }
    const struct ios_type_catalog_slot *slot = &catalog->slots[encoded_slot - 1U];
    if (slot->occupied && slot->generation == generation) *icon = slot->icon;
    return IOS_OK;
}

ios_status ios_type_catalog_resolve_identity(
    const struct ios_type_catalog *catalog,
    ios_type_icon_capability capability,
    ios_u64 *internal_type_identity
)
{
    const ios_u64 encoded_slot = capability & TYPE_CATALOG_SLOT_MASK;
    const ios_u64 generation = capability >> TYPE_CATALOG_SLOT_BITS;
    if (catalog == NULL || internal_type_identity == NULL || catalog->generation_seed == 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *internal_type_identity = 0;
    if (encoded_slot == 0 || encoded_slot > IOS_TYPE_CATALOG_CAPACITY || generation == 0) {
        return IOS_ERROR(IOS_E_BAD_HANDLE);
    }
    const struct ios_type_catalog_slot *slot = &catalog->slots[encoded_slot - 1U];
    if (!slot->occupied || slot->generation != generation) {
        return IOS_ERROR(IOS_E_BAD_HANDLE);
    }
    *internal_type_identity = slot->internal_type_identity;
    return IOS_OK;
}

ios_status ios_type_catalog_find_capability(
    const struct ios_type_catalog *catalog,
    ios_u64 internal_type_identity,
    ios_type_icon_capability *capability
)
{
    if (catalog == NULL || capability == NULL || catalog->generation_seed == 0
        || internal_type_identity == 0) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *capability = IOS_INVALID_TYPE_ICON_CAPABILITY;
    for (ios_size index = 0; index < IOS_TYPE_CATALOG_CAPACITY; ++index) {
        const struct ios_type_catalog_slot *slot = &catalog->slots[index];
        if (slot->occupied && slot->internal_type_identity == internal_type_identity) {
            *capability = encode_capability(index, slot->generation);
            return IOS_OK;
        }
    }
    return IOS_ERROR(IOS_E_NOT_FOUND);
}

ios_u32 ios_type_catalog_entry_count(const struct ios_type_catalog *catalog)
{
    return catalog == NULL ? 0 : catalog->entry_count;
}
