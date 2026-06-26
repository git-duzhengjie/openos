#ifndef OPENOS_ARCH_X86_64_SMP64_H
#define OPENOS_ARCH_X86_64_SMP64_H

#include <stdint.h>
#include <stdbool.h>

/* Step G.4.1 — SMP topology enumeration (no AP wakeup yet).
 *
 * Scope (G.4.1):
 *   - Snapshot BSP apic_id (from LAPIC ID register at runtime).
 *   - Build a list of candidate AP apic_ids by filtering ACPI MADT
 *     processor entries: must have flags.enabled==1 and apic_id != BSP.
 *   - Choose a low-1MB physical page (currently fixed at 0x8000) as the
 *     future AP trampoline drop point. We do not write anything there yet.
 *
 * Deferred (G.4.2+):
 *   - Real-mode trampoline blob (16/32/64-bit transitions)
 *   - INIT-SIPI-SIPI ICR sequencing
 *   - Per-AP stack allocation + kernel_main_ap() entry
 *   - Per-CPU GDT/TSS (G.5)
 */

#define OPENOS_X86_64_SMP_MAX_CPUS          16u
#define OPENOS_X86_64_SMP_TRAMPOLINE_PHYS   0x8000ull

bool arch_x86_64_smp_init(void);
bool arch_x86_64_smp_is_ready(void);

uint8_t  arch_x86_64_smp_bsp_apic_id(void);
uint32_t arch_x86_64_smp_ap_count(void);
uint8_t  arch_x86_64_smp_ap_apic_id(uint32_t index);
uint64_t arch_x86_64_smp_trampoline_phys(void);

/* Total number of CPUs the firmware reported as enabled (BSP + APs). */
uint32_t arch_x86_64_smp_cpu_count(void);

/* G.4.2: install the AP trampoline blob at OPENOS_X86_64_SMP_TRAMPOLINE_PHYS.
 * Returns true on success (blob bytes match and verify ok). No IPI fired.
 */
bool arch_x86_64_smp_install_trampoline(void);
bool arch_x86_64_smp_trampoline_installed(void);

#endif /* OPENOS_ARCH_X86_64_SMP64_H */
