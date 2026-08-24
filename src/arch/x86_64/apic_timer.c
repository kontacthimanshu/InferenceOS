#include <inferenceos/arch/io.h>
#include <inferenceos/arch/platform.h>

enum {
    APIC_ID = 0x020,
    APIC_EOI = 0x0b0,
    APIC_SPURIOUS = 0x0f0,
    APIC_LVT_TIMER = 0x320,
    APIC_TIMER_INITIAL = 0x380,
    APIC_TIMER_CURRENT = 0x390,
    APIC_TIMER_DIVIDE = 0x3e0,
    APIC_SOFTWARE_ENABLE = 0x100,
    APIC_TIMER_PERIODIC = 0x20000,
    APIC_DIVIDE_BY_16 = 0x3,
    ACPI_PM_TIMER_HZ = 3579545,
    CALIBRATION_TIMEOUT_READS = 20000000
};

static volatile ios_u32 *local_apic;
static ios_u32 quantum_ticks;

static ios_u32 apic_read(ios_u32 offset)
{
    return local_apic[offset / sizeof(ios_u32)];
}

static void apic_write(ios_u32 offset, ios_u32 value)
{
    local_apic[offset / sizeof(ios_u32)] = value;
    (void)apic_read(APIC_ID);
}

static ios_u32 pm_timer_read(const struct x86_64_platform_info *platform)
{
    const ios_u32 mask = platform->pm_timer_width == 32
        ? UINT32_MAX : UINT32_C(0x00ffffff);
    return x86_64_port_read32(platform->pm_timer_port) & mask;
}

static ios_u32 pm_timer_elapsed(ios_u32 start, ios_u32 current, ios_u8 width)
{
    const ios_u32 mask = width == 32 ? UINT32_MAX : UINT32_C(0x00ffffff);
    return (current - start) & mask;
}

ios_status x86_64_apic_timer_initialize(const struct x86_64_platform_info *platform)
{
    const ios_u32 target_pm_ticks =
        (ACPI_PM_TIMER_HZ * X86_64_SCHEDULER_QUANTUM_MS + 999U) / 1000U;
    ios_u32 start;
    ios_u32 reads = 0;
    ios_u32 elapsed;

    if (platform == NULL || platform->local_apic_address == 0
        || platform->pm_timer_port == 0
        || (platform->pm_timer_width != 24 && platform->pm_timer_width != 32)) {
        return IOS_ERROR(IOS_E_INVALID_ARGUMENT);
    }
    local_apic = (volatile ios_u32 *)platform->local_apic_address;
    if ((ios_u8)(apic_read(APIC_ID) >> 24) != platform->bootstrap_apic_id) {
        local_apic = NULL;
        return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    }
    apic_write(APIC_SPURIOUS, APIC_SOFTWARE_ENABLE | 0xffU);
    apic_write(APIC_TIMER_DIVIDE, APIC_DIVIDE_BY_16);
    apic_write(APIC_LVT_TIMER, UINT32_C(1) << 16);
    apic_write(APIC_TIMER_INITIAL, UINT32_MAX);
    start = pm_timer_read(platform);
    do {
        elapsed = pm_timer_elapsed(start, pm_timer_read(platform), platform->pm_timer_width);
        ++reads;
    } while (elapsed < target_pm_ticks && reads < CALIBRATION_TIMEOUT_READS);
    if (elapsed < target_pm_ticks) {
        apic_write(APIC_TIMER_INITIAL, 0);
        local_apic = NULL;
        return IOS_ERROR(IOS_E_TIMEOUT);
    }
    quantum_ticks = UINT32_MAX - apic_read(APIC_TIMER_CURRENT);
    if (quantum_ticks == 0) {
        local_apic = NULL;
        return IOS_ERROR(IOS_E_NOT_SUPPORTED);
    }
    apic_write(APIC_LVT_TIMER, APIC_TIMER_PERIODIC | X86_64_APIC_TIMER_VECTOR);
    apic_write(APIC_TIMER_INITIAL, quantum_ticks);
    return IOS_OK;
}

void x86_64_apic_timer_acknowledge(void)
{
    IOS_ASSERT(local_apic != NULL);
    apic_write(APIC_EOI, 0);
}

ios_u32 x86_64_apic_timer_ticks_per_quantum(void)
{
    return quantum_ticks;
}
