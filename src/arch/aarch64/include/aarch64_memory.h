#ifndef OPENOS_ARCH_AARCH64_MEMORY_H
#define OPENOS_ARCH_AARCH64_MEMORY_H

#include <stddef.h>
#include <stdint.h>

#define AARCH64_PAGE_SIZE 4096UL
#define AARCH64_KERNEL_HEAP_SIZE (1024UL * 1024UL)
#define AARCH64_EARLY_PMM_BYTES  (16UL * 1024UL * 1024UL)

typedef struct aarch64_memory_state {
    uintptr_t kernel_end;
    uintptr_t pmm_start;
    uintptr_t pmm_next;
    uintptr_t pmm_end;
    uintptr_t heap_start;
    uintptr_t heap_next;
    uintptr_t heap_end;
    uint64_t pmm_alloc_count;
    uint64_t heap_alloc_count;
    uint8_t initialized;
} aarch64_memory_state_t;

void aarch64_memory_init(void);
const aarch64_memory_state_t *aarch64_memory_get_state(void);
void *aarch64_pmm_alloc_page(void);
void aarch64_pmm_free_page(void *page);
void *aarch64_heap_alloc(size_t size, size_t alignment);
void aarch64_heap_reset(void);
uintptr_t aarch64_vmm_kernel_to_phys(const void *addr);
void *aarch64_vmm_phys_to_kernel(uintptr_t phys);
uint8_t aarch64_vmm_map_identity(uintptr_t virt, uintptr_t phys, size_t size, uint64_t flags);
void aarch64_memory_print_status(void);

#endif /* OPENOS_ARCH_AARCH64_MEMORY_H */
