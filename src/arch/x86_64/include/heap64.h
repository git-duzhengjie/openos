#ifndef OPENOS_ARCH_X86_64_HEAP64_H
#define OPENOS_ARCH_X86_64_HEAP64_H

#include <stdint.h>

#include "arch64_types.h"

#define OPENOS_X86_64_HEAP_START 0xFFFFFFFF90000000ULL
#define OPENOS_X86_64_HEAP_SIZE  (64ULL * 1024ULL * 1024ULL)
#define OPENOS_X86_64_HEAP_END   (OPENOS_X86_64_HEAP_START + OPENOS_X86_64_HEAP_SIZE)
#define OPENOS_X86_64_HEAP_INITIAL_SIZE (64ULL * 1024ULL)

typedef struct x86_64_heap_info {
    x86_64_virt_addr_t heap_start;
    x86_64_virt_addr_t heap_end;
    x86_64_virt_addr_t heap_top;
    x86_64_size_t mapped_bytes;
    x86_64_size_t allocated_bytes;
    x86_64_size_t free_bytes;
} x86_64_heap_info_t;

void arch_x86_64_heap_init(void);
void *arch_x86_64_kmalloc(x86_64_size_t size);
void arch_x86_64_kfree(void *ptr);
x86_64_virt_addr_t arch_x86_64_heap_get_current(void);
const x86_64_heap_info_t *arch_x86_64_heap_get_info(void);
void arch_x86_64_heap_print_status(void);

#endif /* OPENOS_ARCH_X86_64_HEAP64_H */
