#include "loader.h"

#include <inferenceos/runtime.h>

enum { MANIFEST_LINE_MAX = 512, MANIFEST_PATH_MAX = 255 };

static bool parse_decimal(const char *begin, const char *end, ios_u64 *value)
{
    ios_u64 result = 0;
    if (begin == end) { return false; }
    for (const char *cursor = begin; cursor < end; ++cursor) {
        if (*cursor < '0' || *cursor > '9'
            || result > (UINT64_MAX - (ios_u64)(*cursor - '0')) / 10U) { return false; }
        result = result * 10U + (ios_u64)(*cursor - '0');
    }
    *value = result;
    return true;
}

static bool parse_digest(const char *text, ios_size length, ios_u8 digest[32])
{
    if (length != 64) { return false; }
    for (ios_size index = 0; index < 32; ++index) {
        ios_u8 value = 0;
        for (ios_size nibble = 0; nibble < 2; ++nibble) {
            const char character = text[index * 2 + nibble];
            if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'))) {
                return false;
            }
            value = (ios_u8)((value << 4) | (character <= '9'
                ? character - '0' : character - 'a' + 10));
        }
        digest[index] = value;
    }
    return true;
}

static bool path_to_uefi(const char *begin, const char *end, efi_char16 path[MANIFEST_PATH_MAX + 1])
{
    static const char prefix[] = "/InferenceOS/System/";
    const ios_size length = (ios_size)(end - begin);
    if (length <= sizeof(prefix) - 1 || length > MANIFEST_PATH_MAX
        || strncmp(begin, prefix, sizeof(prefix) - 1) != 0 || begin[length - 1] == '/') {
        return false;
    }
    for (ios_size index = 0; index < length; ++index) {
        const char character = begin[index];
        if ((unsigned char)character > 0x7f || character == '\\'
            || (character == '/' && index + 1 < length && begin[index + 1] == '/')
            || (character == '/' && index + 2 < length && begin[index + 1] == '.'
                && (begin[index + 2] == '/' || (begin[index + 2] == '.'
                    && index + 3 < length && begin[index + 3] == '/')))) {
            return false;
        }
        path[index] = character == '/' ? '\\' : (efi_char16)(ios_u8)character;
    }
    path[length] = 0;
    return true;
}

static ios_status parse_record(
    const char *line, ios_size length, ios_u64 values[5], ios_u8 digest[32],
    efi_char16 path[MANIFEST_PATH_MAX + 1]
) {
    const char *fields[7]; const char *ends[7];
    const char *cursor = line; const char *limit = line + length;
    for (ios_size field = 0; field < 7; ++field) {
        fields[field] = cursor;
        while (cursor < limit && *cursor != '|') { ++cursor; }
        ends[field] = cursor;
        if (field != 6) { if (cursor == limit) { return IOS_ERROR(IOS_E_PROTOCOL); } ++cursor; }
    }
    if (cursor != limit) { return IOS_ERROR(IOS_E_PROTOCOL); }
    for (ios_size field = 0; field < 5; ++field) {
        if (!parse_decimal(fields[field], ends[field], &values[field])) {
            return IOS_ERROR(IOS_E_PROTOCOL);
        }
    }
    if (*values == 0 || values[1] < IOS_MODULE_ROLE_SHELL
        || values[1] > IOS_MODULE_ROLE_TEST_APPLICATION || values[2] > 1
        || values[3] != IOS_SYSTEM_MODULE_ABI_VERSION || values[4] == 0
        || !parse_digest(fields[5], (ios_size)(ends[5] - fields[5]), digest)
        || !path_to_uefi(fields[6], ends[6], path)) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    return IOS_OK;
}

static bool is_gui_role(ios_u32 role)
{
    return role == IOS_MODULE_ROLE_GUI_DESKTOP || role == IOS_MODULE_ROLE_GUI_TERMINAL;
}

ios_status ios_uefi_load_modules(
    const char *manifest, ios_size manifest_size, ios_uefi_read_file read_file,
    ios_uefi_allocate_image allocate_image, void *context,
    struct ios_system_module_descriptor *descriptors, ios_size capacity, ios_size *count,
    bool *gui_unavailable
) {
    static const char header[] = "INFERENCEOS-SYSTEM-MODULES|1";
    ios_size offset = 0; ios_size output_count = 0; ios_size shell_count = 0;
    if (manifest == NULL || read_file == NULL || allocate_image == NULL || descriptors == NULL
        || count == NULL || gui_unavailable == NULL || capacity == 0
        || manifest_size <= sizeof(header) || manifest_size > 32768
        || memcmp(manifest, header, sizeof(header) - 1) != 0
        || manifest[sizeof(header) - 1] != '\n') { return IOS_ERROR(IOS_E_PROTOCOL); }
    *gui_unavailable = false;
    offset = sizeof(header);
    while (offset < manifest_size) {
        ios_size end = offset;
        ios_u64 values[5]; ios_u8 expected[32]; efi_char16 path[MANIFEST_PATH_MAX + 1];
        void *file_data = NULL; ios_size file_size = 0; ios_uptr image_address = 0; ios_u8 actual[32];
        while (end < manifest_size && manifest[end] != '\n' && manifest[end] != '\r') { ++end; }
        if (end == offset || end - offset > MANIFEST_LINE_MAX || output_count == capacity) {
            return IOS_ERROR(IOS_E_OUT_OF_RANGE);
        }
        ios_status status = parse_record(manifest + offset, end - offset, values, expected, path);
        if (IOS_FAILED(status)) { return status; }
        for (ios_size index = 0; index < output_count; ++index) {
            if (descriptors[index].application_identity == *values
                || (values[1] != IOS_MODULE_ROLE_TEST_APPLICATION
                    && descriptors[index].role == values[1])) {
                return IOS_ERROR(IOS_E_ALREADY_EXISTS);
            }
        }
        status = read_file(context, path, &file_data, &file_size);
        if (IOS_SUCCEEDED(status) && file_size == values[4]) {
            ios_sha256(file_data, file_size, actual);
            if (memcmp(actual, expected, sizeof(actual)) != 0) { status = IOS_ERROR(IOS_E_CORRUPT); }
        } else if (IOS_SUCCEEDED(status)) { status = IOS_ERROR(IOS_E_CORRUPT); }
        if (IOS_FAILED(status)) {
            if (values[2] != 0 || values[1] == IOS_MODULE_ROLE_SHELL) { return status; }
            if (is_gui_role((ios_u32)values[1])) { *gui_unavailable = true; }
        } else {
            status = allocate_image(context, file_size, 0, false, &image_address);
            if (IOS_FAILED(status)) { return status; }
            memcpy((void *)image_address, file_data, file_size);
            descriptors[output_count] = (struct ios_system_module_descriptor) {
                .structure_size = sizeof(struct ios_system_module_descriptor),
                .structure_version = IOS_SYSTEM_MODULE_ABI_VERSION,
                .application_identity = *values, .role = (ios_u32)values[1],
                .flags = values[2] != 0 ? IOS_SYSTEM_MODULE_REQUIRED : 0,
                .image_address = image_address, .image_size = file_size,
                .entry_abi_version = (ios_u32)values[3]
            };
            memcpy(descriptors[output_count].digest, expected, sizeof(expected));
            if (values[1] == IOS_MODULE_ROLE_SHELL) { ++shell_count; }
            ++output_count;
        }
        while (end < manifest_size && (manifest[end] == '\n' || manifest[end] == '\r')) { ++end; }
        offset = end;
    }
    if (shell_count != 1) { return IOS_ERROR(IOS_E_NOT_FOUND); }
    *count = output_count;
    return IOS_OK;
}
