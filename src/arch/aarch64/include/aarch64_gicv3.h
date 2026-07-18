#ifndef OPENOS_ARCH_AARCH64_GICV3_H
#define OPENOS_ARCH_AARCH64_GICV3_H

#include <stdint.h>

/* QEMU virt GICv3 defaults */
#define AARCH64_GICV3_GICD_BASE_DEFAULT   0x08000000ULL
#define AARCH64_GICV3_GICR_BASE_DEFAULT   0x080A0000ULL

/* Distributor regs (offsets from GICD_BASE) */
#define GICD_CTLR                0x0000
#define GICD_TYPER               0x0004
#define GICD_IIDR                0x0008
#define GICD_IGROUPR(n)          (0x0080 + (n) * 4)
#define GICD_ISENABLER(n)        (0x0100 + (n) * 4)
#define GICD_ICENABLER(n)        (0x0180 + (n) * 4)
#define GICD_ISPENDR(n)          (0x0200 + (n) * 4)
#define GICD_ICPENDR(n)          (0x0280 + (n) * 4)
#define GICD_ISACTIVER(n)        (0x0300 + (n) * 4)
#define GICD_ICACTIVER(n)        (0x0380 + (n) * 4)
#define GICD_IPRIORITYR(n)       (0x0400 + (n) * 4)
#define GICD_ICFGR(n)            (0x0C00 + (n) * 4)

/* GICR SGI/PPI frame (offset 0x10000 from redistributor RD_base) */
#define GICR_WAKER               0x0014
#define GICR_SGI_OFFSET          0x10000
#define GICR_IGROUPR0            (GICR_SGI_OFFSET + 0x0080)
#define GICR_ISENABLER0          (GICR_SGI_OFFSET + 0x0100)
#define GICR_ICENABLER0          (GICR_SGI_OFFSET + 0x0180)
#define GICR_IPRIORITYR(n)       (GICR_SGI_OFFSET + 0x0400 + (n) * 4)
#define GICR_ICFGR0              (GICR_SGI_OFFSET + 0x0C00)
#define GICR_ICFGR1              (GICR_SGI_OFFSET + 0x0C04)

typedef struct aarch64_gicv3_state {
    uint64_t gicd_base;
    uint64_t gicr_base;
    uint32_t max_spi;
    int ready;
    uint32_t irq_enabled_count;
    uint32_t irq_ack_count;
    uint32_t irq_eoi_count;
} aarch64_gicv3_state_t;

int aarch64_gicv3_init(uint64_t gicd_base, uint64_t gicr_base);
int aarch64_gicv3_enable_irq(uint32_t irq, uint8_t priority);
int aarch64_gicv3_disable_irq(uint32_t irq);
uint32_t aarch64_gicv3_ack_irq(void);
int aarch64_gicv3_eoi_irq(uint32_t irq);
const aarch64_gicv3_state_t *aarch64_gicv3_get_state(void);

#endif /* OPENOS_ARCH_AARCH64_GICV3_H */
