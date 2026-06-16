#ifndef OPENOS_ARCH_ARM_ARCH_H
#define OPENOS_ARCH_ARM_ARCH_H

#include "arm_platform.h"
#include "arm_types.h"
#include "early_console_arm.h"

#define OPENOS_ARM_PORT_STATUS_BOOT_STUB 0x00000001u
#define OPENOS_ARM_PORT_STATUS_UART      0x00000002u
#define OPENOS_ARM_PORT_STATUS_VECTOR    0x00000004u
#define OPENOS_ARM_PORT_STATUS_MMU_TODO  0x00000008u
#define OPENOS_ARM_PORT_STATUS_GIC_TODO  0x00000010u

typedef struct arm_port_info {
    uint32_t status_flags;
    arm_entry_t kernel_entry;
    arm_stack_ptr_t initial_sp;
} arm_port_info_t;

void arch_arm_init(void);
const arm_port_info_t *arch_arm_get_port_info(void);
void arm_kernel_main(void);

#endif /* OPENOS_ARCH_ARM_ARCH_H */
