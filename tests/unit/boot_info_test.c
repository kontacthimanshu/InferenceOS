#include <inferenceos/test.h>

#include <inferenceos/base.h>
#include <inferenceos/boot_info.h>
#include <inferenceos/errors.h>

#include <string.h>

enum { TEST_MINIMUM_DESCRIPTOR_SIZE = 32 };

struct test_memory_descriptor {
    ios_u64 physical_start;
    ios_u64 page_count;
    ios_u32 kind;
    ios_u32 version;
    ios_u64 reserved;
};

static ios_u8 byte_checksum(const void *data, ios_size length)
{
    const ios_u8 *bytes = data;
    ios_u8 sum = 0;
    for (ios_size index = 0; index < length; ++index) {
        sum = (ios_u8)(sum + bytes[index]);
    }
    return sum;
}

static void seal_boot_info(struct ios_boot_info *info)
{
    info->checksum = 0;
    info->checksum = (ios_u8)(0U - byte_checksum(info, info->structure_size));
}

static bool range_is_valid(ios_uptr base, ios_u64 length)
{
    return base != 0 && length != 0 && length <= UINT64_MAX - base;
}

static ios_status validate_boot_info(const struct ios_boot_info *info)
{
    ios_u64 framebuffer_minimum;

    if (info == NULL || info->structure_size != sizeof(*info)
        || info->version != IOS_BOOT_INFO_VERSION) {
        return IOS_ERROR(IOS_E_UNSUPPORTED_VERSION);
    }
    if (byte_checksum(info, info->structure_size) != 0 || info->reserved0 != 0
        || info->reserved1 != 0 || info->reserved2 != 0
        || (info->flags & ~IOS_BOOT_FLAG_GUI_UNAVAILABLE) != 0) {
        return IOS_ERROR(IOS_E_CORRUPT);
    }
    if (!range_is_valid(
            info->memory_map_address,
            (ios_u64)info->memory_map_count * info->memory_descriptor_size)
        || info->memory_descriptor_size < TEST_MINIMUM_DESCRIPTOR_SIZE
        || info->memory_map_count == 0) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    if (info->module_descriptor_count == 0 || info->module_descriptor_size == 0
        || !range_is_valid(
            info->module_descriptors_address,
            (ios_u64)info->module_descriptor_count * info->module_descriptor_size)) {
        return IOS_ERROR(IOS_E_PROTOCOL);
    }
    if ((info->flags & IOS_BOOT_FLAG_GUI_UNAVAILABLE) != 0) {
        return info->framebuffer_address == 0 && info->framebuffer_size == 0
            ? IOS_OK : IOS_ERROR(IOS_E_PROTOCOL);
    }
    if (info->framebuffer_width != 1024 || info->framebuffer_height != 768
        || info->framebuffer_stride < info->framebuffer_width
        || info->framebuffer_format != IOS_BOOT_FRAMEBUFFER_BGRX8888) {
        return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    }
    framebuffer_minimum = (ios_u64)info->framebuffer_stride
        * info->framebuffer_height * sizeof(ios_u32);
    if (!range_is_valid(info->framebuffer_address, info->framebuffer_size)
        || info->framebuffer_size < framebuffer_minimum) {
        return IOS_ERROR(IOS_E_BAD_ADDRESS);
    }
    return IOS_OK;
}

static struct ios_boot_info valid_boot_info(void)
{
    static struct test_memory_descriptor memory_map[2];
    static ios_u8 modules[160];
    struct ios_boot_info info = {
        .structure_size = sizeof(struct ios_boot_info),
        .version = IOS_BOOT_INFO_VERSION,
        .memory_map_address = (ios_uptr)memory_map,
        .memory_map_count = 2,
        .memory_descriptor_size = sizeof(struct test_memory_descriptor),
        .framebuffer_address = UINT64_C(0x80000000),
        .framebuffer_size = UINT64_C(1024) * 768 * sizeof(ios_u32),
        .framebuffer_width = 1024,
        .framebuffer_height = 768,
        .framebuffer_stride = 1024,
        .framebuffer_format = IOS_BOOT_FRAMEBUFFER_BGRX8888,
        .module_descriptors_address = (ios_uptr)modules,
        .module_descriptor_count = 2,
        .module_descriptor_size = 80
    };
    seal_boot_info(&info);
    return info;
}

static void test_valid_versioned_handoff_is_accepted(void)
{
    struct ios_boot_info info = valid_boot_info();
    IOS_TEST_ASSERT_STATUS(validate_boot_info(&info), IOS_OK);
}

static void test_version_checksum_and_reserved_fields_are_enforced(void)
{
    struct ios_boot_info info = valid_boot_info();
    info.version = 2;
    IOS_TEST_ASSERT_STATUS(validate_boot_info(&info), IOS_ERROR(IOS_E_UNSUPPORTED_VERSION));

    info = valid_boot_info();
    info.checksum ^= 1;
    IOS_TEST_ASSERT_STATUS(validate_boot_info(&info), IOS_ERROR(IOS_E_CORRUPT));

    info = valid_boot_info();
    info.reserved2 = 1;
    seal_boot_info(&info);
    IOS_TEST_ASSERT_STATUS(validate_boot_info(&info), IOS_ERROR(IOS_E_CORRUPT));
}

static void test_memory_map_and_module_bounds_are_enforced(void)
{
    struct ios_boot_info info = valid_boot_info();
    info.memory_descriptor_size = TEST_MINIMUM_DESCRIPTOR_SIZE - 1;
    seal_boot_info(&info);
    IOS_TEST_ASSERT_STATUS(validate_boot_info(&info), IOS_ERROR(IOS_E_PROTOCOL));

    info = valid_boot_info();
    info.module_descriptors_address = UINT64_MAX - 8;
    info.module_descriptor_size = 80;
    seal_boot_info(&info);
    IOS_TEST_ASSERT_STATUS(validate_boot_info(&info), IOS_ERROR(IOS_E_PROTOCOL));
}

static void test_reference_gop_mode_and_degraded_cui_handoff(void)
{
    struct ios_boot_info info = valid_boot_info();
    info.framebuffer_format = 2;
    seal_boot_info(&info);
    IOS_TEST_ASSERT_STATUS(validate_boot_info(&info), IOS_ERROR(IOS_E_NOT_SUPPORTED));

    info = valid_boot_info();
    info.flags = IOS_BOOT_FLAG_GUI_UNAVAILABLE;
    info.framebuffer_address = 0;
    info.framebuffer_size = 0;
    info.framebuffer_width = 0;
    info.framebuffer_height = 0;
    info.framebuffer_stride = 0;
    info.framebuffer_format = 0;
    seal_boot_info(&info);
    IOS_TEST_ASSERT_STATUS(validate_boot_info(&info), IOS_OK);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_valid_versioned_handoff_is_accepted),
    IOS_TEST_CASE(test_version_checksum_and_reserved_fields_are_enforced),
    IOS_TEST_CASE(test_memory_map_and_module_bounds_are_enforced),
    IOS_TEST_CASE(test_reference_gop_mode_and_degraded_cui_handoff)
};

const size_t ios_test_case_count = sizeof(ios_test_cases) / sizeof(ios_test_cases[0]);
