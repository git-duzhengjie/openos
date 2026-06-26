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

/* G.4.3a — broadcast INIT IPI to all APs (no SIPI yet). Returns count of
 * APs whose INIT was acknowledged by the local APIC; *out_sent (optional)
 * receives the attempted count (== ap_count). */
uint32_t arch_x86_64_smp_send_init_all_aps(uint32_t *out_sent);

/* G.4.3b-1 — issue the full INIT-SIPI-SIPI wakeup sequence to every AP:
 *
 *   for each AP:
 *     send_init(apic_id)
 *     delay 10 ms
 *     send_startup(apic_id, vector=trampoline>>12)
 *     delay 200 us
 *     send_startup(apic_id, vector=trampoline>>12)
 *
 * After this returns, an AP that obeys the spec is executing at the
 * trampoline page; in G.4.3b-1 that page is just `cli; hlt`, so APs simply
 * halt. *out_sent (optional) receives the attempted count; the return value
 * is the number of APs whose IPI sequence fully delivered (3 IPIs each). */
uint32_t arch_x86_64_smp_send_startup_all_aps(uint32_t *out_sent);

/* G.4.3b-2a — alive counter at physical 0x9000 (1 byte).
 *
 * The trampoline blob v2 executes `lock inc byte [0x9000]` before halting,
 * so after a successful INIT-SIPI-SIPI each woken AP bumps this counter by
 * (potentially) 1 or 2 depending on whether the 2nd SIPI was honored.
 *
 * BSP responsibilities:
 *   - zero the counter BEFORE the wakeup sequence (arch_x86_64_smp_alive_reset)
 *   - poll arch_x86_64_smp_alive_count() with a timeout
 */
#define OPENOS_X86_64_SMP_ALIVE_PHYS  0x9000ull

void    arch_x86_64_smp_alive_reset(void);
uint8_t arch_x86_64_smp_alive_count(void);

/* Block until alive_count() >= expected or `timeout_ms` elapses (TSC-based).
 * Returns the final alive_count() value. */
uint8_t arch_x86_64_smp_alive_wait(uint8_t expected, uint32_t timeout_ms);

#endif /* OPENOS_ARCH_X86_64_SMP64_H */
