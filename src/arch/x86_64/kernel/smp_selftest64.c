#include "../include/smp_selftest64.h"
#include "../include/smp64.h"
#include "../include/ap_trampoline64.h"
#include "../include/early_console64.h"

#include <stdint.h>
#include <stdbool.h>

/* Step G.4.1 — SMP topology dump (no AP wakeup). */

static void log_kv(const char *key, uint64_t val)
{
    early_console64_write(key);
    early_console64_write_hex64(val);
}

void arch_x86_64_smp_selftest_run(void)
{
    early_console64_write("\n[x86_64][smp-selftest] begin");

    if (!arch_x86_64_smp_init()) {
        early_console64_write("\n[x86_64][smp-selftest] FAIL smp_init (lapic not ready?)\n");
        return;
    }

    log_kv("\n[x86_64][smp-selftest] bsp_apic_id=", (uint64_t)arch_x86_64_smp_bsp_apic_id());
    log_kv(" cpu_count=", (uint64_t)arch_x86_64_smp_cpu_count());
    log_kv(" ap_count=",  (uint64_t)arch_x86_64_smp_ap_count());
    log_kv(" trampoline_phys=", arch_x86_64_smp_trampoline_phys());

    uint32_t n = arch_x86_64_smp_ap_count();
    for (uint32_t i = 0; i < n; ++i) {
        early_console64_write("\n[x86_64][smp-selftest] ap[");
        early_console64_write_hex64((uint64_t)i);
        early_console64_write("] apic_id=");
        early_console64_write_hex64((uint64_t)arch_x86_64_smp_ap_apic_id(i));
    }

    /* G.4.2: install AP trampoline blob and verify magic. */
    log_kv("\n[x86_64][smp-selftest] tramp_blob_size=", arch_x86_64_ap_trampoline_size());
    if (!arch_x86_64_smp_install_trampoline()) {
        early_console64_write("\n[x86_64][smp-selftest] FAIL trampoline install/verify\n");
        return;
    }
    early_console64_write("\n[x86_64][smp-selftest] trampoline installed @ ");
    early_console64_write_hex64(arch_x86_64_smp_trampoline_phys());

    /* G.4.3a: broadcast INIT IPI to every AP. No SIPI — APs simply enter
     * the "wait for SIPI" state and remain quiescent. We do not assert any
     * specific count under QEMU smp=1 (ap_count may be 0); we only assert
     * that ok == sent, i.e. every attempted ICR write was accepted by the
     * local APIC. */
    uint32_t sent = 0;
    uint32_t ok   = arch_x86_64_smp_send_init_all_aps(&sent);
    log_kv("\n[x86_64][smp-selftest] init_ipi_sent=", (uint64_t)sent);
    log_kv(" init_ipi_ok=", (uint64_t)ok);
    if (ok != sent) {
        early_console64_write("\n[x86_64][smp-selftest] FAIL init_ipi delivery\n");
        return;
    }

    /* G.4.3b-1: full INIT-SIPI-SIPI sequence. After this, any compliant AP
     * is executing at the trampoline page (currently a `cli; hlt` blob, so
     * APs simply halt). We assert ok == sent only — i.e. every IPI in the
     * three-step sequence was accepted by the local APIC. Verifying that
     * APs actually woke up requires an alive flag set by AP code, which
     * arrives with G.4.3b-2. */
    /* G.4.3b-2a: trampoline blob v2 now executes `lock inc byte [0x9000]`
     * before HLT. Zero the counter just before issuing INIT-SIPI-SIPI so any
     * non-zero value afterwards is attributable to AP code that actually ran.
     */
    arch_x86_64_smp_alive_reset();

    uint32_t sipi_sent = 0;
    uint32_t sipi_ok   = arch_x86_64_smp_send_startup_all_aps(&sipi_sent);
    log_kv("\n[x86_64][smp-selftest] sipi_seq_sent=", (uint64_t)sipi_sent);
    log_kv(" sipi_seq_ok=", (uint64_t)sipi_ok);
    if (sipi_ok != sipi_sent) {
        early_console64_write("\n[x86_64][smp-selftest] FAIL sipi sequence\n");
        return;
    }

    /* G.4.3b-2a: poll the alive counter with a 500ms timeout. Under QEMU each
     * AP typically bumps the counter exactly once before HLT (timer dependent).
     * We only require alive >= ap_count; on bare metal each AP may bump 1-2x. */
    uint32_t ap_n = arch_x86_64_smp_ap_count();
    uint8_t  expect = (ap_n > 0xFFu) ? 0xFFu : (uint8_t)ap_n;
    uint8_t  alive  = arch_x86_64_smp_alive_wait(expect, 500);
    log_kv("\n[x86_64][smp-selftest] ap_alive=", (uint64_t)alive);
    log_kv(" expected>=", (uint64_t)expect);
    if (ap_n > 0 && alive < expect) {
        early_console64_write("\n[x86_64][smp-selftest] FAIL ap alive\n");
        return;
    }

    early_console64_write("\n[x86_64][smp-selftest] PASS\n");
}
