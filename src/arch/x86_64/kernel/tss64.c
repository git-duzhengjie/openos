/* G.7a — per-CPU TSS unification.
 *
 * Historically tss64.c owned a single global `tss64` plus its RSP0/IST
 * backing stacks. That setup was fundamentally BSP-only: the moment a
 * second core started taking ring0 interrupts it would clobber the same
 * RSP0 stack as the BSP, and `ltr` on every CPU would point at the same
 * TSS descriptor. percpu64.c already maintains a parallel per-CPU
 * GDT/TSS/RSP0/IST farm (used by APs since G.5); G.7a finishes the job
 * by routing the legacy public API through that farm and dropping the
 * BSP-only singleton.
 *
 * The public surface (arch_x86_64_tss_init/load/get/base/limit/rsp0/ist
 * /print_status) is preserved so syscall64.c, idt64.c, compat32.c and
 * the BSP early-init sequence in kernel64.c don't have to change. Each
 * call simply targets the BSP's per-CPU slot (cpu_idx=0); APs continue
 * to invoke arch_x86_64_percpu_setup/load directly from smp_init.
 */

#include "../include/gdt64.h"
#include "../include/tss64.h"
#include "../include/percpu64.h"
#include "../include/early_console64.h"

/* BSP is always cpu_idx 0 by contract (smp_init enumerates APs as 1..N). */
#define BSP_CPU_IDX 0u

void arch_x86_64_tss_init(void) {
    /* percpu_setup builds GDT + TSS for cpu0 with RSP0 + IST stacks
     * carved out of g_rsp0_stack[0]/g_ist_stack[0] inside percpu64.c. */
    arch_x86_64_percpu_setup(BSP_CPU_IDX);
}

void arch_x86_64_tss_load(void) {
    /* percpu_load issues lgdt + far-return + ds/es/ss/fs/gs reload + ltr
     * in a single critical-section, so we can't split "load gdt" and
     * "ltr" apart any more. That's fine — the legacy gdt64.c is now a
     * no-op (see below) and arch_x86_64_early_init still calls
     * tss_init → gdt_init → tss_load in order; tss_load is the one that
     * actually commits BSP segmentation state. */
    arch_x86_64_percpu_load(BSP_CPU_IDX);
}

const struct x86_64_tss *arch_x86_64_tss_get(void) {
    /* The percpu module owns g_tss[]; expose cpu0's slot through the
     * legacy pointer so anyone who held onto the historical "the TSS"
     * still sees the BSP's view. */
    return (const struct x86_64_tss *)(uintptr_t)
        arch_x86_64_percpu_tss_base(BSP_CPU_IDX);
}

x86_64_virt_addr_t arch_x86_64_tss_base(void) {
    return arch_x86_64_percpu_tss_base(BSP_CPU_IDX);
}

uint32_t arch_x86_64_tss_limit(void) {
    return (uint32_t)(sizeof(struct x86_64_tss) - 1u);
}

x86_64_stack_ptr_t arch_x86_64_tss_rsp0(void) {
    return arch_x86_64_percpu_rsp0(BSP_CPU_IDX);
}

x86_64_stack_ptr_t arch_x86_64_tss_ist(uint8_t ist_index) {
    /* Both the legacy API and arch_x86_64_percpu_ist use the same
     * 1-based convention; just forward. percpu_ist returns 0 for
     * out-of-range slots. */
    return arch_x86_64_percpu_ist(BSP_CPU_IDX, (uint32_t)ist_index);
}

void arch_x86_64_tss_print_status(void) {
    const struct x86_64_tss *t = arch_x86_64_tss_get();
    early_console64_write("[x86_64][tss] cpu0 rsp0=");
    early_console64_write_hex64(t->rsp[0]);
    early_console64_write(" ist1=");
    early_console64_write_hex64(t->ist[0]);
    early_console64_write(" ist2=");
    early_console64_write_hex64(t->ist[1]);
    early_console64_write(" iomap=");
    early_console64_write_hex64(t->iomap_base);
    early_console64_write("\n");
}
