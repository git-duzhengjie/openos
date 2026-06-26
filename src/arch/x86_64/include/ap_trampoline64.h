#ifndef OPENOS_ARCH_X86_64_AP_TRAMPOLINE64_H
#define OPENOS_ARCH_X86_64_AP_TRAMPOLINE64_H

#include <stdint.h>
#include <stdbool.h>

/* Step G.4.2 — AP trampoline blob installer.
 *
 * The blob currently is a placeholder (magic + hlt loop). It is copied to
 * the configured low-1MB page so future G.4.3 can replace the body with
 * the real 16->32->64 bit transition.
 *
 * No IPI is fired yet; APs never actually execute this in G.4.2.
 */

#define OPENOS_AP_TRAMPOLINE_MAGIC0   'A'
#define OPENOS_AP_TRAMPOLINE_MAGIC1   'P'
#define OPENOS_AP_TRAMPOLINE_MAGIC2   'T'
#define OPENOS_AP_TRAMPOLINE_MAGIC3   'R'
#define OPENOS_AP_TRAMPOLINE_VERSION  0x02u

/* Size of the embedded blob (bytes). */
uint64_t arch_x86_64_ap_trampoline_size(void);

/* Pointer to the embedded blob (read-only). */
const uint8_t *arch_x86_64_ap_trampoline_blob(void);

/* Copy the blob to the given physical (== identity-mapped virtual) address.
 * Returns true on success. */
bool arch_x86_64_ap_trampoline_install(uint64_t phys_addr);

/* Read back the first bytes at phys_addr and verify magic+version. */
bool arch_x86_64_ap_trampoline_verify(uint64_t phys_addr);

#endif /* OPENOS_ARCH_X86_64_AP_TRAMPOLINE64_H */
