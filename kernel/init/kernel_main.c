#include <inferenceos/arch/x86_64/cpu.h>
#include <inferenceos/boot.h>
#include <inferenceos/console.h>
#include <inferenceos/device_registry.h>
#include <inferenceos/heap.h>
#include <inferenceos/keyboard.h>
#include <inferenceos/memory.h>
#include <inferenceos/page_allocator.h>
#include <inferenceos/panic.h>
#include <inferenceos/serial.h>
#include <inferenceos/shell.h>

#define KERNEL_PAGE_BITMAP_CAPACITY 32768U
#define KERNEL_HEAP_PAGE_COUNT UINT64_C(16)

static inferenceos_page_allocator kernel_page_allocator;
static inferenceos_heap kernel_heap;
INFERENCEOS_ALIGNED(16) static inferenceos_u8
    kernel_page_bitmap[KERNEL_PAGE_BITMAP_CAPACITY];

static void early_serial_literal(const char *text, inferenceos_size capacity)
{
    (void)inferenceos_serial_write_bounded_string(text, capacity);
}

static bool handoff_is_valid(const inferenceos_uefi_handoff *handoff)
{
    inferenceos_u64 map_end;

    return handoff != NULL
        && handoff->magic == INFERENCEOS_UEFI_HANDOFF_MAGIC
        && handoff->version == INFERENCEOS_UEFI_HANDOFF_VERSION
        && handoff->size == sizeof(*handoff)
        && handoff->reserved == 0U
        && handoff->memory_map != 0U
        && handoff->memory_map_size != 0U
        && handoff->memory_descriptor_size
            >= sizeof(inferenceos_uefi_memory_descriptor)
        && handoff->memory_descriptor_version
            == INFERENCEOS_UEFI_MEMORY_DESCRIPTOR_VERSION
        && handoff->memory_map_size % handoff->memory_descriptor_size == 0U
        && inferenceos_checked_add_u64(handoff->memory_map,
            handoff->memory_map_size, &map_end);
}

static inferenceos_result select_memory_region(
    const inferenceos_uefi_handoff *handoff,
    inferenceos_u64 *base,
    inferenceos_u64 *length
)
{
    const inferenceos_u8 *const map =
        (const inferenceos_u8 *)(inferenceos_uptr)handoff->memory_map;
    const inferenceos_u64 descriptor_count =
        handoff->memory_map_size / handoff->memory_descriptor_size;
    inferenceos_u64 best_base = 0U;
    inferenceos_u64 best_length = 0U;

    if (base == NULL || length == NULL) {
        return INFERENCEOS_RESULT_INVALID_ARGUMENT;
    }
    for (inferenceos_u64 index = 0U; index < descriptor_count; ++index) {
        inferenceos_uefi_memory_descriptor descriptor;
        inferenceos_u64 offset;
        inferenceos_u64 byte_length;
        inferenceos_u64 region_end;
        inferenceos_size bitmap_size;

        if (!inferenceos_checked_mul_u64(index,
                handoff->memory_descriptor_size, &offset)
            || offset > SIZE_MAX) {
            return INFERENCEOS_RESULT_OVERFLOW;
        }
        (void)memcpy(&descriptor, map + (inferenceos_size)offset,
            sizeof(descriptor));
        if (descriptor.type != INFERENCEOS_UEFI_CONVENTIONAL_MEMORY
            || descriptor.physical_start % INFERENCEOS_PAGE_SIZE != 0U
            || descriptor.page_count < KERNEL_HEAP_PAGE_COUNT
            || !inferenceos_checked_mul_u64(descriptor.page_count,
                INFERENCEOS_PAGE_SIZE, &byte_length)
            || !inferenceos_checked_add_u64(descriptor.physical_start,
                byte_length, &region_end)
            || !inferenceos_result_is_success(
                inferenceos_page_allocator_bitmap_size(
                    descriptor.page_count, &bitmap_size))
            || bitmap_size > sizeof(kernel_page_bitmap)) {
            continue;
        }
        (void)region_end;
        if (byte_length > best_length) {
            best_base = descriptor.physical_start;
            best_length = byte_length;
        }
    }
    if (best_length == 0U) {
        return INFERENCEOS_RESULT_NO_SPACE;
    }
    *base = best_base;
    *length = best_length;
    return INFERENCEOS_RESULT_OK;
}

