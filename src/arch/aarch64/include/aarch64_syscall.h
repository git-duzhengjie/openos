#ifndef OPENOS_ARCH_AARCH64_SYSCALL_H
#define OPENOS_ARCH_AARCH64_SYSCALL_H

#include <stdint.h>

#define OPENOS_AARCH64_SYS_EXIT   1u
#define OPENOS_AARCH64_SYS_WRITE  4u
#define OPENOS_AARCH64_SYS_YIELD  24u
#define OPENOS_AARCH64_SYS_GETPID 20u

typedef struct aarch64_trap_frame {
    uint64_t x[19];
    uint64_t fp;
    uint64_t lr;
    uint64_t esr_el1;
    uint64_t elr_el1;
    uint64_t far_el1;
    uint64_t spsr_el1;
    uint64_t vector_id;
} aarch64_trap_frame_t;

void aarch64_syscall_init(void);
uint64_t aarch64_syscall_dispatch(aarch64_trap_frame_t *frame);
uint64_t aarch64_syscall_count(void);
uint32_t aarch64_syscall_last_number(void);
uint32_t aarch64_syscall_last_exit_code(void);
uint8_t aarch64_syscall_exit_requested(void);
void aarch64_syscall_print_status(void);

#endif /* OPENOS_ARCH_AARCH64_SYSCALL_H */
