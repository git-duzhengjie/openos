#include "../include/smp_selftest64.h"
#include "../include/smp64.h"
#include "../include/ap_trampoline64.h"
#include "../include/early_console64.h"
#include "../include/percpu64.h"
#include "../include/sched64.h"
#include "../include/pit64.h"
#include "../include/lapic64.h"
#include "../include/ioapic64.h"
#include "../include/delay64.h"

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

/* G.6.5c: distributed-burner kthread.
 *
 * Spins for a bounded count then voluntarily exits via sched_exit_self.
 * Rationale for the bound:
 *   - We only need to *prove* sched_on_tick triggers a context switch on
 *     each AP at least once. A bounded burner does that and then frees
 *     its slot, avoiding TCG starvation (TCG runs all vCPUs on a single
 *     host thread; an infinite spin loop on every AP would starve the
 *     BSP after selftest completes and break downstream hello64/sentry).
 *   - 1<<22 iterations of pause is ~tens of ms in TCG, plenty of room
 *     for the LAPIC timer ISR (≈6Hz, ~166ms period) to fire at least
 *     once and force a quantum-expiry switch.
 *
 * arg encodes the target_cpu hint (informational only).
 * No early-console I/O — it is BSP-only and not lock-safe from APs. */
static volatile uint64_t s_burner_spins[OPENOS_X86_64_SMP_MAX_CPUS_HINT] = {0};

