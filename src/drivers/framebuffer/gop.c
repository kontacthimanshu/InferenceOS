#include <inferenceos/drivers/framebuffer.h>

ios_status ios_gop_framebuffer_open(
    const struct ios_boot_info *boot_info,
    struct ios_graphics_surface *surface
)
{
    ios_u64 required_pixels;
    ios_u64 required_bytes;

    if (boot_info == NULL || surface == NULL) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    *surface = (struct ios_graphics_surface){ 0 };
    if (boot_info->structure_size < sizeof(*boot_info)
        || boot_info->version != IOS_BOOT_INFO_VERSION) {
        return IOS_ERROR(IOS_E_UNSUPPORTED_VERSION);
    }
    if (boot_info->framebuffer_address == 0) {
        return IOS_ERROR(IOS_E_BAD_ADDRESS);
    }
    if (boot_info->framebuffer_format != IOS_BOOT_FRAMEBUFFER_BGRX8888) {
        return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    }
    if (boot_info->framebuffer_width == 0 || boot_info->framebuffer_height == 0
        || boot_info->framebuffer_stride < boot_info->framebuffer_width) {
        return IOS_ERROR(IOS_E_OUT_OF_RANGE);
    }
    required_pixels = (ios_u64)boot_info->framebuffer_stride
        * (ios_u64)boot_info->framebuffer_height;
    if (required_pixels > UINT64_MAX / sizeof(ios_u32)) {
        return IOS_ERROR(IOS_E_OVERFLOW);
    }
    required_bytes = required_pixels * sizeof(ios_u32);
    if (required_bytes > boot_info->framebuffer_size) {
        return IOS_ERROR(IOS_E_OUT_OF_RANGE);
    }

    surface->pixels = (ios_u32 *)boot_info->framebuffer_address;
    surface->width = boot_info->framebuffer_width;
    surface->height = boot_info->framebuffer_height;
    surface->stride = boot_info->framebuffer_stride;
    return IOS_OK;
}
