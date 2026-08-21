#ifndef INFERENCEOS_FRAMEBUFFER_H
#define INFERENCEOS_FRAMEBUFFER_H

#include <inferenceos/base.h>
#include <inferenceos/result.h>

#define INFERENCEOS_FRAMEBUFFER_GLYPH_WIDTH 8U
#define INFERENCEOS_FRAMEBUFFER_GLYPH_HEIGHT 16U

typedef struct inferenceos_framebuffer_masks {
    inferenceos_u32 red;
    inferenceos_u32 green;
    inferenceos_u32 blue;
    inferenceos_u32 reserved;
} inferenceos_framebuffer_masks;

typedef struct inferenceos_framebuffer_config {
    inferenceos_u64 base;
    inferenceos_u64 size;
    inferenceos_u32 width;
    inferenceos_u32 height;
    inferenceos_u32 pixels_per_scan_line;
    inferenceos_u32 pixel_format;
    inferenceos_framebuffer_masks masks;
} inferenceos_framebuffer_config;

inferenceos_result inferenceos_framebuffer_initialize(
    const inferenceos_framebuffer_config *config
);
bool inferenceos_framebuffer_is_available(void);
inferenceos_result inferenceos_framebuffer_write_byte(inferenceos_u8 byte);
inferenceos_result inferenceos_framebuffer_clear(void);

#endif
