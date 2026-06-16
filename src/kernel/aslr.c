#include "include/aslr.h"
#include "include/usermode.h"
#include "include/vmm.h"

static uint32_t aslr_state = 0x6D2B79F5u;

static uint32_t rotl32(uint32_t value, uint32_t shift)
{
    return (value << shift) | (value >> (32u - shift));
}

uint32_t aslr_next_u32(uint32_t pid, uint32_t tag)
{
    uint32_t x = aslr_state;

    x ^= pid * 0x9E3779B9u;
    x ^= tag * 0x85EBCA6Bu;
    x ^= (uint32_t)&aslr_state;
    x ^= rotl32(x, 13);
    x *= 0xC2B2AE35u;
    x ^= x >> 16;

    aslr_state = x + 0x27D4EB2Fu;
    return x;
}

uint32_t aslr_pick_main_stack_slot(uint32_t pid)
{
    return aslr_next_u32(pid, 0x53544143u) % (ASLR_MAIN_STACK_SLOT_MAX + 1u);
}

uint32_t aslr_pick_next_thread_stack_slot(uint32_t pid)
{
    uint32_t slot = aslr_next_u32(pid, 0x54485244u) % (ASLR_THREAD_STACK_SLOT_MAX + 1u);
    return slot + 1u;
}

uint32_t aslr_apply_heap_gap(uint32_t brk_start, uint32_t pid)
{
    uint32_t pages = aslr_next_u32(pid, 0x48454150u) % (ASLR_HEAP_GAP_MAX_PAGES + 1u);
    uint32_t gap = pages * PAGE_SIZE;
    uint32_t aligned = (brk_start + PAGE_SIZE - 1u) & PAGE_MASK;

    if (aligned < brk_start)
        return brk_start;
    if (aligned + gap < aligned)
        return aligned;
    if (aligned + gap >= ASLR_MMAP_BASE_MIN)
        return aligned;
    return aligned + gap;
}

uint32_t aslr_pick_mmap_base(uint32_t pid)
{
    uint32_t range = ASLR_MMAP_BASE_MAX - ASLR_MMAP_BASE_MIN;
    uint32_t pages = range / PAGE_SIZE;
    uint32_t offset_pages = aslr_next_u32(pid, 0x4D4D4150u) % pages;

    return ASLR_MMAP_BASE_MIN + offset_pages * PAGE_SIZE;
}
