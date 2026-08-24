#include <inferenceos/test.h>

#include <inferenceos/system_module.h>

#include "loader.h"

#include <string.h>

static ios_u8 shell_image[64];
static ios_u8 desktop_image[64];
static ios_u8 terminal_image[64];
static ios_u8 explorer_image[64];

static bool manifest_header_is_supported(const char *header)
{
    return header != NULL && strcmp(header, "INFERENCEOS-SYSTEM-MODULES|1") == 0;
}

static bool decimal_field_is_valid(const char *begin, const char *end, bool allow_zero)
{
    bool nonzero = false;
    if (begin == end) {
        return false;
    }
    for (const char *cursor = begin; cursor < end; ++cursor) {
        if (*cursor < '0' || *cursor > '9') {
            return false;
        }
        nonzero = nonzero || *cursor != '0';
    }
    return allow_zero || nonzero;
}

static bool manifest_record_is_well_formed(const char *record)
{
    const char *fields[7];
    const char *ends[7];
    const char *cursor = record;
    const char *system_prefix = "/InferenceOS/System/";

    for (ios_size field = 0; field < 7; ++field) {
        fields[field] = cursor;
        while (*cursor != '\0' && *cursor != '|') {
            ++cursor;
        }
        ends[field] = cursor;
        if (field != 6) {
            if (*cursor != '|') {
                return false;
            }
            ++cursor;
        }
    }
    if (*cursor != '\0' || !decimal_field_is_valid(fields[0], ends[0], false)
        || ends[1] - fields[1] != 1 || fields[1][0] < '1' || fields[1][0] > '5'
        || ends[2] - fields[2] != 1 || (fields[2][0] != '0' && fields[2][0] != '1')
        || ends[3] - fields[3] != 1 || fields[3][0] != '1'
        || !decimal_field_is_valid(fields[4], ends[4], false)
        || ends[5] - fields[5] != 64) {
        return false;
    }
    for (const char *hex = fields[5]; hex < ends[5]; ++hex) {
        if (!((*hex >= '0' && *hex <= '9') || (*hex >= 'a' && *hex <= 'f'))) {
            return false;
        }
    }
    if ((ios_size)(ends[6] - fields[6]) <= strlen(system_prefix)
        || strncmp(fields[6], system_prefix, strlen(system_prefix)) != 0) {
        return false;
    }
    for (const char *path = fields[6]; path + 1 < ends[6]; ++path) {
        if ((path[0] == '/' && path[1] == '/')
            || (path + 2 < ends[6] && path[0] == '/' && path[1] == '.'
                && (path[2] == '/' || (path[2] == '.' && path + 3 < ends[6]
                    && path[3] == '/')))) {
            return false;
        }
    }
    return true;
}

static void digest_for_image(const void *image, ios_size length, ios_u8 digest[32])
{
    const ios_u8 *bytes = image;
    for (ios_size index = 0; index < 32; ++index) {
        digest[index] = (ios_u8)(bytes[index % length] ^ (ios_u8)length ^ (ios_u8)index);
    }
}

static ios_status verify_digest(
    const void *image,
    ios_size image_size,
    const ios_u8 expected_digest[IOS_SYSTEM_MODULE_DIGEST_SIZE]
)
{
    ios_u8 actual[IOS_SYSTEM_MODULE_DIGEST_SIZE];
    digest_for_image(image, image_size, actual);
    return memcmp(actual, expected_digest, sizeof(actual)) == 0
        ? IOS_OK : IOS_ERROR(IOS_E_CORRUPT);
}

static struct ios_system_module_descriptor module(
    ios_u64 identity,
    ios_u32 role,
    ios_u32 flags,
    ios_u8 *image,
    ios_size image_size
)
{
    struct ios_system_module_descriptor descriptor = {
        .structure_size = sizeof(struct ios_system_module_descriptor),
        .structure_version = IOS_SYSTEM_MODULE_ABI_VERSION,
        .application_identity = identity,
        .role = role,
        .flags = flags,
        .image_address = (ios_uptr)image,
        .image_size = image_size,
        .entry_abi_version = IOS_SYSTEM_MODULE_ABI_VERSION
    };
    digest_for_image(image, image_size, descriptor.digest);
    return descriptor;
}

