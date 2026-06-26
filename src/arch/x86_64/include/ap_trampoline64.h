#ifndef OPENOS_ARCH_X86_64_AP_TRAMPOLINE64_H
#define OPENOS_ARCH_X86_64_AP_TRAMPOLINE64_H

#include <stdint.h>
#include <stdbool.h>

/* Step G.4.3b-2 → G.5 — AP trampoline blob installer.
 *
 * Fixed offsets within the blob (all relative to phys_addr base = 0x8000):
 *   +0x000: 16-bit real mode entry
 *   +0x040: 32-bit protected mode entry
 *   +0x0A0: 64-bit long mode entry
 *   +0x0C0: GDT (8 descriptors, 64 bytes)
 *   +0x100: GDTR (10 bytes)
 *   +0x10A: CR3 slot (8 bytes, backfilled by BSP)
 *   +0x112: AP entry point (8 bytes, backfilled by BSP)
 *   +0x11A: magic 'APTR' (4 bytes) + version (1 byte)
 *
 * Alive counters (physical memory):
 *   0x9000: real mode reached
 *   0x9008: protected mode reached
 *   0x9010: long mode reached
 */

#define OPENOS_AP_TRAMPOLINE_MAGIC0   'A'
#define OPENOS_AP_TRAMPOLINE_MAGIC1   'P'
#define OPENOS_AP_TRAMPOLINE_MAGIC2   'T'
#define OPENOS_AP_TRAMPOLINE_MAGIC3   'R'
#define OPENOS_AP_TRAMPOLINE_VERSION  0x05u

/* Fixed offsets within blob. */
/* G.5-SSE: long-mode entry now enables x87/SSE before C code, which adds
 * ~32 bytes; all subsequent slots shifted up by 0x20 from G.4.3b-2. */
#define OPENOS_AP_TRAMPOLINE_CR3_OFFSET     0x110u
#define OPENOS_AP_TRAMPOLINE_ENTRY_OFFSET   0x118u
#define OPENOS_AP_TRAMPOLINE_MAGIC_OFFSET   0x120u

/* Size of the embedded blob (bytes). */
uint64_t arch_x86_64_ap_trampoline_size(void);

/* Pointer to the embedded blob (read-only). */
const uint8_t *arch_x86_64_ap_trampoline_blob(void);

/* Copy the blob to the given physical (== identity-mapped virtual) address.
 * Returns true on success. Caller must zero 0x9000-0x9017 alive counters.
 */
bool arch_x86_64_ap_trampoline_install(uint64_t phys_addr);

/* Backfill CR3 value at the fixed offset within the installed blob. */
void arch_x86_64_ap_trampoline_set_cr3(uint64_t phys_addr, uint64_t cr3);

/* Backfill AP entry point address at the fixed offset. */
void arch_x86_64_ap_trampoline_set_entry(uint64_t phys_addr, uint64_t entry);

/* Read back the blob at phys_addr and verify magic+version. */
bool arch_x86_64_ap_trampoline_verify(uint64_t phys_addr);

#endif /* OPENOS_ARCH_X86_64_AP_TRAMPOLINE64_H */
