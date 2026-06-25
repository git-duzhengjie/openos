#include "aarch64_memory.h"

#include "aarch64_uart.h"

extern char __kernel_end[];

static aarch64_memory_state_t memory_state;
static unsigned char early_heap[AARCH64_KERNEL_HEAP_SIZE] __attribute__((aligned(AARCH64_PAGE_SIZE)));
static unsigned char early_pmm[AARCH64_EARLY_PMM_BYTES] __attribute__((aligned(AARCH64_PAGE_SIZE)));

static uintptr_t align_up_uintptr(uintptr_t value, uintptr_t alignment)
{
    if (alignment == 0u) {
        return value;
    }
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static void write_hex64(uint64_t value)
{
    aarch64_uart_write("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        uint8_t digit = (uint8_t)((value >> shift) & 0xfu);
        aarch64_uart_putc((char)(digit < 10u ? ('0' + digit) : ('a' + digit - 10u)));
    }
}

void aarch64_memory_init(void)
{
    uintptr_t pmm_start = align_up_uintptr((uintptr_t)early_pmm, AARCH64_PAGE_SIZE);
    uintptr_t pmm_end = ((uintptr_t)early_pmm + sizeof(early_pmm)) & ~(uintptr_t)(AARCH64_PAGE_SIZE - 1u);
    uintptr_t heap_start = align_up_uintptr((uintptr_t)early_heap, 16u);

    memory_state.kernel_end = (uintptr_t)__kernel_end;
    memory_state.pmm_start = pmm_start;
    memory_state.pmm_next = pmm_start;
    memory_state.pmm_end = pmm_end;
    memory_state.heap_start = heap_start;
    memory_state.heap_next = heap_start;
    memory_state.heap_end = (uintptr_t)early_heap + sizeof(early_heap);
    memory_state.pmm_alloc_count = 0;
    memory_state.heap_alloc_count = 0;
    memory_state.initialized = 1u;
}

const aarch64_memory_state_t *aarch64_memory_get_state(void)
{
    return &memory_state;
}

void *aarch64_pmm_alloc_page(void)
{
    uintptr_t page;

    if (memory_state.initialized == 0u) {
        aarch64_memory_init();
    }

    page = align_up_uintptr(memory_state.pmm_next, AARCH64_PAGE_SIZE);
    if (page + AARCH64_PAGE_SIZE > memory_state.pmm_end) {
        return 0;
    }

    memory_state.pmm_next = page + AARCH64_PAGE_SIZE;
    ++memory_state.pmm_alloc_count;
    return (void *)page;
}

void aarch64_pmm_free_page(void *page)
{
    (void)page;
}

void *aarch64_heap_alloc(size_t size, size_t alignment)
{
    uintptr_t allocation;

    if (memory_state.initialized == 0u) {
        aarch64_memory_init();
    }
    if (size == 0u) {
        return 0;
    }
    if (alignment < 8u) {
        alignment = 8u;
    }

    allocation = align_up_uintptr(memory_state.heap_next, (uintptr_t)alignment);
    if (allocation + size > memory_state.heap_end) {
        return 0;
    }

    memory_state.heap_next = allocation + size;
    ++memory_state.heap_alloc_count;
    return (void *)allocation;
}

void aarch64_heap_reset(void)
{
    memory_state.heap_next = memory_state.heap_start;
    memory_state.heap_alloc_count = 0;
}

uintptr_t aarch64_vmm_kernel_to_phys(const void *addr)
{
    return (uintptr_t)addr;
}

void *aarch64_vmm_phys_to_kernel(uintptr_t phys)
{
    return (void *)phys;
}

uint8_t aarch64_vmm_map_identity(uintptr_t virt, uintptr_t phys, size_t size, uint64_t flags)
{
    (void)virt;
    (void)phys;
    (void)size;
    (void)flags;
    return 1u;
}

void aarch64_memory_print_status(void)
{
    aarch64_uart_write("A5.5: PMM start=");
    write_hex64(memory_state.pmm_start);
    aarch64_uart_write(" next=");
    write_hex64(memory_state.pmm_next);
    aarch64_uart_write(" end=");
    write_hex64(memory_state.pmm_end);
    aarch64_uart_write(" heap_next=");
    write_hex64(memory_state.heap_next);
    aarch64_uart_write("\n");
}
