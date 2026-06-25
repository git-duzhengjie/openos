#include "aarch64_platform.h"
#include "aarch64_bootinfo.h"
#include "aarch64_uart.h"

#define FDT_MAGIC 0xd00dfeedU

static aarch64_platform_state_t g_aarch64_platform_state;

static uint32_t aarch64_be32_to_cpu(uint32_t value)
{
    return ((value & 0x000000ffU) << 24) |
           ((value & 0x0000ff00U) << 8) |
           ((value & 0x00ff0000U) >> 8) |
           ((value & 0xff000000U) >> 24);
}

static void aarch64_mmio_write32(uint64_t address, uint32_t value)
{
    volatile uint32_t *reg = (volatile uint32_t *)(uintptr_t)address;
    *reg = value;
}

void aarch64_gic_init(void)
{
    /* Minimal GICv2 bring-up for QEMU virt: keep all SPIs disabled for now,
     * enable distributor and CPU interface so later IRQ routing can be added
     * without changing platform_ops wiring. */
    aarch64_mmio_write32(AARCH64_QEMU_VIRT_GICD_BASE + 0x000, 0x0U);
    aarch64_mmio_write32(AARCH64_QEMU_VIRT_GICC_BASE + 0x004, 0xffU);
    aarch64_mmio_write32(AARCH64_QEMU_VIRT_GICC_BASE + 0x000, 0x1U);
    aarch64_mmio_write32(AARCH64_QEMU_VIRT_GICD_BASE + 0x000, 0x1U);
    g_aarch64_platform_state.gic_ready = 1;
}

void aarch64_timer_init(void)
{
    uint32_t frequency = aarch64_timer_read_frequency();
    g_aarch64_platform_state.timer_frequency_hz = frequency;
    g_aarch64_platform_state.timer_ready = (frequency != 0U);
}

uint64_t aarch64_timer_read_counter(void)
{
    uint64_t value;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(value));
    return value;
}

uint32_t aarch64_timer_read_frequency(void)
{
    uint64_t value;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(value));
    return (uint32_t)value;
}

void aarch64_psci_init(void)
{
    g_aarch64_platform_state.psci_ready = 1;
}

static long aarch64_psci_call(uint32_t function_id)
{
    register uint64_t x0 __asm__("x0") = function_id;
    __asm__ volatile("smc #0" : "+r"(x0) : : "x1", "x2", "x3", "memory");
    return (long)x0;
}

int aarch64_psci_system_off(void)
{
    if (!g_aarch64_platform_state.psci_ready) {
        return -1;
    }
    (void)aarch64_psci_call(0x84000008U);
    return -1;
}

int aarch64_psci_system_reset(void)
{
    if (!g_aarch64_platform_state.psci_ready) {
        return -1;
    }
    (void)aarch64_psci_call(0x84000009U);
    return -1;
}

aarch64_dtb_info_t aarch64_dtb_probe(uint64_t dtb_base)
{
    aarch64_dtb_info_t info;
    info.base = dtb_base;
    info.size = 0;
    info.valid = 0;

    if (!dtb_base) {
        return info;
    }

    const uint32_t *header = (const uint32_t *)(uintptr_t)dtb_base;
    uint32_t magic = aarch64_be32_to_cpu(header[0]);
    if (magic != FDT_MAGIC) {
        return info;
    }

    info.size = aarch64_be32_to_cpu(header[1]);
    info.valid = info.size >= 40U;
    return info;
}

void aarch64_platform_init(uint64_t dtb_base)
{
    g_aarch64_platform_state.dtb_base = dtb_base;
    aarch64_dtb_info_t dtb = aarch64_dtb_probe(dtb_base);
    g_aarch64_platform_state.dtb_ready = dtb.valid;

    aarch64_psci_init();
    aarch64_timer_init();
    aarch64_gic_init();

    aarch64_uart_write("A5.4: ARM platform init ");
    aarch64_uart_write(g_aarch64_platform_state.dtb_ready ? "DTB OK" : "DTB unavailable");
    aarch64_uart_write("\n");
}

const aarch64_platform_state_t *aarch64_platform_get_state(void)
{
    return &g_aarch64_platform_state;
}

const openos_bootinfo_t *aarch64_platform_bootinfo_from_dtb(uint64_t dtb_base)
{
    aarch64_dtb_info_t dtb = aarch64_dtb_probe(dtb_base);
    aarch64_boot_stub_args_t args;

    args.device_tree_base = dtb.valid ? dtb.base : 0U;
    args.device_tree_size = dtb.valid ? dtb.size : 0U;
    args.initrd_base = 0U;
    args.initrd_size = 0U;
    args.cmdline = 0U;
    args.cmdline_size = 0U;
    args.kernel_phys_start = OPENOS_AARCH64_BOOTINFO_DEFAULT_KERNEL_BASE;
    args.kernel_phys_end = 0U;

    return arch_aarch64_bootinfo_from_stub(&args);
}
