#ifndef OPENOS_ARCH_X86_64_PERCPU64_H
#define OPENOS_ARCH_X86_64_PERCPU64_H

#include <stdint.h>
#include <stdbool.h>

/* G.6.2: per-CPU "current" structure, addressable via %gs:0..
 * - self      : copy of the pointer to this structure itself (for sanity
 *               checks; lets code read it via %gs:0 and verify it == &g_percpu[i])
 * - cpu_idx   : logical CPU index (BSP=0, AP=1..N-1)
 * - magic     : 'PCPU' (0x55504350) so a stray uninit GS_BASE is detected
 * - sched_ticks / sched_switches : per-CPU scheduler counters (populated by
 *                                   G.6.3+; declared here so layout is stable)
 */
#define OPENOS_X86_64_PERCPU_MAGIC 0x55504350u  /* 'PCPU' little-endian */

typedef struct openos_x86_64_percpu {
    uint64_t self;            /* offset 0x00: pointer to self */
    uint32_t cpu_idx;         /* offset 0x08 */
    uint32_t magic;           /* offset 0x0C */
    uint64_t sched_ticks;     /* offset 0x10 (reserved for G.6.3+) */
    uint64_t sched_switches;  /* offset 0x18 (reserved for G.6.3+) */
    uint64_t _pad[12];        /* pad to 128 bytes for cache-line alignment */
} __attribute__((aligned(64))) arch_x86_64_percpu_t;

/* IA32_GS_BASE MSR */
#define OPENOS_X86_64_MSR_GS_BASE        0xC0000101u
#define OPENOS_X86_64_MSR_KERNEL_GS_BASE 0xC0000102u

/* Install GS_BASE for the *current* CPU to &g_percpu[cpu_idx], after
 * filling the struct. Safe to call from BSP (cpu_idx=0) and from each AP. */
void arch_x86_64_percpu_install_gs(uint32_t cpu_idx);

/* Read the current CPU's percpu struct via %gs:0 (the "self" slot). */
static inline arch_x86_64_percpu_t *arch_x86_64_this_cpu_ptr(void) {
    arch_x86_64_percpu_t *p;
    __asm__ volatile ("movq %%gs:0, %0" : "=r"(p));
    return p;
}

static inline uint32_t arch_x86_64_this_cpu_idx(void) {
    uint32_t idx;
    __asm__ volatile ("movl %%gs:8, %0" : "=r"(idx));
    return idx;
}

static inline uint32_t arch_x86_64_this_cpu_magic(void) {
    uint32_t mg;
    __asm__ volatile ("movl %%gs:12, %0" : "=r"(mg));
    return mg;
}

/* Returns true iff %gs:0 points to a properly initialized percpu struct
 * whose self-pointer is self-consistent and magic == 'PCPU'. */
bool arch_x86_64_percpu_gs_ok(void);

/* Direct accessor for the BSP's percpu (used by the selftest). */
arch_x86_64_percpu_t *arch_x86_64_percpu_slot(uint32_t cpu_idx);

/*
 * Step G.5-gdt-tss: per-CPU GDT + TSS infrastructure.
 *
 * Each CPU needs its own TSS because RSP0 (the stack pointer hardware
 * loads on ring3 -> ring0 transitions) is per-CPU state. The TSS
 * descriptor lives inside the GDT, and the TSS *base* field encodes
 * a 64-bit virtual address, so the GDT itself must also be per-CPU.
 *
 * This module owns a fixed-size array of GDT+TSS+stack tuples indexed
 * by cpu_idx (0 = BSP, 1..N = APs). Each AP calls percpu_setup() to
 * fill in its slot, then percpu_load() to commit lgdt + ltr.
 */

#include <stdint.h>

#include "arch64_types.h"

#define OPENOS_X86_64_PERCPU_MAX_CPUS    4u
#define OPENOS_X86_64_PERCPU_RSP0_SIZE   16384u   /* 16 KiB ring0 stack */
#define OPENOS_X86_64_PERCPU_IST_COUNT   2u       /* IST1 = NMI, IST2 = #DF */
#define OPENOS_X86_64_PERCPU_IST_SIZE    8192u    /* 8 KiB each */

/* Build the GDT + TSS for cpu_idx in this module's BSS arrays. */
void arch_x86_64_percpu_setup(uint32_t cpu_idx);

/* Execute lgdt + ltr against cpu_idx's tables on the current CPU.
 * Must be called from the CPU that will use these tables. */
void arch_x86_64_percpu_load(uint32_t cpu_idx);

/* Helpers / accessors. */
uint32_t           arch_x86_64_percpu_max(void);
x86_64_stack_ptr_t arch_x86_64_percpu_rsp0(uint32_t cpu_idx);
x86_64_virt_addr_t arch_x86_64_percpu_tss_base(uint32_t cpu_idx);

#endif /* OPENOS_ARCH_X86_64_PERCPU64_H */
