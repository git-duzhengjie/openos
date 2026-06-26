#include "../include/smp64.h"
#include "../include/acpi64.h"
#include "../include/lapic64.h"
#include "../include/ap_trampoline64.h"
#include "../include/delay64.h"

#include <stdint.h>
#include <stdbool.h>

/* Step G.4.1 — SMP topology snapshot.
 * No AP wakeup is performed here; this only consumes ACPI MADT data and
 * captures the BSP apic_id at runtime so later stages have authoritative
 * input. See smp64.h for the staged plan. */

typedef struct smp_state {
    bool     ready;
    bool     trampoline_installed;
    uint8_t  bsp_apic_id;
    uint32_t cpu_count;          /* enabled CPUs incl. BSP */
    uint32_t ap_count;
    uint8_t  ap_apic_ids[OPENOS_X86_64_SMP_MAX_CPUS];
} smp_state_t;

static smp_state_t g_smp;

bool arch_x86_64_smp_init(void) {
    g_smp.ready = false;
    g_smp.bsp_apic_id = 0;
    g_smp.cpu_count = 0;
    g_smp.ap_count = 0;

    /* Capture BSP apic_id from LAPIC ID register (must be after lapic init). */
    if (!arch_x86_64_lapic_is_ready()) {
        return false;
    }
    g_smp.bsp_apic_id = (uint8_t)arch_x86_64_lapic_id();

    const arch_x86_64_acpi_info_t *acpi = arch_x86_64_acpi_info();
    if (acpi == 0 || acpi->cpu_count == 0) {
        /* No ACPI: still mark ready as a UP system. */
        g_smp.cpu_count = 1;
        g_smp.ap_count = 0;
        g_smp.ready = true;
        return true;
    }

    uint32_t enabled = 0;
    uint32_t ap_idx = 0;
    for (uint32_t i = 0; i < acpi->cpu_count; ++i) {
        const acpi_cpu_entry_t *cpu = &acpi->cpus[i];
        if ((cpu->flags & 0x1u) == 0u) {
            continue;
        }
        enabled++;
        if (cpu->apic_id == g_smp.bsp_apic_id) {
            continue;
        }
        if (ap_idx < OPENOS_X86_64_SMP_MAX_CPUS) {
            g_smp.ap_apic_ids[ap_idx++] = cpu->apic_id;
        }
    }
    g_smp.cpu_count = enabled;
    g_smp.ap_count  = ap_idx;
    g_smp.ready = true;
    return true;
}

bool arch_x86_64_smp_is_ready(void) { return g_smp.ready; }
uint8_t  arch_x86_64_smp_bsp_apic_id(void) { return g_smp.bsp_apic_id; }
uint32_t arch_x86_64_smp_ap_count(void) { return g_smp.ap_count; }
uint32_t arch_x86_64_smp_cpu_count(void) { return g_smp.cpu_count; }

uint8_t arch_x86_64_smp_ap_apic_id(uint32_t index) {
    if (index >= g_smp.ap_count) {
        return 0xFFu;
    }
    return g_smp.ap_apic_ids[index];
}

uint64_t arch_x86_64_smp_trampoline_phys(void) {
    /* Fixed low-1MB landing zone. G.4.2 installs blob here. */
    return OPENOS_X86_64_SMP_TRAMPOLINE_PHYS;
}

bool arch_x86_64_smp_install_trampoline(void) {
    uint64_t phys = OPENOS_X86_64_SMP_TRAMPOLINE_PHYS;
    if (!arch_x86_64_ap_trampoline_install(phys)) {
        g_smp.trampoline_installed = false;
        return false;
    }
    if (!arch_x86_64_ap_trampoline_verify(phys)) {
        g_smp.trampoline_installed = false;
        return false;
    }
    g_smp.trampoline_installed = true;
    return true;
}

bool arch_x86_64_smp_trampoline_installed(void) {
    return g_smp.trampoline_installed;
}

/* G.4.3a — broadcast INIT IPI to every AP we discovered. No SIPI, no pause:
 * the APs enter the INIT "wait for SIPI" state and stay quiescent. Returns
 * the number of APs whose ICR write completed successfully; out-param
 * 'sent' is the number we attempted. Safe to call with ap_count == 0. */
