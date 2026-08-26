#include <inferenceos/test.h>

#include <inferenceos/base.h>
#include <inferenceos/boot.h>
#include <inferenceos/errors.h>

#include <string.h>

struct test_memory_descriptor {
    ios_u32 type;
    ios_u32 padding;
    ios_u64 physical_start;
    ios_u64 virtual_start;
    ios_u64 page_count;
    ios_u64 attributes;
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

static struct ios_boot_info valid_boot_info(void)
{
    static ios_u8 root_system_description_pointer[36];
    static struct test_memory_descriptor memory_map[2] = {
        { .type = 7, .physical_start = UINT64_C(0x100000), .page_count = 256 },
        { .type = 9, .physical_start = UINT64_C(0x200000), .page_count = 16 }
    };
    static ios_u8 shell_image[4096];
    static ios_u8 desktop_image[4096];
    static struct ios_system_module_descriptor modules[2];
    struct ios_boot_info info = {
        .structure_size = sizeof(struct ios_boot_info),
        .version = IOS_BOOT_INFO_VERSION,
        .memory_map_address = (ios_uptr)memory_map,
        .memory_map_count = 2,
        .memory_descriptor_size = sizeof(struct test_memory_descriptor),
        .memory_descriptor_version = IOS_BOOT_MEMORY_DESCRIPTOR_VERSION,
        .framebuffer_address = UINT64_C(0x80000000),
        .framebuffer_size = UINT64_C(1024) * 768 * sizeof(ios_u32),
        .framebuffer_width = 1024,
        .framebuffer_height = 768,
        .framebuffer_stride = 1024,
        .framebuffer_format = IOS_BOOT_FRAMEBUFFER_BGRX8888,
        .module_descriptors_address = (ios_uptr)modules,
        .module_descriptor_count = 2,
        .module_descriptor_size = sizeof(*modules),
        .esp_device_handle = 1,
        .root_system_description_pointer = (ios_uptr)root_system_description_pointer
    };

    memset(modules, 0, sizeof(modules));
    modules[0].structure_size = sizeof(modules[0]);
    modules[0].structure_version = IOS_SYSTEM_MODULE_ABI_VERSION;
    modules[0].application_identity = 1;
    modules[0].role = IOS_MODULE_ROLE_SHELL;
    modules[0].flags = IOS_SYSTEM_MODULE_REQUIRED;
    modules[0].image_address = (ios_uptr)shell_image;
    modules[0].image_size = sizeof(shell_image);
    modules[0].entry_abi_version = IOS_SYSTEM_MODULE_ABI_VERSION;
    modules[1].structure_size = sizeof(modules[1]);
    modules[1].structure_version = IOS_SYSTEM_MODULE_ABI_VERSION;
    modules[1].application_identity = 2;
    modules[1].role = IOS_MODULE_ROLE_GUI_DESKTOP;
    modules[1].image_address = (ios_uptr)desktop_image;
    modules[1].image_size = sizeof(desktop_image);
    modules[1].entry_abi_version = IOS_SYSTEM_MODULE_ABI_VERSION;
    seal_boot_info(&info);
    return info;
}

static void test_valid_versioned_handoff_is_accepted(void)
{
    struct ios_boot_info info = valid_boot_info();
    IOS_TEST_ASSERT_STATUS(ios_boot_info_validate(&info), IOS_OK);
}

static void test_version_checksum_and_reserved_fields_are_enforced(void)
{
    struct ios_boot_info info = valid_boot_info();
    info.version = IOS_BOOT_INFO_VERSION + 1;
    IOS_TEST_ASSERT_STATUS(
        ios_boot_info_validate(&info), IOS_ERROR(IOS_E_UNSUPPORTED_VERSION));

    info = valid_boot_info();
    info.checksum ^= 1;
    IOS_TEST_ASSERT_STATUS(ios_boot_info_validate(&info), IOS_ERROR(IOS_E_CORRUPT));

    info = valid_boot_info();
    info.root_system_description_pointer = 0;
    seal_boot_info(&info);
    IOS_TEST_ASSERT_STATUS(ios_boot_info_validate(&info), IOS_ERROR(IOS_E_PROTOCOL));
}

static void test_memory_map_and_module_bounds_are_enforced(void)
{
    struct ios_boot_info info = valid_boot_info();
    info.memory_descriptor_size = IOS_BOOT_MEMORY_DESCRIPTOR_MINIMUM_SIZE - 8;
    seal_boot_info(&info);
    IOS_TEST_ASSERT_STATUS(ios_boot_info_validate(&info), IOS_ERROR(IOS_E_PROTOCOL));

    info = valid_boot_info();
    info.module_descriptors_address = UINT64_MAX - 8;
    info.module_descriptor_size = sizeof(struct ios_system_module_descriptor);
    seal_boot_info(&info);
    IOS_TEST_ASSERT_STATUS(ios_boot_info_validate(&info), IOS_ERROR(IOS_E_PROTOCOL));
}

static void test_reference_gop_mode_and_degraded_cui_handoff(void)
{
    struct ios_boot_info info = valid_boot_info();
    info.framebuffer_format = 2;
    seal_boot_info(&info);
    IOS_TEST_ASSERT_STATUS(ios_boot_info_validate(&info), IOS_ERROR(IOS_E_NOT_SUPPORTED));

    info = valid_boot_info();
    info.flags = IOS_BOOT_FLAG_GUI_UNAVAILABLE;
    info.framebuffer_address = 0;
    info.framebuffer_size = 0;
    info.framebuffer_width = 0;
    info.framebuffer_height = 0;
    info.framebuffer_stride = 0;
    info.framebuffer_format = 0;
    seal_boot_info(&info);
    IOS_TEST_ASSERT_STATUS(ios_boot_info_validate(&info), IOS_OK);
}

static void test_uefi_memory_map_is_converted_without_reclaiming_loader_memory(void)
{
    struct ios_boot_info info = valid_boot_info();
    struct ios_physical_memory_region regions[2];
    ios_size count = 0;

    IOS_TEST_ASSERT_STATUS(
        ios_boot_build_memory_regions(&info, regions, IOS_ARRAY_COUNT(regions), &count),
        IOS_OK
    );
    IOS_TEST_ASSERT(count == 2);
    IOS_TEST_ASSERT(regions[0].base == UINT64_C(0x100000));
    IOS_TEST_ASSERT(regions[0].page_count == 256);
    IOS_TEST_ASSERT(regions[0].kind == IOS_PHYSICAL_USABLE);
    IOS_TEST_ASSERT(regions[1].kind == IOS_PHYSICAL_RECLAIMABLE);
}

const struct ios_test_case ios_test_cases[] = {
    IOS_TEST_CASE(test_valid_versioned_handoff_is_accepted),
    IOS_TEST_CASE(test_version_checksum_and_reserved_fields_are_enforced),
    IOS_TEST_CASE(test_memory_map_and_module_bounds_are_enforced),
    IOS_TEST_CASE(test_reference_gop_mode_and_degraded_cui_handoff),
    IOS_TEST_CASE(test_uefi_memory_map_is_converted_without_reclaiming_loader_memory)
};

const size_t ios_test_case_count = sizeof(ios_test_cases) / sizeof(ios_test_cases[0]);
