#include "../include/riscv_arch.h"

static riscv_port_info_t riscv_port_info;

void arch_riscv_init(void) {
    arch_riscv_platform_init();
    early_console_riscv_init();

    riscv_port_info.status_flags = OPENOS_RISCV_PORT_STATUS_BOOT_STUB |
                                   OPENOS_RISCV_PORT_STATUS_UART |
                                   OPENOS_RISCV_PORT_STATUS_TRAP_TODO |
                                   OPENOS_RISCV_PORT_STATUS_MMU_TODO |
                                   OPENOS_RISCV_PORT_STATUS_PLIC_TODO;
    riscv_port_info.kernel_entry = (riscv_entry_t)(uintptr_t)&riscv_kernel_main;
    riscv_port_info.initial_sp = 0;
}

const riscv_port_info_t *arch_riscv_get_port_info(void) {
    return &riscv_port_info;
}

void riscv_kernel_main(void) {
    const riscv_platform_info_t *platform;

    arch_riscv_init();
    platform = arch_riscv_platform_get_info();

    early_console_riscv_write("[RISC-V] OpenOS RV64 port skeleton ready\n");
    early_console_riscv_write("[RISC-V] platform=");
    early_console_riscv_write_hex64(platform->platform_id);
    early_console_riscv_write(" uart0=");
    early_console_riscv_write_hex64(platform->uart0_base);
    early_console_riscv_write(" kernel_base=");
    early_console_riscv_write_hex64(platform->kernel_base);
    early_console_riscv_write("\n");

    for (;;) {
        __asm__ __volatile__("" ::: "memory");
    }
}
