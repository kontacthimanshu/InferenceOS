#include <inferenceos/boot.h>

#include <inferenceos/arch/interrupts.h>
#include <inferenceos/drivers/serial.h>
#include <inferenceos/memory.h>
#include <inferenceos/kernel_runtime.h>
#include <inferenceos/panic.h>

extern ios_u8 __kernel_start[];
extern ios_u8 __kernel_end[];

static struct ios_physical_memory_region boot_regions[IOS_MEMORY_MAP_MAX_REGIONS];
static struct ios_physical_reservation boot_reservations[IOS_BOOT_RESERVATION_MAX_COUNT];

static _Noreturn void boot_failure(const char *stage, ios_status status)
{
    serial_write("INFERENCEOS:BOOT_FAILURE stage=");
    serial_write(stage);
    serial_write(" status=");
    serial_write_hex_u64((ios_u64)status);
    serial_write("\n");
    ios_panic(stage);
}

void kernel_main(const struct ios_boot_info *information)
{
    struct ios_physical_memory_statistics statistics;
    ios_size region_count;
    ios_size reservation_count;
    ios_status status;

    serial_write_line("INFERENCEOS:KERNEL_ENTRY");

    status = ios_boot_info_validate(information);
    if (IOS_FAILED(status)) {
        boot_failure("boot-info", status);
    }
    serial_write_line("INFERENCEOS:BOOT_INFO_VALID");

    status = ios_boot_build_memory_regions(
        information,
        boot_regions,
        IOS_ARRAY_COUNT(boot_regions),
        &region_count
    );
    if (IOS_FAILED(status)) {
        boot_failure("memory-map", status);
    }
    status = ios_boot_build_reservations(
        information,
        (ios_uptr)__kernel_start,
        (ios_u64)((ios_uptr)__kernel_end - (ios_uptr)__kernel_start),
        boot_reservations,
        IOS_ARRAY_COUNT(boot_reservations),
        &reservation_count
    );
    if (IOS_FAILED(status)) {
        boot_failure("memory-reservations", status);
    }
    status = physical_memory_initialize(
        boot_regions,
        region_count,
        boot_reservations,
        reservation_count
    );
    if (IOS_FAILED(status)) {
        boot_failure("physical-memory", status);
    }
    physical_memory_statistics(&statistics);
    serial_write("INFERENCEOS:PHYSICAL_MEMORY_READY free_pages=");
    serial_write_decimal_u64(statistics.free_pages);
    serial_write("\n");

    status = virtual_memory_initialize();
    if (IOS_FAILED(status)) {
        boot_failure("virtual-memory", status);
    }
    serial_write_line("INFERENCEOS:VIRTUAL_MEMORY_READY");

    if ((information->flags & IOS_BOOT_FLAG_GUI_UNAVAILABLE) != 0) {
        serial_write_line("INFERENCEOS:GUI_UNAVAILABLE");
    }
    serial_write_line("INFERENCEOS:KERNEL_FOUNDATION_READY");

    status = ios_kernel_runtime_initialize(information);
    if (IOS_FAILED(status)) {
        boot_failure("runtime", status);
    }
    ios_kernel_runtime_run();
}
