#ifndef OPENOS_ARCH_RISCV_ARCH_H
#define OPENOS_ARCH_RISCV_ARCH_H

#include "early_console_riscv.h"
#include "riscv_platform.h"
#include "riscv_types.h"

#define OPENOS_RISCV_PORT_STATUS_BOOT_STUB 0x00000001ull
#define OPENOS_RISCV_PORT_STATUS_UART      0x00000002ull
#define OPENOS_RISCV_PORT_STATUS_TRAP_TODO 0x00000004ull
#define OPENOS_RISCV_PORT_STATUS_MMU_TODO  0x00000008ull
#define OPENOS_RISCV_PORT_STATUS_PLIC_TODO 0x00000010ull

typedef struct riscv_port_info {
    uint64_t status_flags;
    riscv_entry_t kernel_entry;
    riscv_stack_ptr_t initial_sp;
} riscv_port_info_t;

void arch_riscv_init(void);
const riscv_port_info_t *arch_riscv_get_port_info(void);
void riscv_kernel_main(void);

#endif /* OPENOS_ARCH_RISCV_ARCH_H */
