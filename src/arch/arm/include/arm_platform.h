#ifndef OPENOS_ARCH_ARM_PLATFORM_H
#define OPENOS_ARCH_ARM_PLATFORM_H

#include "arm_types.h"

#define OPENOS_ARM_PLATFORM_QEMU_VIRT 1u
#define OPENOS_ARM_UART0_BASE 0x09000000u
#define OPENOS_ARM_GIC_DIST_BASE 0x08000000u
#define OPENOS_ARM_GIC_CPU_BASE  0x08010000u
#define OPENOS_ARM_KERNEL_BASE   0x80000000u

typedef struct arm_platform_info {
    uint32_t platform_id;
    arm_phys_addr_t uart0_base;
    arm_phys_addr_t gic_dist_base;
    arm_phys_addr_t gic_cpu_base;
    arm_phys_addr_t kernel_base;
} arm_platform_info_t;

void arch_arm_platform_init(void);
const arm_platform_info_t *arch_arm_platform_get_info(void);

#endif /* OPENOS_ARCH_ARM_PLATFORM_H */
