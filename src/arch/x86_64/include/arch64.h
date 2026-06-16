#ifndef OPENOS_ARCH_X86_64_H
#define OPENOS_ARCH_X86_64_H

#include <stdint.h>

#include "arch64_types.h"

#define OPENOS_X86_64_VIRTUAL_BITS 48
#define OPENOS_X86_64_PAGE_SIZE 4096ULL
#define OPENOS_X86_64_KERNEL_BASE 0xFFFFFFFF80000000ULL

typedef struct x86_64_kernel_entry_info {
    x86_64_entry_t entry;
    x86_64_stack_ptr_t stack_top;
    x86_64_virt_addr_t kernel_base;
} x86_64_kernel_entry_info_t;

void _start64(void);
void kernel_main64(void);
void arch_x86_64_early_init(void);

#endif /* OPENOS_ARCH_X86_64_H */
