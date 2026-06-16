#ifndef OPENOS_ARCH_RISCV_PLATFORM_H
#define OPENOS_ARCH_RISCV_PLATFORM_H

#include "riscv_types.h"

#define OPENOS_RISCV_PLATFORM_QEMU_VIRT 1ull
#define OPENOS_RISCV_UART0_BASE         0x10000000ull
#define OPENOS_RISCV_CLINT_BASE         0x02000000ull
#define OPENOS_RISCV_PLIC_BASE          0x0C000000ull
#define OPENOS_RISCV_DRAM_BASE          0x80000000ull
#define OPENOS_RISCV_KERNEL_BASE        OPENOS_RISCV_DRAM_BASE
#define OPENOS_RISCV_BOOT_STACK_SIZE    0x4000ull

typedef struct riscv_platform_info {
    uint64_t platform_id;
    riscv_phys_addr_t uart0_base;
    riscv_phys_addr_t clint_base;
    riscv_phys_addr_t plic_base;
    riscv_phys_addr_t dram_base;
    riscv_phys_addr_t kernel_base;
} riscv_platform_info_t;

void arch_riscv_platform_init(void);
const riscv_platform_info_t *arch_riscv_platform_get_info(void);

#endif /* OPENOS_ARCH_RISCV_PLATFORM_H */