static void initialize_images(void)
{
    for (ios_size index = 0; index < sizeof(shell_image); ++index) {
        shell_image[index] = (ios_u8)(0x10 + index);
        desktop_image[index] = (ios_u8)(0x40 + index);
        terminal_image[index] = (ios_u8)(0x70 + index);
        explorer_image[index] = (ios_u8)(0xa0 + index);
    }
}

static void test_required_shell_and_valid_digest_are_accepted(void)
{
    struct ios_system_module_descriptor descriptor;
    initialize_images();
    descriptor = module(
        1, IOS_MODULE_ROLE_SHELL, IOS_SYSTEM_MODULE_REQUIRED,
        shell_image, sizeof(shell_image)
    );
    IOS_TEST_ASSERT_STATUS(
        system_module_set_validate(&descriptor, 1, NULL, 0, verify_digest), IOS_OK
    );
}

static void test_versioned_manifest_grammar_bounds_digest_and_path(void)
{
    static const char valid_record[] =
        "1|1|1|1|64|0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef|"
        "/InferenceOS/System/shell.elf";
    static const char uppercase_digest[] =
        "1|1|1|1|64|0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef|"
        "/InferenceOS/System/shell.elf";
    static const char traversal_path[] =
        "1|1|1|1|64|0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef|"
        "/InferenceOS/System/../shell.elf";

    IOS_TEST_ASSERT(manifest_header_is_supported("INFERENCEOS-SYSTEM-MODULES|1"));
    IOS_TEST_ASSERT(!manifest_header_is_supported("INFERENCEOS-SYSTEM-MODULES|2"));
    IOS_TEST_ASSERT(manifest_record_is_well_formed(valid_record));
    IOS_TEST_ASSERT(!manifest_record_is_well_formed(uppercase_digest));
    IOS_TEST_ASSERT(!manifest_record_is_well_formed(traversal_path));
}

static void test_digest_mismatch_and_module_overlap_are_rejected(void)
{
    struct ios_system_module_descriptor descriptor;
    struct ios_system_module_range forbidden;
    initialize_images();
    descriptor = module(
        1, IOS_MODULE_ROLE_SHELL, IOS_SYSTEM_MODULE_REQUIRED,
        shell_image, sizeof(shell_image)
    );
    descriptor.digest[5] ^= 1;
    IOS_TEST_ASSERT_STATUS(
        system_module_descriptor_validate(&descriptor, NULL, 0, verify_digest),
        IOS_ERROR(IOS_E_CORRUPT)
    );

    descriptor = module(
        1, IOS_MODULE_ROLE_SHELL, IOS_SYSTEM_MODULE_REQUIRED,
        shell_image, sizeof(shell_image)
    );
    forbidden.base = (ios_uptr)shell_image + 16;
    forbidden.byte_count = 16;
    IOS_TEST_ASSERT_STATUS(
        system_module_descriptor_validate(&descriptor, &forbidden, 1, verify_digest),
        IOS_ERROR(IOS_E_BAD_ADDRESS)
    );
}

static void test_duplicate_identity_role_and_required_unknown_role_are_rejected(void)
{
    struct ios_system_module_descriptor descriptors[2];
    initialize_images();
    descriptors[0] = module(
        1, IOS_MODULE_ROLE_SHELL, IOS_SYSTEM_MODULE_REQUIRED,
        shell_image, sizeof(shell_image)
    );
    descriptors[1] = module(
        1, IOS_MODULE_ROLE_GUI_DESKTOP, 0, desktop_image, sizeof(desktop_image)
    );
    IOS_TEST_ASSERT_STATUS(
        system_module_set_validate(descriptors, 2, NULL, 0, verify_digest),
        IOS_ERROR(IOS_E_ALREADY_EXISTS)
    );

    descriptors[1] = module(
        2, IOS_MODULE_ROLE_SHELL, IOS_SYSTEM_MODULE_REQUIRED,
        desktop_image, sizeof(desktop_image)
    );
    IOS_TEST_ASSERT_STATUS(
        system_module_set_validate(descriptors, 2, NULL, 0, verify_digest),
        IOS_ERROR(IOS_E_ALREADY_EXISTS)
    );

    descriptors[0].role = 99;
    IOS_TEST_ASSERT_STATUS(
        system_module_set_validate(descriptors, 1, NULL, 0, verify_digest),
        IOS_ERROR(IOS_E_PROTOCOL)
    );
}