uint32_t arch_x86_64_smp_send_init_all_aps(uint32_t *out_sent) {
    uint32_t sent = 0, ok = 0;
    if (!g_smp.ready) {
        if (out_sent) *out_sent = 0;
        return 0;
    }
    for (uint32_t i = 0; i < g_smp.ap_count; ++i) {
        uint8_t id = g_smp.ap_apic_ids[i];
        sent++;
        if (arch_x86_64_lapic_send_init(id)) {
            ok++;
        }
    }
    if (out_sent) *out_sent = sent;
    return ok;
}

/* G.4.3b-1 — issue INIT-SIPI-SIPI sequence to every AP. The trampoline must
 * already be installed (we read its physical address back from smp state).
 * Per Intel SDM 10.4.4.1 "Universal Algorithm":
 *   1. send INIT (assert)
 *   2. wait 10 ms
 *   3. send STARTUP (vec = trampoline >> 12)
 *   4. wait 200 us
 *   5. send STARTUP again
 * We count an AP as "ok" only if all three IPIs (INIT + SIPI + SIPI) deliver.
 * The trampoline page in G.4.3b-1 is still a `cli; hlt` blob, so APs end up
 * halted at the trampoline address — verifiable via QEMU monitor `info cpus`. */
uint32_t arch_x86_64_smp_send_startup_all_aps(uint32_t *out_sent) {
    uint32_t sent = 0, ok = 0;
    if (!g_smp.ready || !g_smp.trampoline_installed) {
        if (out_sent) *out_sent = 0;
        return 0;
    }

    /* The trampoline lives at the fixed low-1MB page chosen by
     * arch_x86_64_smp_install_trampoline(); ap_trampoline64 exposes it. */
    uint64_t tramp_phys = arch_x86_64_smp_trampoline_phys();
    if (tramp_phys == 0 || tramp_phys >= 0x100000ULL ||
        (tramp_phys & 0xFFFULL) != 0) {
        /* trampoline must be 4KB-aligned and below 1MB for SIPI vector. */
        if (out_sent) *out_sent = 0;
        return 0;
    }
    uint8_t vector = (uint8_t)(tramp_phys >> 12);

    for (uint32_t i = 0; i < g_smp.ap_count; ++i) {
        uint8_t id = g_smp.ap_apic_ids[i];
        sent++;

        if (!arch_x86_64_lapic_send_init(id)) continue;
        arch_x86_64_delay_ms(10);

        if (!arch_x86_64_lapic_send_startup(id, vector)) continue;
        arch_x86_64_delay_us(200);

        if (!arch_x86_64_lapic_send_startup(id, vector)) continue;

        ok++;
    }
    if (out_sent) *out_sent = sent;
    return ok;
}

/* G.4.3b-2a — alive counter at phys 0x9000. The AP trampoline blob v2
 * issues `lock inc byte [0x9000]` before halting, so this byte tracks how
 * many wake events were observed (each AP may bump it 1–2 times depending
 * on whether the second SIPI lands before HLT). The BSP zeroes it before
 * INIT-SIPI-SIPI and polls it afterwards. */
void arch_x86_64_smp_alive_reset(void) {
    volatile uint8_t *p = (volatile uint8_t *)(uintptr_t)OPENOS_X86_64_SMP_ALIVE_PHYS;
    *p = 0;
}

uint8_t arch_x86_64_smp_alive_count(void) {
    const volatile uint8_t *p = (const volatile uint8_t *)(uintptr_t)OPENOS_X86_64_SMP_ALIVE_PHYS;
    return *p;
}

uint8_t arch_x86_64_smp_alive_wait(uint8_t expected, uint32_t timeout_ms) {
    /* Poll in 1ms slices using the TSC-based delay primitive. */
    uint32_t elapsed = 0;
    for (;;) {
        uint8_t cur = arch_x86_64_smp_alive_count();
        if (cur >= expected) return cur;
        if (elapsed >= timeout_ms) return cur;
        arch_x86_64_delay_ms(1);
        elapsed++;
    }
}
