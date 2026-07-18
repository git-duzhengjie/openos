#include "aarch64_platform.h"
#include "aarch64_bootinfo.h"
#include "aarch64_uart.h"
#include "aarch64_dtb.h"
#include "aarch64_gicv3.h"

/* Cached DTB parser state (populated by platform_init, consumed by selftest). */
static aarch64_dtb_parser_t          g_aarch64_dtb_parser;
static aarch64_dtb_memory_info_t     g_aarch64_dtb_memory;
static aarch64_dtb_chosen_info_t     g_aarch64_dtb_chosen;
static uint8_t                       g_aarch64_dtb_parse_ok;

const aarch64_dtb_parser_t *aarch64_platform_dtb_parser(void)
{
    return g_aarch64_dtb_parse_ok ? &g_aarch64_dtb_parser : (const aarch64_dtb_parser_t *)0;
}

const aarch64_dtb_memory_info_t *aarch64_platform_dtb_memory(void)
{
    return g_aarch64_dtb_parse_ok ? &g_aarch64_dtb_memory : (const aarch64_dtb_memory_info_t *)0;
}

const aarch64_dtb_chosen_info_t *aarch64_platform_dtb_chosen(void)
{
    return g_aarch64_dtb_parse_ok ? &g_aarch64_dtb_chosen : (const aarch64_dtb_chosen_info_t *)0;
}

#define FDT_MAGIC 0xd00dfeedU

/* EL1 physical (non-secure) timer PPI on ARMv8 GIC:
 *   INTID 30 = CNTP (physical secure or non-secure per config)
 *   QEMU virt routes EL1 phys timer to INTID 30. */
#define AARCH64_TIMER_PPI_INTID  30u
#define AARCH64_TIMER_PRIORITY   0xA0u

static aarch64_platform_state_t g_aarch64_platform_state;

static uint32_t aarch64_be32_to_cpu(uint32_t value)
{
    return ((value & 0x000000ffU) << 24) |
           ((value & 0x0000ff00U) << 8) |
           ((value & 0x00ff0000U) >> 8) |
           ((value & 0xff000000U) >> 24);
}

void aarch64_gic_init(void)
{
    /* Prefer GICv3 (QEMU virt default since v2.10). */
    int rc = aarch64_gicv3_init(AARCH64_GICV3_GICD_BASE_DEFAULT,
                                AARCH64_GICV3_GICR_BASE_DEFAULT);
    g_aarch64_platform_state.gic_ready = (rc == 0) ? 1 : 0;

    if (rc == 0) {
        aarch64_uart_write("A5.4: GICv3 ready\n");
    } else {
        aarch64_uart_write("A5.4: GICv3 init failed\n");
    }
}

void aarch64_timer_init(void)
{
    uint32_t frequency = aarch64_timer_read_frequency();
    g_aarch64_platform_state.timer_frequency_hz = frequency;
    g_aarch64_platform_state.timer_ready = (frequency != 0U);

    /* Route EL1 physical timer PPI to the GIC (INTID 30). */
    if (g_aarch64_platform_state.gic_ready) {
        (void)aarch64_gicv3_enable_irq(AARCH64_TIMER_PPI_INTID,
                                       AARCH64_TIMER_PRIORITY);
        g_aarch64_platform_state.timer_irq = AARCH64_TIMER_PPI_INTID;
    }
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

    /* Deep DTB parse (memory range, /chosen bootargs, initrd, model). */
    g_aarch64_dtb_parse_ok = 0;
    if (dtb.valid && aarch64_dtb_parser_init(&g_aarch64_dtb_parser, dtb_base) == 0) {
        (void)aarch64_dtb_parse_memory(&g_aarch64_dtb_parser, &g_aarch64_dtb_memory);
        (void)aarch64_dtb_parse_chosen(&g_aarch64_dtb_parser, &g_aarch64_dtb_chosen);
        g_aarch64_dtb_parse_ok = 1;
    }

    aarch64_psci_init();
    aarch64_gic_init();
    aarch64_timer_init();

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
