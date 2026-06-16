#include "../include/arm_platform.h"

static arm_platform_info_t arm_platform_info;

void arch_arm_platform_init(void) {
    arm_platform_info.platform_id = OPENOS_ARM_PLATFORM_QEMU_VIRT;
    arm_platform_info.uart0_base = OPENOS_ARM_UART0_BASE;
    arm_platform_info.gic_dist_base = OPENOS_ARM_GIC_DIST_BASE;
    arm_platform_info.gic_cpu_base = OPENOS_ARM_GIC_CPU_BASE;
    arm_platform_info.kernel_base = OPENOS_ARM_KERNEL_BASE;
}

const arm_platform_info_t *arch_arm_platform_get_info(void) {
    return &arm_platform_info;
}