static inferenceos_result initialize_memory(
    const inferenceos_uefi_handoff *handoff
)
{
    inferenceos_u64 region_base;
    inferenceos_u64 region_length;
    inferenceos_u64 heap_address;
    inferenceos_u64 heap_size;
    inferenceos_result result = select_memory_region(
        handoff, &region_base, &region_length
    );

    if (!inferenceos_result_is_success(result)) {
        return result;
    }
    result = inferenceos_page_allocator_initialize(
        &kernel_page_allocator, region_base, region_length,
        kernel_page_bitmap, sizeof(kernel_page_bitmap)
    );
    if (!inferenceos_result_is_success(result)) {
        return result;
    }
    result = inferenceos_page_allocator_allocate(
        &kernel_page_allocator, KERNEL_HEAP_PAGE_COUNT, 1U, &heap_address
    );
    if (!inferenceos_result_is_success(result)
        || !inferenceos_checked_mul_u64(KERNEL_HEAP_PAGE_COUNT,
            INFERENCEOS_PAGE_SIZE, &heap_size)
        || heap_size > SIZE_MAX) {
        return inferenceos_result_is_success(result)
            ? INFERENCEOS_RESULT_OVERFLOW
            : result;
    }
    return inferenceos_heap_initialize(
        &kernel_heap, (void *)(inferenceos_uptr)heap_address,
        (inferenceos_size)heap_size
    );
}

static inferenceos_framebuffer_config framebuffer_from_handoff(
    const inferenceos_uefi_handoff *handoff
)
{
    return (inferenceos_framebuffer_config) {
        .base = handoff->framebuffer_base,
        .size = handoff->framebuffer_size,
        .width = handoff->horizontal_resolution,
        .height = handoff->vertical_resolution,
        .pixels_per_scan_line = handoff->pixels_per_scan_line,
        .pixel_format = handoff->pixel_format,
        .masks = handoff->pixel_masks
    };
}

INFERENCEOS_NORETURN void inferenceos_kernel_main(
    const inferenceos_uefi_handoff *handoff
)
{
    inferenceos_framebuffer_config framebuffer_config;
    inferenceos_result result;

    (void)inferenceos_serial_initialize();
    early_serial_literal("BOOT: kernel entry\r\n", sizeof("BOOT: kernel entry\r\n"));
    if (!handoff_is_valid(handoff)) {
        inferenceos_panic("invalid UEFI handoff");
    }

    inferenceos_arch_cpu_initialize();
    early_serial_literal("BOOT: CPU tables ready\r\n",
        sizeof("BOOT: CPU tables ready\r\n"));

    result = initialize_memory(handoff);
    if (!inferenceos_result_is_success(result)) {
        inferenceos_panic("memory initialization failed");
    }
    early_serial_literal("BOOT: memory ready\r\n", sizeof("BOOT: memory ready\r\n"));

    framebuffer_config = framebuffer_from_handoff(handoff);
    result = inferenceos_console_initialize(
        handoff->framebuffer_base != 0U ? &framebuffer_config : NULL
    );
    if (!inferenceos_result_is_success(result)) {
        inferenceos_panic("console initialization failed");
    }
    (void)inferenceos_console_write_bounded_string(
        "InferenceOS minimal filesystem demonstrator\r\n",
        sizeof("InferenceOS minimal filesystem demonstrator\r\n")
    );
    (void)inferenceos_console_write_bounded_string(
        "BOOT: console ready\r\n", sizeof("BOOT: console ready\r\n")
    );

    result = inferenceos_ps2_keyboard_initialize();
    if (!inferenceos_result_is_success(result)) {
        inferenceos_panic("keyboard initialization failed");
    }
    (void)inferenceos_console_write_bounded_string(
        "BOOT: keyboard ready\r\n", sizeof("BOOT: keyboard ready\r\n")
    );

    result = inferenceos_device_registry_initialize();
    if (!inferenceos_result_is_success(result)) {
        inferenceos_panic("block device initialization failed");
    }
    (void)inferenceos_console_write_bounded_string(
        "BOOT: block devices ready\r\n",
        sizeof("BOOT: block devices ready\r\n")
    );

    result = inferenceos_shell_initialize();
    if (!inferenceos_result_is_success(result)) {
        inferenceos_panic("shell initialization failed");
    }
    (void)inferenceos_console_write_bounded_string(
        "BOOT: shell ready\r\n", sizeof("BOOT: shell ready\r\n")
    );
    inferenceos_shell_run();
}
