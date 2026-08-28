#ifndef INFERENCEOS_BOOT_INFO_H
#define INFERENCEOS_BOOT_INFO_H

#include <inferenceos/base.h>
#include <inferenceos/system_module.h>

enum {
    IOS_BOOT_INFO_VERSION = 3,
    IOS_BOOT_FRAMEBUFFER_BGRX8888 = 1,
    IOS_BOOT_FLAG_GUI_UNAVAILABLE = UINT32_C(1) << 0
};

struct ios_boot_info {
    ios_u32 structure_size;
    ios_u16 version;
    ios_u16 checksum;
    ios_u32 flags;
    ios_u32 reserved0;
    ios_uptr memory_map_address;
    ios_u64 memory_map_count;
    ios_u64 memory_descriptor_size;
    ios_u64 memory_map_key;
    ios_u32 memory_descriptor_version;
    ios_u32 reserved1;
    ios_uptr framebuffer_address;
    ios_u64 framebuffer_size;
    ios_u32 framebuffer_width;
    ios_u32 framebuffer_height;
    ios_u32 framebuffer_stride;
    ios_u32 framebuffer_format;
    ios_uptr module_descriptors_address;
    ios_u32 module_descriptor_count;
    ios_u32 module_descriptor_size;
    ios_uptr esp_device_handle;
    ios_uptr root_system_description_pointer;
    ios_uptr runtime_services;
    ios_u8 boot_partition_guid[16];
};

IOS_STATIC_ASSERT(sizeof(struct ios_boot_info) == 144, "boot information ABI size");

#endif