static void test_gui_failures_degrade_but_shell_failure_stops_boot(void)
{
    struct ios_system_module_descriptor descriptors[4];
    initialize_images();
    descriptors[0] = module(
        1, IOS_MODULE_ROLE_SHELL, IOS_SYSTEM_MODULE_REQUIRED,
        shell_image, sizeof(shell_image)
    );
    descriptors[1] = module(
        2, IOS_MODULE_ROLE_GUI_DESKTOP, 0, desktop_image, sizeof(desktop_image)
    );
    descriptors[2] = module(
        3, IOS_MODULE_ROLE_GUI_TERMINAL, 0, terminal_image, sizeof(terminal_image)
    );
    descriptors[3] = module(
        4, IOS_MODULE_ROLE_FILE_EXPLORER, 0, explorer_image, sizeof(explorer_image)
    );
    descriptors[1].digest[0] ^= 1;
    descriptors[2].digest[0] ^= 1;
    descriptors[3].digest[0] ^= 1;
    IOS_TEST_ASSERT_STATUS(
        system_module_set_validate(descriptors, 4, NULL, 0, verify_digest), IOS_OK
    );

    descriptors[0].digest[0] ^= 1;
    IOS_TEST_ASSERT_STATUS(
        system_module_set_validate(descriptors, 4, NULL, 0, verify_digest),
        IOS_ERROR(IOS_E_CORRUPT)
    );
}

static void test_absent_optional_file_explorer_does_not_block_boot(void)
{
    struct ios_system_module_descriptor descriptors[3];
    initialize_images();
    descriptors[0] = module(
        1, IOS_MODULE_ROLE_SHELL, IOS_SYSTEM_MODULE_REQUIRED,
        shell_image, sizeof(shell_image)
    );
    descriptors[1] = module(
        2, IOS_MODULE_ROLE_GUI_DESKTOP, 0, desktop_image, sizeof(desktop_image)
    );
    descriptors[2] = module(
        3, IOS_MODULE_ROLE_GUI_TERMINAL, 0, terminal_image, sizeof(terminal_image)
    );
    IOS_TEST_ASSERT_STATUS(
        system_module_set_validate(descriptors, 3, NULL, 0, verify_digest), IOS_OK
    );
}

static void test_real_sha256_matches_published_vectors(void)
{
    static const ios_u8 empty_digest[32] = {
        0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
        0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
        0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
        0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55
    };
    static const ios_u8 abc_digest[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
    };
    ios_u8 actual[32];
    ios_sha256("", 0, actual);
    IOS_TEST_ASSERT(memcmp(actual, empty_digest, sizeof(actual)) == 0);
    ios_sha256("abc", 3, actual);
    IOS_TEST_ASSERT(memcmp(actual, abc_digest, sizeof(actual)) == 0);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_required_shell_and_valid_digest_are_accepted),
    IOS_TEST_CASE(test_versioned_manifest_grammar_bounds_digest_and_path),
    IOS_TEST_CASE(test_digest_mismatch_and_module_overlap_are_rejected),
    IOS_TEST_CASE(test_duplicate_identity_role_and_required_unknown_role_are_rejected),
    IOS_TEST_CASE(test_gui_failures_degrade_but_shell_failure_stops_boot),
    IOS_TEST_CASE(test_absent_optional_file_explorer_does_not_block_boot),
    IOS_TEST_CASE(test_real_sha256_matches_published_vectors)
};

const size_t ios_test_case_count = sizeof(ios_test_cases) / sizeof(ios_test_cases[0]);
