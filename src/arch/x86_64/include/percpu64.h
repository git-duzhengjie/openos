#ifndef OPENOS_ARCH_X86_64_PERCPU64_H
#define OPENOS_ARCH_X86_64_PERCPU64_H

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
