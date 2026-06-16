#ifndef OPENOS_ASLR_H
#define OPENOS_ASLR_H

#include <stdint.h>

#define ASLR_MAIN_STACK_SLOT_MAX 31u
#define ASLR_THREAD_STACK_SLOT_MAX 31u
#define ASLR_HEAP_GAP_MAX_PAGES 64u
#define ASLR_MMAP_BASE_MIN 0x50000000u
#define ASLR_MMAP_BASE_MAX 0x60000000u
#define ASLR_MMAP_LIMIT    0x70000000u

uint32_t aslr_next_u32(uint32_t pid, uint32_t tag);
uint32_t aslr_pick_main_stack_slot(uint32_t pid);
uint32_t aslr_pick_next_thread_stack_slot(uint32_t pid);
uint32_t aslr_apply_heap_gap(uint32_t brk_start, uint32_t pid);
uint32_t aslr_pick_mmap_base(uint32_t pid);

#endif
