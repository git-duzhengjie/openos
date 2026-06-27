#include "../include/smp_selftest64.h"
#include "../include/smp64.h"
#include "../include/ap_trampoline64.h"
#include "../include/early_console64.h"
#include "../include/percpu64.h"
#include "../include/sched64.h"
#include "../include/pit64.h"

#include <stdint.h>
#include <stdbool.h>

/* Step G.4.3b-2 — SMP three-stage trampoline verification.
 *
 * This selftest verifies end-to-end AP wakeup through all three stages:
 *   Stage 1: real mode     (alive_rm   >= ap_count)
 *   Stage 2: protected mode (alive_pm32 >= ap_count)
 *   Stage 3: long mode      (alive_lm64 >= ap_count)
 *
 * Under QEMU -smp N, we expect N-1 APs to reach each stage.
 */

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

    /* Debug: dump first 16 bytes of blob source */
    const uint8_t *blob_src = arch_x86_64_ap_trampoline_blob();
    for (int i = 0; i < 8; i++) {
        log_kv(" blob_src[", (uint64_t)i);
        early_console64_write("]=");
        early_console64_write_hex64((uint64_t)blob_src[i]);
    }

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

    /* G.4.3b-2: full INIT-SIPI-SIPI sequence. Before sending, zero all three
     * alive counters so we have a clean slate. */
    arch_x86_64_smp_alive_reset_all();

    /* G.5: backfill CR3 and entry into trampoline blob before SIPI. */
    arch_x86_64_smp_prepare_aps();

    uint32_t sipi_sent = 0;
    uint32_t sipi_ok   = arch_x86_64_smp_send_startup_all_aps(&sipi_sent);
    log_kv("\n[x86_64][smp-selftest] sipi_seq_sent=", (uint64_t)sipi_sent);
    log_kv(" sipi_seq_ok=", (uint64_t)sipi_ok);
    if (sipi_ok != sipi_sent) {
        early_console64_write("\n[x86_64][smp-selftest] FAIL sipi sequence\n");
        return;
    }

    /* G.4.3b-2: three-stage alive verification.
     * We poll each stage with 500ms timeout. APs should reach each stage
     * essentially instantaneously under QEMU, but the timeout handles
     * potential simulation slowdowns.
     */
    uint32_t ap_n = arch_x86_64_smp_ap_count();
    uint8_t  expect = (ap_n > 0xFFu) ? 0xFFu : (uint8_t)ap_n;

    /* Stage 1: real mode */
    uint8_t alive_rm = arch_x86_64_smp_alive_rm_wait(expect, 500);
    log_kv("\n[x86_64][smp-selftest] alive_rm=", (uint64_t)alive_rm);
    log_kv(" expected>=", (uint64_t)expect);
    if (ap_n > 0 && alive_rm < expect) {
        early_console64_write("\n[x86_64][smp-selftest] FAIL: AP stuck before real mode\n");
        return;
    }

    /* Stage 2: protected mode */
    uint8_t alive_pm32 = arch_x86_64_smp_alive_pm32_wait(expect, 500);
    log_kv("\n[x86_64][smp-selftest] alive_pm32=", (uint64_t)alive_pm32);
    log_kv(" expected>=", (uint64_t)expect);
    if (ap_n > 0 && alive_pm32 < expect) {
        early_console64_write("\n[x86_64][smp-selftest] FAIL: AP stuck in real mode\n");
        return;
    }

    /* Stage 3: long mode */
    uint8_t alive_lm64 = arch_x86_64_smp_alive_lm64_wait(expect, 500);
    log_kv("\n[x86_64][smp-selftest] alive_lm64=", (uint64_t)alive_lm64);
    log_kv(" expected>=", (uint64_t)expect);
    if (ap_n > 0 && alive_lm64 < expect) {
        early_console64_write("\n[x86_64][smp-selftest] FAIL: AP stuck in protected mode\n");
        return;
    }

    /* Stage 4: per-AP LAPIC bring-up (G.5-lapic). */
    uint8_t alive_lapic = arch_x86_64_smp_alive_lapic_wait(expect, 500);
    log_kv("\n[x86_64][smp-selftest] alive_lapic=", (uint64_t)alive_lapic);
    log_kv(" expected>=", (uint64_t)expect);
    if (ap_n > 0 && alive_lapic < expect) {
        early_console64_write("\n[x86_64][smp-selftest] FAIL: AP LAPIC init incomplete\n");
        return;
    }

    /* Stage 5: per-AP GDT+TSS installed (G.5-gdt-tss). */
    uint8_t alive_percpu = arch_x86_64_smp_alive_percpu_wait(expect, 500);
    log_kv("\n[x86_64][smp-selftest] alive_percpu=", (uint64_t)alive_percpu);
    log_kv(" expected>=", (uint64_t)expect);
    if (ap_n > 0 && alive_percpu < expect) {
        early_console64_write("\n[x86_64][smp-selftest] FAIL: AP per-CPU GDT/TSS load incomplete\n");
        return;
    }

    /* Stage 6: per-AP IDTR installed + AP idle loop reached (G.6.1). */
    uint8_t alive_idle = arch_x86_64_smp_alive_idle_wait(expect, 500);
    log_kv("\n[x86_64][smp-selftest] alive_idle=", (uint64_t)alive_idle);
    log_kv(" expected>=", (uint64_t)expect);
    if (ap_n > 0 && alive_idle < expect) {
        early_console64_write("\n[x86_64][smp-selftest] FAIL: AP did not reach idle loop\n");
        return;
    }

    /* Stage 7: per-CPU GS_BASE installed on every AP (G.6.2). */
    uint8_t alive_gs = arch_x86_64_smp_alive_gs_wait(expect, 500);
    log_kv("\n[x86_64][smp-selftest] alive_gs=", (uint64_t)alive_gs);
    log_kv(" expected>=", (uint64_t)expect);
    if (ap_n > 0 && alive_gs < expect) {
        early_console64_write("\n[x86_64][smp-selftest] FAIL: AP did not install per-CPU GS_BASE\n");
        return;
    }

    /* G.6.2 bonus: BSP-side GS_BASE sanity. From kernel64_main we installed
     * GS for cpu_idx=0 right after tss_load; verify the self-pointer and
     * magic round-trip through %gs:0. */
    if (!arch_x86_64_percpu_gs_ok()) {
        early_console64_write("\n[x86_64][smp-selftest] FAIL: BSP GS_BASE not consistent\n");
        return;
    }
    arch_x86_64_percpu_t *bsp_pcpu = arch_x86_64_this_cpu_ptr();
    log_kv("\n[x86_64][smp-selftest] bsp_pcpu_self=", (uint64_t)(uintptr_t)bsp_pcpu);
    log_kv(" cpu_idx=", (uint64_t)arch_x86_64_this_cpu_idx());
    log_kv(" magic=",   (uint64_t)arch_x86_64_this_cpu_magic());
    if (arch_x86_64_this_cpu_idx() != 0 ||
        arch_x86_64_this_cpu_magic() != OPENOS_X86_64_PERCPU_MAGIC ||
        bsp_pcpu != arch_x86_64_percpu_slot(0)) {
        early_console64_write("\n[x86_64][smp-selftest] FAIL: BSP gs:0 not pointing at percpu[0]\n");
        return;
    }

    /* Stage 8: per-CPU idle slot registered in the scheduler (G.6.4).
     * Each AP, after installing its private GS_BASE, calls
     * sched_init_ap() + sched_register_ap_idle() and bumps the
     * alive_sched counter. The BSP then runs sched_idle_selftest() to
     * confirm every online CPU owns exactly one is_idle RUNNING slot. */
    uint8_t alive_sched = arch_x86_64_smp_alive_sched_wait(expect, 500);
    log_kv("\n[x86_64][smp-selftest] alive_sched=", (uint64_t)alive_sched);
    log_kv(" expected>=", (uint64_t)expect);
    if (ap_n > 0 && alive_sched < expect) {
        early_console64_write("\n[x86_64][smp-selftest] FAIL: AP did not register idle slot\n");
        return;
    }

    uint32_t online_cpus = ap_n + 1u;
    uint32_t idle_rc = arch_x86_64_sched_idle_selftest(online_cpus);
    log_kv("\n[x86_64][smp-selftest] sched_idle_selftest=", (uint64_t)idle_rc);
    log_kv(" online_cpus=", (uint64_t)online_cpus);
    if (idle_rc != 0u) {
        early_console64_write("\n[x86_64][smp-selftest] FAIL: sched_idle_selftest non-zero\n");
        return;
    }

    /* G.6.5a Stage 9: every AP must be receiving its own LAPIC-timer
     * interrupts. Each AP arms its LAPIC timer (periodic, vector 0x40,
     * DCR=div16, ICR=10_000_000) right before its sti/hlt loop, so by the
     * time we reach this stage the ISR has already been bumping each
     * AP's percpu.lapic_timer_count. We require at least 2 ticks per AP
     * to avoid flagging a single stray interrupt as success.
     *
     * The BSP slot is expected to stay at 0 — we explicitly do NOT
     * program the BSP's LAPIC timer in G.6.5a (the BSP scheduler still
     * runs off PIT IRQ0). Asserting bsp_ticks==0 is part of the contract
     * for this step: a non-zero BSP count would mean a stray AP IRQ was
     * misrouted, which would be a real bug.
     *
     * Polling budget: ~500 ms total. We don't have a precise wait helper
     * for this counter, so we spin on PIT-derived jiffies. */
    if (ap_n > 0) {
        const uint64_t timer_min = 2u;
        uint64_t deadline = arch_x86_64_pit_get_ticks() + 500u;
        bool all_ticking = false;
        while (arch_x86_64_pit_get_ticks() < deadline) {
            all_ticking = true;
            for (uint32_t i = 1; i <= ap_n; ++i) {
                if (arch_x86_64_smp_lapic_timer_count(i) < timer_min) {
                    all_ticking = false;
                    break;
                }
            }
            if (all_ticking) break;
            __asm__ volatile ("pause");
        }
        for (uint32_t i = 0; i <= ap_n; ++i) {
            log_kv("\n[x86_64][smp-selftest] lapic_timer[", (uint64_t)i);
            log_kv("] count=", arch_x86_64_smp_lapic_timer_count(i));
        }
        uint64_t bsp_ticks = arch_x86_64_smp_lapic_timer_count(0);
        if (bsp_ticks != 0u) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: BSP LAPIC timer fired (should be 0 in G.6.5a)\n");
            return;
        }
        if (!all_ticking) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: at least one AP LAPIC timer not ticking\n");
            return;
        }
    }

    /* G.6.5b Stage 10: each AP's LAPIC-timer ISR now drives sched_on_tick.
     * We verify that the per-CPU sched_tick_calls counter (bumped at the
     * very entry of sched_on_tick) advances on every CPU:
     *   - BSP (cpu 0): driven by PIT IRQ0, must have advanced since boot.
     *   - Each AP:     driven by LAPIC-timer vector 0x40, must have
     *                  advanced at least once per AP.
     *
     * This is the proof that sched_on_tick is reachable from each CPU's
     * *own* IRQ path (not just cooperative yields), and that the per-CPU
     * cursors / quantum / preempt counters are being driven independently.
     *
     * Polling budget: another ~500 ms. We're tolerant here (min=1 per AP)
     * because the LAPIC timer cadence is ~6 Hz in QEMU and we already
     * burned half a second in stage 9 waiting for raw ticks. */
    {
        const uint64_t sched_min = 1u;
        uint64_t deadline = arch_x86_64_pit_get_ticks() + 500u;
        bool all_ticking = false;
        while (arch_x86_64_pit_get_ticks() < deadline) {
            all_ticking = true;
            /* BSP: must always be advancing (PIT-driven). */
            if (arch_x86_64_sched_tick_calls_for_cpu(0) == 0u) {
                all_ticking = false;
            }
            for (uint32_t i = 1; i <= ap_n && all_ticking; ++i) {
                if (arch_x86_64_sched_tick_calls_for_cpu(i) < sched_min) {
                    all_ticking = false;
                    break;
                }
            }
            if (all_ticking) break;
            __asm__ volatile ("pause");
        }
        for (uint32_t i = 0; i <= ap_n; ++i) {
            log_kv("\n[x86_64][smp-selftest] sched_tick_calls[", (uint64_t)i);
            log_kv("]=", arch_x86_64_sched_tick_calls_for_cpu(i));
        }
        if (!all_ticking) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: sched_on_tick not reachable on every CPU's own IRQ path\n");
            return;
        }
    }

    /* All stages reached by all APs. */
    if (ap_n > 0) {
        early_console64_write("\n[x86_64][smp-selftest] PASS: all APs idle on private GDT/TSS/IDT+GS + sched-registered + LAPIC-timer driving sched_on_tick\n");
    } else {
        early_console64_write("\n[x86_64][smp-selftest] PASS: no APs (UP system, BSP idle slot only)\n");
    }
}