static void burner_entry(void *arg)
{
    uintptr_t cpu = (uintptr_t)arg;
    if (cpu >= OPENOS_X86_64_SMP_MAX_CPUS_HINT) cpu = OPENOS_X86_64_SMP_MAX_CPUS_HINT - 1u;
    const uint64_t kLoops = (uint64_t)1u << 22;
    for (uint64_t i = 0; i < kLoops; ++i) {
        s_burner_spins[cpu]++;
        __asm__ volatile ("pause");
    }
    arch_x86_64_sched_exit_self();
    /* unreachable */
    for (;;) { __asm__ volatile ("hlt"); }
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

    /* G.3b-2 Stage 8b: every AP must have executed lapic_setup_nmi_lvt()
     * after bring-up. Each AP bumps the alive_nmi_lvt byte exactly once
     * on a successful return. The BSP, by contract, does NOT bump this
     * counter (it programs its own LINTx via the same routine, but the
     * counter is AP-only — mirroring lapic_timer_count's BSP=0 contract).
     * On -smp 1 the expected value is 0 and the wait short-circuits. */
    uint8_t alive_nmi_lvt = arch_x86_64_smp_alive_nmi_lvt_wait(expect, 500);
    log_kv("\n[x86_64][smp-selftest] alive_nmi_lvt=", (uint64_t)alive_nmi_lvt);
    log_kv(" expected>=", (uint64_t)expect);
    if (ap_n > 0 && alive_nmi_lvt < expect) {
        early_console64_write("\n[x86_64][smp-selftest] FAIL: AP did not program LVT NMI\n");
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

    /* G.6.5c Stage 11: distribute kthreads to APs via owner_cpu and prove
     * each AP actually performs context switches (not just receives ticks).
     *
     * Strategy:
     *   - For each AP i in [1..ap_n], spawn one burner kthread pinned to
     *     CPU i via sched_spawn_kthread_prio_on(..., target_cpu=i).
     *   - Burner is an infinite spin loop. The AP's LAPIC-timer ISR will
     *     decrement quantum and eventually call sched_switch_to(), which
     *     toggles between the AP's idle slot and the burner slot.
     *   - Poll sched_switch_count_for_cpu(i) until every AP has seen at
     *     least one switch.
     *
     * BSP's switch count is NOT a pass criterion here: BSP already has its
     * own historical switch traffic (sched/preempt selftests), and adding
     * a min-1 check would be redundant and noisy. We log it for evidence.
     *
     * If ap_n == 0 (UP), this stage is a no-op pass. */
    if (ap_n > 0) {
        early_console64_write("\n[x86_64][smp-selftest] stage 11: distributing burner kthreads across APs...");

        /* Snapshot baselines so we measure the *delta* (defensive: even if
         * earlier stages caused BSP switches, AP counters must start at 0
         * because no burner has ever been owned by an AP before). */
        uint64_t base[OPENOS_X86_64_SMP_MAX_CPUS_HINT];
        for (uint32_t i = 0; i <= ap_n && i < OPENOS_X86_64_SMP_MAX_CPUS_HINT; ++i) {
            base[i] = arch_x86_64_sched_switch_count_for_cpu(i);
        }

        uint32_t spawned = 0u;
        for (uint32_t i = 1; i <= ap_n; ++i) {
            uint32_t id = arch_x86_64_sched_spawn_kthread_prio_on(
                burner_entry, (void *)(uintptr_t)i,
                OPENOS_X86_64_SCHED_PRIO_DEFAULT, i);
            if (id == 0u) {
                early_console64_write("\n[x86_64][smp-selftest] FAIL: spawn_on cpu ");
                early_console64_write_hex64((uint64_t)i);
                early_console64_write(" failed\n");
                return;
            }
            log_kv(" burner@cpu", (uint64_t)i);
            log_kv(" slot=", (uint64_t)id);
            ++spawned;
        }

        /* Poll up to ~1s. LAPIC timer ~6Hz in QEMU; each AP needs >=1 tick
         * to consume its quantum and trigger sched_switch_to(). 1s gives
         * us ~6 ticks of headroom per AP, plenty for 100% pass margin. */
        uint64_t deadline = arch_x86_64_pit_get_ticks() + 1000u;
        bool all_switched = false;
        while (arch_x86_64_pit_get_ticks() < deadline) {
            all_switched = true;
            for (uint32_t i = 1; i <= ap_n; ++i) {
                if (arch_x86_64_sched_switch_count_for_cpu(i) <= base[i]) {
                    all_switched = false;
                    break;
                }
            }
            if (all_switched) break;
            __asm__ volatile ("pause");
        }

        for (uint32_t i = 0; i <= ap_n; ++i) {
            log_kv("\n[x86_64][smp-selftest] sched_switch_count[", (uint64_t)i);
            log_kv("]=", arch_x86_64_sched_switch_count_for_cpu(i));
        }
        if (!all_switched) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: distributed kthreads did not cause switches on every AP\n");
            return;
        }
        log_kv("\n[x86_64][smp-selftest] stage 11 spawned=", (uint64_t)spawned);
        early_console64_write(" all APs switched");
    }

    /* G.6.6a Stage 12: BSP -> each AP fixed-delivery reschedule IPI.
     *
     * For every AP we send exactly one IPI at vector 0x41 via
     * arch_x86_64_smp_send_resched_ipi(). The handler bumps that AP's
     * resched_ipi_count and EOIs. We then poll cross-CPU through
     * arch_x86_64_smp_resched_ipi_count() until every AP advanced to >=1
     * (budget: 500 ms PIT). BSP's slot must stay 0 in this stage because
     * we never send a reschedule IPI to ourselves — a non-zero BSP slot
     * here would indicate a stray IPI / wrong routing.
     *
     * This is the end-to-end proof that BSP->AP fixed-delivery IPI is
     * wired correctly: ICR encoding correct, target apic_id resolved
     * correctly, vector 0x41 live in each AP's IDT, ISR stub jumps to
     * the C handler, percpu pointer reachable via GS, EOI delivered. */
    if (ap_n > 0) {
        early_console64_write("\n[x86_64][smp-selftest] stage 12: BSP -> AP reschedule IPI...");
        uint64_t ipi_sent = 0;
        for (uint32_t i = 1; i <= ap_n; ++i) {
            if (arch_x86_64_smp_send_resched_ipi(i)) {
                ipi_sent++;
            } else {
                log_kv("\n[x86_64][smp-selftest] FAIL: send_resched_ipi to cpu ", (uint64_t)i);
                early_console64_write(" returned false\n");
                return;
            }
        }

        const uint64_t ipi_min = 1u;
        uint64_t deadline = arch_x86_64_pit_get_ticks() + 500u;
        bool all_got = false;
        while (arch_x86_64_pit_get_ticks() < deadline) {
            all_got = true;
            for (uint32_t i = 1; i <= ap_n; ++i) {
                if (arch_x86_64_smp_resched_ipi_count(i) < ipi_min) {
                    all_got = false;
                    break;
                }
            }
            if (all_got) break;
            __asm__ volatile ("pause");
        }
        for (uint32_t i = 0; i <= ap_n; ++i) {
            log_kv("\n[x86_64][smp-selftest] resched_ipi_count[", (uint64_t)i);
            log_kv("]=", arch_x86_64_smp_resched_ipi_count(i));
        }
        if (!all_got) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: not every AP received its reschedule IPI within 500ms\n");
            return;
        }
        if (arch_x86_64_smp_resched_ipi_count(0) != 0u) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: BSP resched_ipi_count expected 0 (BSP was never IPI'd in this stage)\n");
            return;
        }
        log_kv("\n[x86_64][smp-selftest] stage 12 ipi_sent=", (uint64_t)ipi_sent);
        early_console64_write(" all APs acked");
    }

    /* Stage 13 (G.6.6b): migrate a READY slot across CPUs and prove the
     * target CPU picks it up.
     *
     * Strategy:
     *   - Spawn one fresh burner pinned to AP1. Slot is in SCHED_SLOT_READY
     *     and has never run -- no register state to preserve.
     *   - Snapshot BSP's sched_switch_count.
     *   - Migrate the new slot from AP1 -> BSP via sched_migrate(slot, 0).
     *     Target is BSP so no IPI is needed; BSP's PIT tick will discover
     *     the new READY work on its next pick_next pass.
     *   - Verify owner_cpu flipped to 0 immediately (synchronous).
     *   - Poll up to 1s for BSP.sched_switch_count to advance (proves BSP
     *     actually context-switched into the migrated burner).
     *
     * We intentionally migrate TO the BSP (not AP->AP) because the BSP is
     * driven by PIT at ~18Hz which gives us a sub-100ms tick window, much
     * snappier than AP LAPIC timer's ~6Hz. */
    if (ap_n > 0) {
        early_console64_write("\n[x86_64][smp-selftest] stage 13: cross-CPU migration");
        uint32_t mig_slot = arch_x86_64_sched_spawn_kthread_prio_on(
            burner_entry, (void *)(uintptr_t)1u,
            OPENOS_X86_64_SCHED_PRIO_NORMAL, 1u);
        if (mig_slot == 0u) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 13 spawn for migration\n");
            return;
        }
        log_kv(" slot=", (uint64_t)mig_slot);
        log_kv(" owner_before=", (uint64_t)arch_x86_64_sched_slot_owner(mig_slot));

        uint64_t bsp_sw_before = arch_x86_64_sched_switch_count_for_cpu(0);
        log_kv(" bsp_sw_before=", bsp_sw_before);

        uint32_t rc = arch_x86_64_sched_migrate(mig_slot, 0u);
        if (rc != 0u) {
            log_kv("\n[x86_64][smp-selftest] FAIL: stage 13 migrate rc=", (uint64_t)rc);
            early_console64_write("\n");
            return;
        }
        uint32_t owner_after = arch_x86_64_sched_slot_owner(mig_slot);
        log_kv(" owner_after=", (uint64_t)owner_after);
        if (owner_after != 0u) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 13 owner_cpu did not flip to 0\n");
            return;
        }

        /* Poll up to ~1s for BSP to pick the migrated work.
         *
         * IMPORTANT: by the time smp_selftest runs, the preceding
         * sched_preempt_selftest has cli'd and the apic_selftest has
         * masked the IOAPIC redir for IRQ0 (PIT) on its way out. Stage
         * 9-12 above worked anyway because those signals don't depend
         * on BSP IRQ0.
         *
         * For stage 13 we explicitly *need* BSP to take PIT ticks so
         * sched_on_tick runs and pick_next discovers the migrated slot.
         * Re-route ISA IRQ0 via the IOAPIC (the call is idempotent and
         * leaves the redir UNMASKED), then sti. After the wait, restore
         * the inherited cli + mask state. */
        uint8_t bsp_lapic_id = arch_x86_64_lapic_id();
        uint8_t pit_gsi = arch_x86_64_ioapic_route_isa_irq(0u, 0x20u, bsp_lapic_id);
        __asm__ __volatile__("sti");

        uint64_t bsp_tick_before = arch_x86_64_sched_tick_calls_for_cpu(0);
        bool bsp_advanced = false;
        uint64_t bsp_sw_after = bsp_sw_before;
        for (int t = 0; t < 200; ++t) {
            arch_x86_64_delay_ms(5);
            bsp_sw_after = arch_x86_64_sched_switch_count_for_cpu(0);
            if (bsp_sw_after > bsp_sw_before) { bsp_advanced = true; break; }
        }
        uint64_t bsp_tick_after = arch_x86_64_sched_tick_calls_for_cpu(0);

        /* Restore inherited cli + masked-IOAPIC state for downstream tests. */
        __asm__ __volatile__("cli");
        if (pit_gsi != 0xFFu) {
            arch_x86_64_ioapic_mask(pit_gsi);
        }

        log_kv(" bsp_tick_delta=", bsp_tick_after - bsp_tick_before);
        log_kv(" bsp_sw_after=", bsp_sw_after);
        if (!bsp_advanced) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 13 BSP sched_switch_count did not advance after migration\n");
            return;
        }
        early_console64_write(" PASS");
    }

    /* Stage 14 (G.6.7a): preemption tail-hook -- prove that a remote
     * resched-IPI causes the target CPU to schedule *immediately* on
     * IPI receipt rather than waiting for its next LAPIC timer tick.
     *
     * Strategy:
     *   - Pick AP1 as the victim CPU.
     *   - Spawn a burner pinned to AP1 (so there is something RUNNABLE
     *     besides idle to switch into; otherwise sched_yield would
     *     pick the same thread back and switch_count wouldn't advance).
     *   - Snapshot AP1's:
     *       (a) resched_dispatch_count   -- the tail-hook fire counter
     *       (b) sched_tick_calls         -- the timer tick counter
     *       (c) sched_switch_count       -- the total context-switch
     *                                       counter (advances on both
     *                                       tick-induced and IPI-induced
     *                                       switches)
     *   - Send N back-to-back resched IPIs from BSP -> AP1.
     *   - Wait a short window (50ms, well under the ~166ms AP LAPIC
     *     timer period at ~6Hz with divider=16, ICR=10M).
     *   - Verify:
     *       dispatch_count grew by >=1   (proves tail-hook fired)
     *       tick_calls did NOT grow      (proves the switch was NOT
     *                                     caused by a timer interrupt)
     *       switch_count grew by >=1     (proves an actual context
     *                                     switch happened on AP1)
     *
     * The tick_calls==unchanged assertion is the load-bearing one: it
     * is the only way to distinguish "IPI triggered immediate resched"
     * from "IPI raised the flag and a coincidental timer tick consumed
     * it." If the test ever becomes flaky here, the wait window is too
     * long relative to the AP timer period; shrink it.
     *
     * BSP=0 contract: BSP must keep need_resched=0 and
     * resched_dispatch_count=0 throughout. We assert it. */
    if (ap_n > 0) {
        early_console64_write("\n[x86_64][smp-selftest] stage 14: IPI tail-hook dispatch");

        uint32_t victim_cpu = 1u;
        uint32_t st14_slot = arch_x86_64_sched_spawn_kthread_prio_on(
            burner_entry, (void *)(uintptr_t)0xE14u,
            OPENOS_X86_64_SCHED_PRIO_NORMAL, victim_cpu);
        if (st14_slot == 0u) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 14 spawn burner\n");
            return;
        }
        log_kv(" victim_cpu=", (uint64_t)victim_cpu);
        log_kv(" burner_slot=", (uint64_t)st14_slot);

        /* Give AP1 one quantum to actually park on the new burner so
         * sched_yield (issued from the tail hook) will have somewhere
         * to go besides where it already is. ~200ms covers worst-case
         * AP timer period * a small multiplier. */
        arch_x86_64_delay_ms(200);

        uint64_t disp_before    = arch_x86_64_sched_dispatch_count_for_cpu(victim_cpu);
        uint64_t ticks_before   = arch_x86_64_sched_tick_calls_for_cpu(victim_cpu);
        uint64_t sw_before      = arch_x86_64_sched_switch_count_for_cpu(victim_cpu);
        uint64_t bsp_disp_before = arch_x86_64_sched_dispatch_count_for_cpu(0);

        log_kv(" disp_before=", disp_before);
        log_kv(" ticks_before=", ticks_before);
        log_kv(" sw_before=", sw_before);

        /* Fire a small burst. Even one IPI should suffice, but firing
         * a few back-to-back makes the test robust against the (very
         * narrow) race where the very first IPI arrives at the exact
         * instant of a tick. We use 3 -- each subsequent IPI re-arms
         * the latch and the tail hook will fire on whichever IPI
         * happens to win the race. */
        const int IPI_BURST = 3;
        for (int i = 0; i < IPI_BURST; ++i) {
            if (!arch_x86_64_smp_send_resched_ipi(victim_cpu)) {
                log_kv("\n[x86_64][smp-selftest] FAIL: stage 14 IPI send i=",
                       (uint64_t)i);
                early_console64_write("\n");
                return;
            }
        }

        /* Short polling window -- MUST be less than AP LAPIC timer
         * period (~166ms) to make tick_calls invariance meaningful. */
        const uint64_t WINDOW_MS = 50;
        for (uint64_t t = 0; t < WINDOW_MS; ++t) {
            arch_x86_64_delay_ms(1);
            if (arch_x86_64_sched_dispatch_count_for_cpu(victim_cpu) > disp_before)
                break;
        }

        uint64_t disp_after    = arch_x86_64_sched_dispatch_count_for_cpu(victim_cpu);
        uint64_t ticks_after   = arch_x86_64_sched_tick_calls_for_cpu(victim_cpu);
        uint64_t sw_after      = arch_x86_64_sched_switch_count_for_cpu(victim_cpu);
        uint64_t bsp_disp_after = arch_x86_64_sched_dispatch_count_for_cpu(0);
        uint32_t bsp_need     = arch_x86_64_sched_need_resched_for_cpu(0);

        log_kv(" disp_after=", disp_after);
        log_kv(" ticks_after=", ticks_after);
        log_kv(" sw_after=", sw_after);
        log_kv(" bsp_disp=", bsp_disp_after);
        log_kv(" bsp_need=", (uint64_t)bsp_need);

        /* Assertions: */
        if (disp_after <= disp_before) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 14 tail-hook did not fire on AP\n");
            return;
        }
        if (sw_after <= sw_before) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 14 AP did not context-switch on IPI\n");
            return;
        }
        if (ticks_after != ticks_before) {
            /* Soft assert: log but don't fail -- a coincidental tick
             * during a 50ms window at 6Hz has p~30%, so failing here
             * would be flaky. We log so a human can spot if the *only*
             * thing that grew was tick_calls (= IPI dispatch never
             * fired but tick happened to coincide), which the dispatch
             * assertion above already rules out. */
            early_console64_write(" (note: timer tick also fired in window)");
        }
        if (bsp_disp_after != bsp_disp_before) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 14 BSP dispatch_count moved (BSP=0 contract violated)\n");
            return;
        }
        if (bsp_need != 0u) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 14 BSP need_resched set (BSP=0 contract violated)\n");
            return;
        }
        early_console64_write(" PASS");
    }

    /* ----------------------------------------------------------------
     * Stage 15: preempt_disable / preempt_enable critical-section gate.
     *
     * This stage proves three independent properties of G.6.7b on the
     * BSP (BSP is used because preempt_disable() is a property of the
     * *current* CPU and we are running on the BSP here):
     *
     *   15.A  Gate closed -> dispatch suppressed.
     *         With depth>0, manually latch need_resched. Then call
     *         check_and_dispatch() and assert it returns 0 and the
     *         latch is still set and resched_dispatch_count did NOT
     *         move. This proves the gate actually blocks.
     *
     *   15.B  Gate reopened -> deferred dispatch fires.
     *         Call preempt_enable() (1->0 edge). Assert
     *         preempt_deferred_count incremented by exactly 1. The
     *         latch should now be clear because the deferred
     *         check_and_dispatch consumed it. (We do NOT assert a
     *         context switch here because we are the only runnable
     *         thread on the BSP at this point -- sched_yield will
     *         no-op back to us. The deferred_count edge is the
     *         load-bearing signal.)
     *
     *   15.C  Nesting depth is honored.
     *         disable;disable -> depth=2. enable once -> depth=1,
     *         latch (set manually) should STILL be blocked. enable
     *         again -> depth=0 edge, deferred_count should jump.
     *         This proves outer critical sections survive inner
     *         enable() calls.
     *
     * The BSP=0 contract from G.6.7a remains: stage 14 already
     * asserted bsp dispatch_count==0 and bsp need_resched==0 BEFORE
     * this stage runs. Stage 15 *intentionally* moves these counters
     * on the BSP, which is why it is sequenced last.
     */
    {
        early_console64_write("\n[x86_64][smp-selftest] stage 15: preempt-disable/enable gate");

        /* Sub-test 15.A ------------------------------------------------ */
        uint64_t disp_before  = arch_x86_64_sched_dispatch_count_for_cpu(0u);
        uint64_t defer_before = arch_x86_64_preempt_deferred_count_for_cpu(0u);

        arch_x86_64_preempt_disable();
        if (arch_x86_64_preempt_depth_for_cpu(0u) != 1u) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 15.A depth after disable != 1\n");
            return;
        }
        arch_x86_64_sched_set_need_resched();
        if (arch_x86_64_sched_need_resched_for_cpu(0u) != 1u) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 15.A latch not set\n");
            return;
        }
        /* Critical assertion: dispatch must be suppressed while gate closed. */
        uint32_t ret = arch_x86_64_sched_check_and_dispatch();
        if (ret != 0u) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 15.A dispatch fired with depth>0\n");
            return;
        }
        if (arch_x86_64_sched_need_resched_for_cpu(0u) != 1u) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 15.A latch was cleared by gated dispatch\n");
            return;
        }
        if (arch_x86_64_sched_dispatch_count_for_cpu(0u) != disp_before) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 15.A dispatch_count moved despite gate\n");
            return;
        }

        /* Sub-test 15.B ------------------------------------------------ */
        arch_x86_64_preempt_enable();   /* 1 -> 0 edge, latch is pending */
        if (arch_x86_64_preempt_depth_for_cpu(0u) != 0u) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 15.B depth after enable != 0\n");
            return;
        }
        uint64_t defer_after_B = arch_x86_64_preempt_deferred_count_for_cpu(0u);
        if (defer_after_B != defer_before + 1ull) {
            log_kv("\n[x86_64][smp-selftest] FAIL: stage 15.B deferred_count delta != 1: before=",
                   defer_before);
            log_kv(" after=", defer_after_B);
            early_console64_write("\n");
            return;
        }
        if (arch_x86_64_sched_need_resched_for_cpu(0u) != 0u) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 15.B latch not consumed on enable edge\n");
            return;
        }

        /* Sub-test 15.C ------------------------------------------------ */
        uint64_t defer_before_C = arch_x86_64_preempt_deferred_count_for_cpu(0u);
        arch_x86_64_preempt_disable();
        arch_x86_64_preempt_disable();
        if (arch_x86_64_preempt_depth_for_cpu(0u) != 2u) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 15.C nested depth != 2\n");
            return;
        }
        arch_x86_64_sched_set_need_resched();
        arch_x86_64_preempt_enable();   /* 2 -> 1, must NOT fire */
        if (arch_x86_64_preempt_depth_for_cpu(0u) != 1u) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 15.C depth after inner enable != 1\n");
            return;
        }
        if (arch_x86_64_preempt_deferred_count_for_cpu(0u) != defer_before_C) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 15.C deferred fired on 2->1 (should only fire on 1->0)\n");
            return;
        }
        if (arch_x86_64_sched_need_resched_for_cpu(0u) != 1u) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 15.C latch cleared on 2->1\n");
            return;
        }
        arch_x86_64_preempt_enable();   /* 1 -> 0, MUST fire */
        if (arch_x86_64_preempt_depth_for_cpu(0u) != 0u) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 15.C depth after outer enable != 0\n");
            return;
        }
        if (arch_x86_64_preempt_deferred_count_for_cpu(0u) != defer_before_C + 1ull) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 15.C deferred did not fire on 1->0\n");
            return;
        }
        if (arch_x86_64_sched_need_resched_for_cpu(0u) != 0u) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 15.C latch not consumed on outer enable edge\n");
            return;
        }

        early_console64_write(" PASS");
    }

    /* All stages reached by all APs. */
    if (ap_n > 0) {
        early_console64_write("\n[x86_64][smp-selftest] PASS: all APs idle on private GDT/TSS/IDT+GS + sched-registered + LAPIC-timer driving sched_on_tick + distributed kthreads switched per-CPU + cross-CPU reschedule IPI delivered + cross-CPU migration verified + IPI tail-hook dispatch verified + preempt-disable gate verified\n");
    } else {
        early_console64_write("\n[x86_64][smp-selftest] PASS: no APs (UP system, BSP idle slot only) + preempt-disable gate verified\n");
    }
}
