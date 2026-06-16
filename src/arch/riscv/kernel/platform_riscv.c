#include "../include/riscv_platform.h"

static riscv_platform_info_t riscv_platform_info;

void arch_riscv_platform_init(void) {
    riscv_platform_info.platform_id = OPENOS_RISCV_PLATFORM_QEMU_VIRT;
    riscv_platform_info.uart0_base = OPENOS_RISCV_UART0_BASE;
    riscv_platform_info.clint_base = OPENOS_RISCV_CLINT_BASE;
    riscv_platform_info.plic_base = OPENOS_RISCV_PLIC_BASE;
    riscv_platform_info.dram_base = OPENOS_RISCV_DRAM_BASE;
    riscv_platform_info.kernel_base = OPENOS_RISCV_KERNEL_BASE;
}

const riscv_platform_info_t *arch_riscv_platform_get_info(void) {
    return &riscv_platform_info;
}
