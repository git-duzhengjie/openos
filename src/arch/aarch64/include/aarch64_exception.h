#ifndef OPENOS_ARCH_AARCH64_EXCEPTION_H
#define OPENOS_ARCH_AARCH64_EXCEPTION_H

#include "aarch64_syscall.h"

typedef enum aarch64_exception_type {
    AARCH64_EXC_SYNC_CURRENT_SP0 = 0,
    AARCH64_EXC_IRQ_CURRENT_SP0 = 1,
    AARCH64_EXC_FIQ_CURRENT_SP0 = 2,
    AARCH64_EXC_SERROR_CURRENT_SP0 = 3,
    AARCH64_EXC_SYNC_CURRENT_SPX = 4,
    AARCH64_EXC_IRQ_CURRENT_SPX = 5,
    AARCH64_EXC_FIQ_CURRENT_SPX = 6,
    AARCH64_EXC_SERROR_CURRENT_SPX = 7,
    AARCH64_EXC_SYNC_LOWER_AARCH64 = 8,
    AARCH64_EXC_IRQ_LOWER_AARCH64 = 9,
    AARCH64_EXC_FIQ_LOWER_AARCH64 = 10,
    AARCH64_EXC_SERROR_LOWER_AARCH64 = 11,
    AARCH64_EXC_SYNC_LOWER_AARCH32 = 12,
    AARCH64_EXC_IRQ_LOWER_AARCH32 = 13,
    AARCH64_EXC_FIQ_LOWER_AARCH32 = 14,
    AARCH64_EXC_SERROR_LOWER_AARCH32 = 15,
} aarch64_exception_type_t;

void aarch64_exception_init(void);
void aarch64_exception_dispatch(aarch64_trap_frame_t *frame);

/* IRQ dispatch layer (M11-C.3). */
typedef void (*aarch64_irq_handler_fn_t)(uint32_t intid, void *cookie);
int      aarch64_irq_register(uint32_t intid, aarch64_irq_handler_fn_t handler, void *cookie);
uint32_t aarch64_irq_total_count(void);
uint32_t aarch64_irq_count_for(uint32_t intid);
uint32_t aarch64_irq_spurious_count(void);
void     aarch64_irq_simulate(uint32_t intid);
void aarch64_panic(const char *reason);

extern char aarch64_exception_vector_table[];

#endif /* OPENOS_ARCH_AARCH64_EXCEPTION_H */
