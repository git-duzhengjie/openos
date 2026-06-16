#ifndef OPENOS_ARCH_X86_64_SCHED64_H
#define OPENOS_ARCH_X86_64_SCHED64_H

#include <stdint.h>

#include "arch64_types.h"

#define OPENOS_X86_64_CONTEXT_RFLAGS_IF (1ULL << 9)

typedef struct x86_64_context {
    x86_64_stack_ptr_t rsp;
    x86_64_entry_t rip;
    uint64_t rflags;

    uint64_t rbx;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;

    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
} x86_64_context_t;

typedef struct x86_64_thread_context {
    x86_64_context_t regs;
    x86_64_stack_ptr_t kernel_stack_base;
    x86_64_size_t kernel_stack_size;
} x86_64_thread_context_t;

typedef void (*x86_64_thread_entry_t)(void *arg);

void arch_x86_64_sched_init(void);
void arch_x86_64_context_init(x86_64_thread_context_t *ctx,
                              x86_64_thread_entry_t entry,
                              void *arg,
                              x86_64_stack_ptr_t stack_top);
void arch_x86_64_context_switch(x86_64_context_t *from, x86_64_context_t *to);
const x86_64_context_t *arch_x86_64_current_context(void);
void arch_x86_64_sched_print_status(void);

#endif /* OPENOS_ARCH_X86_64_SCHED64_H */
