#ifndef INFERENCEOS_DRIVERS_FRAMEBUFFER_H
#define INFERENCEOS_DRIVERS_FRAMEBUFFER_H

#include <inferenceos/boot_info.h>
#include <inferenceos/gui/graphics.h>

ios_status ios_gop_framebuffer_open(
    const struct ios_boot_info *boot_info,
    struct ios_graphics_surface *surface
);

#endif
