#include "../include/arm_arch.h"

static arm_port_info_t arm_port_info;

void arch_arm_init(void) {
    arch_arm_platform_init();
    early_console_arm_init();

    arm_port_info.status_flags = OPENOS_ARM_PORT_STATUS_BOOT_STUB |
                                 OPENOS_ARM_PORT_STATUS_UART |
                                 OPENOS_ARM_PORT_STATUS_VECTOR |
                                 OPENOS_ARM_PORT_STATUS_MMU_TODO |
                                 OPENOS_ARM_PORT_STATUS_GIC_TODO;
    arm_port_info.kernel_entry = (arm_entry_t)(uintptr_t)&arm_kernel_main;
    arm_port_info.initial_sp = 0;
}

const arm_port_info_t *arch_arm_get_port_info(void) {
    return &arm_port_info;
}

void arm_kernel_main(void) {
    const arm_platform_info_t *platform;

    arch_arm_init();
    platform = arch_arm_platform_get_info();

    early_console_arm_write("[ARM] OpenOS ARM port skeleton ready\n");
    early_console_arm_write("[ARM] platform=");
    early_console_arm_write_hex32(platform->platform_id);
    early_console_arm_write(" uart0=");
    early_console_arm_write_hex32(platform->uart0_base);
    early_console_arm_write(" kernel_base=");
    early_console_arm_write_hex32(platform->kernel_base);
    early_console_arm_write("\n");

    for (;;) {
        __asm__ __volatile__("" ::: "memory");
    }
}
