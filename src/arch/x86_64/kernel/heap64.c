#include "../include/early_console64.h"
#include "../include/heap64.h"
#include "../include/pmm64.h"
#include "../include/vmm64.h"

#define HEAP64_ALLOC_FLAG 0x1ULL
#define HEAP64_SIZE_MASK  (~0xFULL)
#define HEAP64_MIN_BLOCK_SIZE 32ULL
#define HEAP64_ALIGN 16ULL

typedef struct heap64_alloc_header {
    x86_64_size_t size;
} heap64_alloc_header_t;

typedef struct heap64_free_block {
    x86_64_size_t size;
    struct heap64_free_block *next;
} heap64_free_block_t;

static heap64_free_block_t *heap64_free_list;
static x86_64_heap_info_t heap64_info;

static x86_64_size_t align_up_size(x86_64_size_t value, x86_64_size_t align) {
    return (value + align - 1ULL) & ~(align - 1ULL);
}

static uint64_t block_size(uint64_t value) {
    return value & HEAP64_SIZE_MASK;
}

static void recalc_free_bytes(void) {
    heap64_free_block_t *cur = heap64_free_list;
    x86_64_size_t free_bytes = 0;

    while (cur) {
        free_bytes += block_size(cur->size);
        cur = cur->next;
    }
    heap64_info.free_bytes = free_bytes;
}

static int heap64_expand(x86_64_size_t bytes) {
    uint64_t pages;
    x86_64_virt_addr_t start;
    uint64_t i;

    bytes = align_up_size(bytes, OPENOS_X86_64_VMM_PAGE_SIZE);
    if (bytes == 0 || heap64_info.heap_top + bytes > heap64_info.heap_end) {
        return -1;
    }

    start = heap64_info.heap_top;
    pages = bytes / OPENOS_X86_64_VMM_PAGE_SIZE;
    for (i = 0; i < pages; ++i) {
        x86_64_phys_addr_t phys = arch_x86_64_pmm_alloc_page();
        if (phys == 0) {
            return -1;
        }
        if (arch_x86_64_vmm_map_page(heap64_info.heap_top,
                                     phys,
                                     OPENOS_X86_64_VMM_KERNEL_FLAGS) != 0) {
            arch_x86_64_pmm_free_page(phys);
            return -1;
        }
        heap64_info.heap_top += OPENOS_X86_64_VMM_PAGE_SIZE;
        heap64_info.mapped_bytes += OPENOS_X86_64_VMM_PAGE_SIZE;
    }

    heap64_free_block_t *block = (heap64_free_block_t *)(uintptr_t)start;
    block->size = bytes;
    block->next = heap64_free_list;
    heap64_free_list = block;
    recalc_free_bytes();
    return 0;
}

void arch_x86_64_heap_init(void) {
    heap64_free_list = 0;
    heap64_info.heap_start = OPENOS_X86_64_HEAP_START;
    heap64_info.heap_end = OPENOS_X86_64_HEAP_END;
    heap64_info.heap_top = OPENOS_X86_64_HEAP_START;
    heap64_info.mapped_bytes = 0;
    heap64_info.allocated_bytes = 0;
    heap64_info.free_bytes = 0;

    (void)heap64_expand(OPENOS_X86_64_HEAP_INITIAL_SIZE);
}

void *arch_x86_64_kmalloc(x86_64_size_t size) {
    x86_64_size_t total;
    heap64_free_block_t *prev;
    heap64_free_block_t *cur;

    if (size == 0) {
        return 0;
    }

    total = align_up_size(size + sizeof(heap64_alloc_header_t), HEAP64_ALIGN);
    if (total < HEAP64_MIN_BLOCK_SIZE) {
        total = HEAP64_MIN_BLOCK_SIZE;
    }

    for (;;) {
        prev = 0;
        cur = heap64_free_list;
        while (cur) {
            x86_64_size_t cur_size = block_size(cur->size);
            if (cur_size >= total) {
                if (cur_size >= total + HEAP64_MIN_BLOCK_SIZE) {
                    heap64_free_block_t *remain = (heap64_free_block_t *)((uint8_t *)cur + total);
                    remain->size = cur_size - total;
                    remain->next = cur->next;
                    if (prev) {
                        prev->next = remain;
                    } else {
                        heap64_free_list = remain;
                    }
                    cur->size = total | HEAP64_ALLOC_FLAG;
                } else {
                    if (prev) {
                        prev->next = cur->next;
                    } else {
                        heap64_free_list = cur->next;
                    }
                    cur->size = cur_size | HEAP64_ALLOC_FLAG;
                    total = cur_size;
                }

                heap64_info.allocated_bytes += total;
                recalc_free_bytes();
                return (void *)((heap64_alloc_header_t *)cur + 1);
            }
            prev = cur;
            cur = cur->next;
        }

        if (heap64_expand(total) != 0) {
            return 0;
        }
    }
}

void arch_x86_64_kfree(void *ptr) {
    heap64_alloc_header_t *hdr;
    heap64_free_block_t *block;
    heap64_free_block_t *prev;
    heap64_free_block_t *cur;
    x86_64_size_t size;

    if (!ptr) {
        return;
    }

    hdr = (heap64_alloc_header_t *)ptr - 1;
    if ((hdr->size & HEAP64_ALLOC_FLAG) == 0) {
        return;
    }

    size = block_size(hdr->size);
    block = (heap64_free_block_t *)hdr;
    block->size = size;

    prev = 0;
    cur = heap64_free_list;
    while (cur && cur < block) {
        prev = cur;
        cur = cur->next;
    }

    block->next = cur;
    if (prev) {
        prev->next = block;
    } else {
        heap64_free_list = block;
    }

    if (block->next && (uint8_t *)block + block_size(block->size) == (uint8_t *)block->next) {
        block->size = block_size(block->size) + block_size(block->next->size);
        block->next = block->next->next;
    }
    if (prev && (uint8_t *)prev + block_size(prev->size) == (uint8_t *)block) {
        prev->size = block_size(prev->size) + block_size(block->size);
        prev->next = block->next;
    }

    if (heap64_info.allocated_bytes >= size) {
        heap64_info.allocated_bytes -= size;
    } else {
        heap64_info.allocated_bytes = 0;
    }
    recalc_free_bytes();
}

x86_64_virt_addr_t arch_x86_64_heap_get_current(void) {
    return heap64_info.heap_top;
}

const x86_64_heap_info_t *arch_x86_64_heap_get_info(void) {
    return &heap64_info;
}

void arch_x86_64_heap_print_status(void) {
    early_console64_write("[x86_64][HEAP] top=");
    early_console64_write_hex64(heap64_info.heap_top);
    early_console64_write(" mapped=");
    early_console64_write_hex64(heap64_info.mapped_bytes);
    early_console64_write(" free=");
    early_console64_write_hex64(heap64_info.free_bytes);
    early_console64_write(" allocated=");
    early_console64_write_hex64(heap64_info.allocated_bytes);
    early_console64_write("\n");
}
