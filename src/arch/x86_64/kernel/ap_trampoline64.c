#include "../include/ap_trampoline64.h"

#include <stdint.h>
#include <stdbool.h>

/* Symbols emitted by ap_trampoline64.S */
extern const uint8_t __ap_trampoline_blob_start[];
extern const uint8_t __ap_trampoline_blob_end[];

uint64_t arch_x86_64_ap_trampoline_size(void)
{
    return (uint64_t)(__ap_trampoline_blob_end - __ap_trampoline_blob_start);
}

const uint8_t *arch_x86_64_ap_trampoline_blob(void)
{
    return __ap_trampoline_blob_start;
}

bool arch_x86_64_ap_trampoline_install(uint64_t phys_addr)
{
    if (phys_addr == 0 || phys_addr >= 0x100000ull) {
        /* AP boot needs the trampoline in low 1MB (real mode addressable). */
        return false;
    }
    uint8_t *dst = (uint8_t *)(uintptr_t)phys_addr;
    const uint8_t *src = __ap_trampoline_blob_start;
    uint64_t n = arch_x86_64_ap_trampoline_size();
    for (uint64_t i = 0; i < n; ++i) {
        dst[i] = src[i];
    }
    return true;
}

void arch_x86_64_ap_trampoline_set_cr3(uint64_t phys_addr, uint64_t cr3)
{
    volatile uint64_t *slot = (volatile uint64_t *)(uintptr_t)
        (phys_addr + OPENOS_AP_TRAMPOLINE_CR3_OFFSET);
    *slot = cr3;
}

void arch_x86_64_ap_trampoline_set_entry(uint64_t phys_addr, uint64_t entry)
{
    volatile uint64_t *slot = (volatile uint64_t *)(uintptr_t)
        (phys_addr + OPENOS_AP_TRAMPOLINE_ENTRY_OFFSET);
    *slot = entry;
}

bool arch_x86_64_ap_trampoline_verify(uint64_t phys_addr)
{
    if (phys_addr == 0 || phys_addr >= 0x100000ull) {
        return false;
    }
    const volatile uint8_t *p = (const volatile uint8_t *)(uintptr_t)phys_addr;
    if (p[OPENOS_AP_TRAMPOLINE_MAGIC_OFFSET + 0] != (uint8_t)OPENOS_AP_TRAMPOLINE_MAGIC0) return false;
    if (p[OPENOS_AP_TRAMPOLINE_MAGIC_OFFSET + 1] != (uint8_t)OPENOS_AP_TRAMPOLINE_MAGIC1) return false;
    if (p[OPENOS_AP_TRAMPOLINE_MAGIC_OFFSET + 2] != (uint8_t)OPENOS_AP_TRAMPOLINE_MAGIC2) return false;
    if (p[OPENOS_AP_TRAMPOLINE_MAGIC_OFFSET + 3] != (uint8_t)OPENOS_AP_TRAMPOLINE_MAGIC3) return false;
    if (p[OPENOS_AP_TRAMPOLINE_MAGIC_OFFSET + 4] != (uint8_t)OPENOS_AP_TRAMPOLINE_VERSION) return false;
    return true;
}
