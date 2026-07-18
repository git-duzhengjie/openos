#include "aarch64_gicv3.h"

static aarch64_gicv3_state_t g_gicv3_state = {0};

static inline uint32_t mmio_read32(uint64_t addr)
{
    return *((volatile uint32_t *)(uintptr_t)addr);
}

static inline void mmio_write32(uint64_t addr, uint32_t value)
{
    *((volatile uint32_t *)(uintptr_t)addr) = value;
}

/* ICC_* system register accessors via MSR/MRS */
static inline void gic_write_igrpen1(uint32_t value)
{
    __asm__ volatile("msr S3_0_C12_C12_7, %0" : : "r"((uint64_t)value));
}

static inline void gic_write_pmr(uint32_t value)
{
    __asm__ volatile("msr S3_0_C4_C6_0, %0" : : "r"((uint64_t)value));
}

static inline void gic_write_sre(uint32_t value)
{
    __asm__ volatile("msr S3_0_C12_C12_5, %0" : : "r"((uint64_t)value));
    __asm__ volatile("isb");
}

static inline uint32_t gic_read_iar1(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, S3_0_C12_C12_0" : "=r"(v));
    return (uint32_t)v;
}

static inline void gic_write_eoir1(uint32_t value)
{
    __asm__ volatile("msr S3_0_C12_C12_1, %0" : : "r"((uint64_t)value));
}

int aarch64_gicv3_init(uint64_t gicd_base, uint64_t gicr_base)
{
    g_gicv3_state.gicd_base = gicd_base;
    g_gicv3_state.gicr_base = gicr_base;
    g_gicv3_state.irq_enabled_count = 0;
    g_gicv3_state.irq_ack_count = 0;
    g_gicv3_state.irq_eoi_count = 0;

    /* GICD_TYPER.ITLinesNumber -> max SPI = 32 * (ITLinesNumber + 1) - 1 */
    uint32_t typer = mmio_read32(gicd_base + GICD_TYPER);
    uint32_t it_lines = typer & 0x1fU;
    g_gicv3_state.max_spi = 32u * (it_lines + 1u) - 1u;

    /* Enable affinity routing (ARE_NS) and Group 1 NS */
    mmio_write32(gicd_base + GICD_CTLR, (1u << 4) | (1u << 1));

    /* Wake up redistributor: clear ProcessorSleep, wait ChildrenAsleep */
    uint32_t waker = mmio_read32(gicr_base + GICR_WAKER);
    waker &= ~(1u << 1);
    mmio_write32(gicr_base + GICR_WAKER, waker);
    for (int spin = 0; spin < 1000000; spin++) {
        if ((mmio_read32(gicr_base + GICR_WAKER) & (1u << 2)) == 0) {
            break;
        }
    }

    /* SGI/PPI: set group 1 for the SGI frame */
    mmio_write32(gicr_base + GICR_IGROUPR0, 0xffffffffU);

    /* CPU interface: enable system register access, set priority mask, group 1 */
    gic_write_sre(0x7);
    gic_write_pmr(0xff);
    gic_write_igrpen1(0x1);

    g_gicv3_state.ready = 1;
    return 0;
}

int aarch64_gicv3_enable_irq(uint32_t irq, uint8_t priority)
{
    if (!g_gicv3_state.ready) {
        return -1;
    }
    if (irq < 32) {
        /* SGI/PPI on redistributor */
        uint64_t base = g_gicv3_state.gicr_base;
        mmio_write32(base + GICR_IPRIORITYR(irq / 4)
                     + 0, (uint32_t)priority << ((irq % 4) * 8));
        /* Configure PPI (16-31) as edge/level? default level for timer PPI 30 (edge on virt cntv) */
        if (irq >= 16 && irq < 32) {
            uint32_t cfg = mmio_read32(base + GICR_ICFGR1);
            /* leave defaults */
            (void)cfg;
        }
        mmio_write32(base + GICR_ISENABLER0, 1u << irq);
    } else {
        /* SPI on distributor */
        uint64_t base = g_gicv3_state.gicd_base;
        uint32_t idx = irq / 4;
        uint32_t shift = (irq % 4) * 8;
        uint32_t pri = mmio_read32(base + GICD_IPRIORITYR(idx));
        pri &= ~(0xffu << shift);
        pri |= ((uint32_t)priority) << shift;
        mmio_write32(base + GICD_IPRIORITYR(idx), pri);
        mmio_write32(base + GICD_IGROUPR(irq / 32), 0xffffffffU);
        mmio_write32(base + GICD_ISENABLER(irq / 32), 1u << (irq % 32));
    }
    g_gicv3_state.irq_enabled_count++;
    return 0;
}

int aarch64_gicv3_disable_irq(uint32_t irq)
{
    if (!g_gicv3_state.ready) {
        return -1;
    }
    if (irq < 32) {
        mmio_write32(g_gicv3_state.gicr_base + GICR_ICENABLER0, 1u << irq);
    } else {
        mmio_write32(g_gicv3_state.gicd_base + GICD_ICENABLER(irq / 32), 1u << (irq % 32));
    }
    return 0;
}

uint32_t aarch64_gicv3_ack_irq(void)
{
    uint32_t iar = gic_read_iar1();
    g_gicv3_state.irq_ack_count++;
    return iar & 0xffffffU;
}

int aarch64_gicv3_eoi_irq(uint32_t irq)
{
    gic_write_eoir1(irq);
    g_gicv3_state.irq_eoi_count++;
    return 0;
}

const aarch64_gicv3_state_t *aarch64_gicv3_get_state(void)
{
    return &g_gicv3_state;
}
