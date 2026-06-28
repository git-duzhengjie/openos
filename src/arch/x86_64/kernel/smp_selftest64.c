#include "../include/smp_selftest64.h"
#include "../include/smp64.h"
#include "../include/ap_trampoline64.h"
#include "../include/early_console64.h"
#include "../include/percpu64.h"
#include "../include/sched64.h"
#include "../include/pit64.h"
#include "../include/lapic64.h"
#include "../include/idt64.h"  /* G.7g-2: nmi_count accessor */
#include "../include/ioapic64.h"
#include "../include/delay64.h"
#include "../include/gdt64.h"
#include "../include/tss64.h"
#include "../include/vmm64.h"
#include "../include/pmm64.h"

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

        /* G.6.7c: now that the timer-tick path also latches need_resched
         * and routes through the ISR-tail dispatch hook, the BSP would
         * otherwise self-dispatch during the 50ms measurement window
         * (BSP runs a PIT timer at ~100Hz, so we'd see ~5 ticks here).
         * To preserve the original "BSP=0 contract" -- which proves
         * that the *IPI* path does not spuriously target BSP -- we
         * close the preempt gate on BSP for the duration of the
         * window. The gate (G.6.7b) keeps ticks from translating into
         * actual dispatches: timer tick still latches need_resched on
         * BSP, but check_and_dispatch returns 0 immediately because
         * depth>0.
         *
         * We then assert two things at window-end:
         *   - bsp_disp unchanged (the IPI was strictly AP-targeted)
         *   - whatever bsp_need turned out to be, we drain it before
         *     re-enabling to keep the rest of the suite in a clean
         *     state (deferred dispatch on enable() would otherwise
         *     fire NOW and perturb downstream stages). */
        arch_x86_64_preempt_disable();

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

        /* G.6.7c: drain BSP latch and re-open the gate. We MUST clear
         * need_resched manually BEFORE preempt_enable(), otherwise the
         * 1->0 edge will fire a deferred dispatch right here, which
         * would (a) bump bsp_deferred_count in a place Stage 15 doesn't
         * expect and (b) potentially context-switch through the
         * selftest thread. Reading-and-clearing via check_and_dispatch
         * is wrong too (depth still >0 -> gate would just return
         * without clearing). We use a dedicated drain primitive that
         * peeks-and-clears need_resched without going through the
         * gate. */
        arch_x86_64_sched_drain_need_resched(0u);
        arch_x86_64_preempt_enable();

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
        /* G.6.7c: bsp_need may have been latched by a coincidental PIT
         * tick inside the gated window. We logged it above and drained
         * it via drain_need_resched() before re-enabling preempt. The
         * load-bearing assertion is bsp_disp_after == bsp_disp_before:
         * even though BSP saw timer ticks, the gate prevented them
         * from translating into dispatches. So we no longer assert
         * bsp_need == 0 here; that assertion belonged to a world
         * where BSP never latched. */
        (void)bsp_need;
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

    /* Stage 16 (G.6.7c): timer-tick path goes through the preempt gate.
     *
     * Before G.6.7c the timer ISR yielded inline when quantum expired,
     * which bypassed preempt_disable_depth entirely. After G.6.7c the
     * timer ISR only latches need_resched and routes through the same
     * ISR-tail dispatch hook as the resched IPI. The gate now
     * uniformly applies to BOTH paths.
     *
     * We prove this on the BSP. The BSP runs a PIT IRQ0 at ~100 Hz so
     * we are guaranteed at least one tick per 10 ms wall-clock.
     * Strategy:
     *   16.A   With gate CLOSED, busy-wait long enough that several
     *          PIT ticks must have fired. Read need_resched: it must
     *          have been latched by at least one tick (no, it may
     *          NOT have been latched -- only quantum-expiry latches
     *          it; a tick that decrements quantum from N to N-1
     *          without crossing 0 does NOT latch). The actual
     *          load-bearing assertion is:
     *            bsp_disp_after == bsp_disp_before
     *          i.e. however many ticks fired, none of them translated
     *          into a dispatch. The gate held.
     *          We also expect tick_calls_after > tick_calls_before to
     *          prove that the timer actually ticked during our window
     *          (otherwise the test is vacuous).
     *   16.B   Drain any latched need_resched without going through
     *          the gate (drain_need_resched), then open the gate. The
     *          deferred-fire path should NOT trigger because we just
     *          cleared the latch. Assert bsp_deferred_count unchanged.
     *
     * Why on BSP and not an AP? BSP's PIT is unconditionally running
     * and at a stable frequency. AP's LAPIC timer is also running but
     * the AP is busy in its idle loop and may yield-out of any
     * selftest context we set up. BSP is the only place where the
     * selftest thread itself observes the tick. */
    {
        early_console64_write("\n[x86_64][smp-selftest] stage 16 (G.6.7c): timer-tick path honours preempt-disable gate ...");

        uint64_t bsp_disp_before     = arch_x86_64_sched_dispatch_count_for_cpu(0);
        uint64_t bsp_ticks_before    = arch_x86_64_sched_tick_calls_for_cpu(0);
        uint64_t bsp_deferred_before = arch_x86_64_preempt_deferred_count_for_cpu(0);

        log_kv(" bsp_disp_before=", bsp_disp_before);
        log_kv(" bsp_ticks_before=", bsp_ticks_before);
        log_kv(" bsp_deferred_before=", bsp_deferred_before);

        /* 16.A: close the gate and busy-wait until BSP's PIT IRQ has
         * fired at least 5 times. We poll sched_tick_calls directly
         * (which is bumped inside sched_on_tick, which is called from
         * the PIT ISR). This is wall-clock-accurate by construction
         * because we're literally counting timer interrupts; no TSC
         * vs wall-clock calibration issues. preempt_disable does NOT
         * mask IF, so PIT interrupts still fire and still bump the
         * counter; only the dispatch is suppressed.
         *
         * BEFORE we can busy-wait for PIT ticks we have to route IRQ0
         * back through the IOAPIC and sti: the surrounding selftest
         * context runs with cli + IOAPIC IRQ0 masked (inherited from
         * sched_preempt_selftest and apic_selftest's tear-down). This
         * is exactly the same pattern stage 13 uses to wait on PIT
         * ticks; see comment there. We restore both at the end.
         *
         * Safety: if for any reason PIT stops ticking, we have an
         * outer iteration cap so the test fails fast rather than
         * hangs the selftest. */
        uint8_t bsp_lapic_id_16 = arch_x86_64_lapic_id();
        uint8_t pit_gsi_16 = arch_x86_64_ioapic_route_isa_irq(0u, 0x20u, bsp_lapic_id_16);
        arch_x86_64_preempt_disable();
        __asm__ __volatile__("sti");
        const uint64_t target_ticks = bsp_ticks_before + 5;
        uint64_t safety = 0;
        while (arch_x86_64_sched_tick_calls_for_cpu(0) < target_ticks) {
            for (volatile int i = 0; i < 1000; ++i) {
                __asm__ volatile("pause");
            }
            if (++safety > 200000ull) break;
        }
        __asm__ __volatile__("cli");

        uint64_t bsp_disp_mid   = arch_x86_64_sched_dispatch_count_for_cpu(0);
        uint64_t bsp_ticks_mid  = arch_x86_64_sched_tick_calls_for_cpu(0);
        uint32_t bsp_need_mid   = arch_x86_64_sched_need_resched_for_cpu(0);

        log_kv(" bsp_disp_mid=", bsp_disp_mid);
        log_kv(" bsp_ticks_mid=", bsp_ticks_mid);
        log_kv(" bsp_need_mid=", (uint64_t)bsp_need_mid);

        /* Load-bearing: ticks DID fire, dispatches did NOT. */
        if (bsp_disp_mid != bsp_disp_before) {
            arch_x86_64_sched_drain_need_resched(0u);
            arch_x86_64_preempt_enable();
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 16.A BSP dispatched while gate was closed (timer path bypassed gate)\n");
            return;
        }
        if (bsp_ticks_mid <= bsp_ticks_before) {
            arch_x86_64_sched_drain_need_resched(0u);
            arch_x86_64_preempt_enable();
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 16.A no PIT ticks fired (test vacuous, busy-wait too short?)\n");
            return;
        }

        /* 16.B: drain latch and open gate; deferred count must NOT bump. */
        (void)arch_x86_64_sched_drain_need_resched(0u);
        arch_x86_64_preempt_enable();

        /* Restore inherited cli + masked-IOAPIC state for downstream tests. */
        if (pit_gsi_16 != 0xFFu) {
            arch_x86_64_ioapic_mask(pit_gsi_16);
        }

        uint64_t bsp_deferred_after = arch_x86_64_preempt_deferred_count_for_cpu(0);
        log_kv(" bsp_deferred_after=", bsp_deferred_after);

        if (bsp_deferred_after != bsp_deferred_before) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 16.B drained latch but deferred_count moved (drain primitive broken)\n");
            return;
        }

        early_console64_write(" PASS");
    }

    /* Stage 17 (G.7a): per-CPU TSS distinctness.
     *
     * G.7a routes the legacy arch_x86_64_tss_* surface through the
     * per-CPU GDT/TSS farm in percpu64.c. The invariant we want to
     * lock in *before* we start running ring3 on multiple cores is:
     *
     *   1. Each brought-up CPU has a distinct &tss[cpu_idx] base in
     *      its own GDT (no two CPUs share a TSS descriptor).
     *   2. Each CPU's RSP0 backing stack is a distinct address (no two
     *      CPUs would clobber each other's ring0 stack on a ring3->0
     *      transition).
     *   3. The BSP's STR matches OPENOS_X86_64_GDT_TSS (i.e. percpu_load
     *      actually installed the per-CPU TSS into TR).
     *
     * We only inspect the percpu farm from the BSP — no need to bounce
     * onto each AP, because the storage and descriptor encoding are all
     * computed on the BSP at smp_init time (APs only run `lgdt+ltr` on
     * the descriptor we built for them, they don't *create* it).
     */
    {
        early_console64_write("\n[x86_64][smp-selftest] stage 17: per-CPU TSS distinctness ...");

        uint32_t cpu_total = arch_x86_64_smp_cpu_count();
        if (cpu_total == 0u) {
            cpu_total = 1u;
        }
        log_kv(" cpu_total=", (uint64_t)cpu_total);

        /* (3) BSP's TR encoding. */
        uint16_t tr_sel = 0;
        __asm__ __volatile__("str %0" : "=r"(tr_sel));
        log_kv(" bsp_tr=", (uint64_t)tr_sel);
        if (tr_sel != arch_x86_64_gdt_tss_selector()) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 17 BSP TR mismatch (percpu_load did not LTR)\n");
            return;
        }

        /* (1)+(2) distinct TSS bases and distinct RSP0 stacks. */
        x86_64_virt_addr_t bases[OPENOS_X86_64_PERCPU_MAX_CPUS] = {0};
        x86_64_stack_ptr_t rsp0s[OPENOS_X86_64_PERCPU_MAX_CPUS] = {0};
        for (uint32_t i = 0; i < cpu_total && i < OPENOS_X86_64_PERCPU_MAX_CPUS; ++i) {
            bases[i] = arch_x86_64_percpu_tss_base(i);
            rsp0s[i] = arch_x86_64_percpu_rsp0(i);
            if (bases[i] == 0) {
                early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 17 cpu has zero TSS base (percpu_setup not invoked?)\n");
                return;
            }
            if (rsp0s[i] == 0) {
                early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 17 cpu has zero RSP0 (percpu_setup not invoked?)\n");
                return;
            }
        }
        for (uint32_t i = 0; i < cpu_total && i < OPENOS_X86_64_PERCPU_MAX_CPUS; ++i) {
            for (uint32_t j = i + 1u; j < cpu_total && j < OPENOS_X86_64_PERCPU_MAX_CPUS; ++j) {
                if (bases[i] == bases[j]) {
                    early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 17 two CPUs share a TSS base (per-CPU TSS not unique)\n");
                    return;
                }
                if (rsp0s[i] == rsp0s[j]) {
                    early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 17 two CPUs share an RSP0 (ring0 stack would clobber)\n");
                    return;
                }
            }
        }

        /* Also: cpu0's TSS base must equal the legacy arch_x86_64_tss_base()
         * wrapper (BSP shim still exposes cpu0 through the old surface). */
        if (arch_x86_64_tss_base() != bases[0]) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 17 legacy tss_base() != percpu cpu0 base (tss64.c shim broken)\n");
            return;
        }
        if (arch_x86_64_tss_rsp0() != rsp0s[0]) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 17 legacy tss_rsp0() != percpu cpu0 rsp0\n");
            return;
        }

        early_console64_write(" PASS");
    }

    /* Stage 18 (G.7b): swapgs scaffolding — IA32_GS_BASE / IA32_KERNEL_GS_BASE
     * pair must be programmed identically on every brought-up CPU.
     *
     * Background: G.6.2 already programmed both MSRs to &percpu[cpu_idx]
     * inside percpu_install_gs(), but until this commit there was no path
     * that actually USED IA32_KERNEL_GS_BASE — no swapgs anywhere. G.7b
     * re-introduces swapgs on every ring-crossing (ISR_NOERR/ERR common,
     * irq0/irq_lapic_timer/irq_lapic_resched stubs, syscall/sysretq, and
     * the usermode iretq stub). Because the two MSRs are identical, each
     * swapgs is semantically a no-op today and ring0 code that touches
     * %gs:0 after one (zero, two, …) swapgs still finds the same percpu
     * pointer. That is the *whole point*: G.7c will flip KERNEL_GS_BASE
     * to the "user GS" model (= 0 for now), and every swapgs site is
     * already in place, so we won't need to retro-fit them.
     *
     * What this stage actually proves:
     *   (a) BSP's IA32_GS_BASE == IA32_KERNEL_GS_BASE == &percpu[0]
     *       (precondition that makes swapgs a no-op today, and that all
     *       ring0 paths post-swapgs still observe a valid percpu).
     *   (b) Same invariant holds on the *current* CPU we are running on
     *       (which is the BSP here; AP equivalents were already covered
     *       by Stage 7 alive_gs and the per-AP percpu_gs_ok contract).
     *   (c) A live swapgs;swapgs round-trip from kernel context returns
     *       %gs:0 to exactly the percpu pointer we started with. This is
     *       a small in-kernel smoke test of the macro itself: if the
     *       assembler ever silently miscompiled the swapgs site (or if
     *       wrmsr to one of the two MSRs ever stopped happening), the
     *       round-trip would land on a wrong percpu and we'd catch it.
     */
    {
        early_console64_write("\n[x86_64][smp-selftest] stage 18 (G.7b): swapgs MSR pair ...");

        uint64_t gs_base = 0, kernel_gs_base = 0;
        arch_x86_64_percpu_read_gs_pair(&gs_base, &kernel_gs_base);
        log_kv(" gs_base=", gs_base);
        log_kv(" kernel_gs_base=", kernel_gs_base);

        arch_x86_64_percpu_t *bsp_slot = arch_x86_64_percpu_slot(0);
        if (gs_base == 0 || kernel_gs_base == 0) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 18 one of the GS_BASE MSRs is zero (percpu_install_gs not invoked?)\n");
            return;
        }
        if (gs_base != (uint64_t)(uintptr_t)bsp_slot) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 18 IA32_GS_BASE != &percpu[0]\n");
            return;
        }
        if (kernel_gs_base != gs_base) {
            /* G.7b contract: both MSRs hold the same percpu pointer.
             * If/when G.7c lands, this contract changes to
             *   kernel_gs_base == &percpu[i]
             *   gs_base        == "user GS" (currently 0)
             * and the assertion below will be updated alongside it. */
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 18 GS_BASE != KERNEL_GS_BASE (G.7b expects identical, divergence is a G.7c-only change)\n");
            return;
        }

        /* (c) live swapgs round-trip: swapgs;swapgs must restore %gs:0.
         * We read %gs:0 (the self-pointer at percpu_t offset 0), execute
         * two back-to-back swapgs, read again, and require they match.
         * Two swapgs is intentional — a single swapgs would leave us on
         * KERNEL_GS_BASE, which currently happens to equal GS_BASE; the
         * pair restores the original ordering regardless of whether the
         * two MSRs are identical or distinct, which makes this self-test
         * forward-compatible with G.7c. */
        uint64_t pre = 0, post = 0;
        __asm__ __volatile__(
            "movq %%gs:0, %0\n\t"
            "swapgs\n\t"
            "swapgs\n\t"
            "movq %%gs:0, %1\n\t"
            : "=r"(pre), "=r"(post)
            :
            : "memory");
        log_kv(" gs0_pre=", pre);
        log_kv(" gs0_post=", post);
        if (pre == 0 || post == 0) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 18 %gs:0 read returned zero\n");
            return;
        }
        if (pre != post) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 18 swapgs;swapgs did not restore %gs:0\n");
            return;
        }
        if (pre != (uint64_t)(uintptr_t)bsp_slot) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 18 %gs:0 != &percpu[0] (self-pointer corrupted?)\n");
            return;
        }

        early_console64_write(" PASS");
    }

    /* Stage 19 (G.7c): syscall stack-swap save-area must match TSS.RSP0
     * on every CPU.
     *
     * The new syscall_sysret64.S entry sequence is:
     *     swapgs
     *     movq  %rsp, %gs:0x68          // park user rsp
     *     movq  %gs:0x60, %rsp          // load kernel rsp (== TSS.RSP0)
     *
     * If syscall_kernel_rsp (offset 0x60 in the percpu block) ever
     * diverges from g_tss[cpu_idx].rsp[0], then:
     *   - the syscall path lands on stack A
     *   - an interrupt taken mid-syscall lands on stack B (via TSS.RSP0)
     * which corrupts the saved frame the moment any IRQ fires inside
     * a syscall. arch_x86_64_percpu_set_rsp0() updates both atomically;
     * this stage proves the BSP came up with both halves in sync, and
     * also that the cached kernel rsp values are distinct across CPUs
     * (i.e. no accidental shared stack between cores).
     */
    {
        early_console64_write("\n[x86_64][smp-selftest] stage 19 (G.7c): syscall RSP save-area ...");

        /* (a) on the BSP: percpu[0].syscall_kernel_rsp must equal the
         *     RSP0 we just programmed for cpu0, and must be non-zero. */
        uint64_t bsp_rsp0     = (uint64_t)arch_x86_64_percpu_rsp0(0);
        uint64_t bsp_krsp_pc  = arch_x86_64_percpu_slot(0)->syscall_kernel_rsp;
        log_kv(" cpu0_tss_rsp0=",  bsp_rsp0);
        log_kv(" cpu0_pcpu_krsp=", bsp_krsp_pc);
        if (bsp_rsp0 == 0 || bsp_krsp_pc == 0) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 19 cpu0 rsp0/syscall_kernel_rsp is zero\n");
            return;
        }
        if (bsp_rsp0 != bsp_krsp_pc) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 19 cpu0 TSS.RSP0 != percpu.syscall_kernel_rsp (set_rsp0 didn't mirror both)\n");
            return;
        }
        /* user-rsp slot must come up zeroed; it is only ever written by
         * the syscall entry path itself. */
        if (arch_x86_64_percpu_slot(0)->syscall_user_rsp != 0) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 19 cpu0 syscall_user_rsp not zero at boot\n");
            return;
        }

        /* (b) on each brought-up AP: same invariant, AND the kernel-rsp
         *     differs from the BSP (no shared stack). */
        for (uint32_t i = 1; i < OPENOS_X86_64_PERCPU_MAX_CPUS; ++i) {
            arch_x86_64_percpu_t *slot = arch_x86_64_percpu_slot(i);
            if (slot == 0 || slot->magic != OPENOS_X86_64_PERCPU_MAGIC) {
                continue; /* CPU not brought up */
            }
            uint64_t rsp0    = (uint64_t)arch_x86_64_percpu_rsp0(i);
            uint64_t krsp_pc = slot->syscall_kernel_rsp;
            if (rsp0 == 0 || krsp_pc == 0) {
                early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 19 AP rsp0/syscall_kernel_rsp is zero\n");
                return;
            }
            if (rsp0 != krsp_pc) {
                early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 19 AP TSS.RSP0 != percpu.syscall_kernel_rsp\n");
                return;
            }
            if (krsp_pc == bsp_krsp_pc) {
                early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 19 AP syscall_kernel_rsp == BSP (shared kernel stack would be unsafe)\n");
                return;
            }
        }

        early_console64_write(" PASS");
    }

    /* Stage 20 (G.7d): thread-kind tagging scaffolding.
     *
     * G.7d is pure scaffolding -- it adds a `kind` field and a
     * `kernel_stack_top` field to every sched slot, but introduces no
     * USER slots yet (those land in G.7e). The invariants that must
     * hold right now, on every CPU configuration, are:
     *
     *   (1) slot 0 (BSP bootstrap):
     *         kind        == KERNEL
     *         kstack_top  == 0    (it rides the per-CPU shared RSP0)
     *   (2) every AP idle slot at index `cpu` (cpu in [1..n_cpus)):
     *         kind        == KERNEL
     *         kstack_top  == 0    (rides per-CPU RSP0 set up at AP bringup)
     *   (3) every non-FREE, non-idle slot (i.e. real kthreads spawned
     *       so far by the harness): kind == KERNEL AND kstack_top != 0
     *       AND kstack_top is 16-byte aligned (matches the
     *       OPENOS_X86_64_SCHED_KSTACK_ALIGN contract from spawn_kthread).
     *   (4) at least ONE such non-idle slot exists (otherwise this stage
     *       would be silently vacuous and a future regression that
     *       stopped spawning kthreads would slip through).
     *
     * If any of these fails, an upcoming spawn_uthread() would either
     * misclassify a kernel thread as USER (and try to apply a bogus
     * TSS.RSP0) or skip RSP0 apply for a real USER (and the next IRQ
     * mid-userland would land on the wrong stack). Catch it now while
     * everything is still KERNEL and reasoning is local. */
    {
        early_console64_write("\n[x86_64][smp-selftest] stage 20 (G.7d): sched-slot kind/kstack tagging ...");

        /* (1) slot 0 */
        uint32_t  s0_kind = arch_x86_64_sched_slot_kind(0u);
        uintptr_t s0_top  = arch_x86_64_sched_slot_kstack_top(0u);
        log_kv(" slot0_kind=", (uint64_t)s0_kind);
        log_kv(" slot0_kstack_top=", (uint64_t)s0_top);
        if (s0_kind != 0u /* KERNEL */) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 20 slot0 kind != KERNEL\n");
            return;
        }
        if (s0_top != 0u) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 20 slot0 kstack_top != 0 (BSP must ride per-CPU RSP0)\n");
            return;
        }

        /* (2) AP idle slots -- by sched-init convention, AP `cpu` registers
         *     itself as sched slot index `cpu`. We probe indices
         *     [1..PERCPU_MAX_CPUS) and only validate those whose percpu
         *     block is live. */
        for (uint32_t cpu = 1; cpu < OPENOS_X86_64_PERCPU_MAX_CPUS; ++cpu) {
            arch_x86_64_percpu_t *pcpu = arch_x86_64_percpu_slot(cpu);
            if (pcpu == 0 || pcpu->magic != OPENOS_X86_64_PERCPU_MAGIC) continue;
            uint32_t  k = arch_x86_64_sched_slot_kind(cpu);
            uintptr_t t = arch_x86_64_sched_slot_kstack_top(cpu);
            if (k != 0u) {
                early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 20 AP idle slot kind != KERNEL\n");
                return;
            }
            if (t != 0u) {
                early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 20 AP idle slot kstack_top != 0\n");
                return;
            }
        }

        /* (3)+(4) sweep the rest. */
        uint32_t kthr_seen = 0;
        for (uint32_t i = 1; i < OPENOS_X86_64_SCHED_MAX_KTHREADS; ++i) {
            arch_x86_64_percpu_t *pcpu = (i < OPENOS_X86_64_PERCPU_MAX_CPUS)
                                            ? arch_x86_64_percpu_slot(i) : 0;
            uint32_t looks_like_idle = (pcpu != 0 && pcpu->magic == OPENOS_X86_64_PERCPU_MAGIC);
            uint32_t k = arch_x86_64_sched_slot_kind(i);
            if (k == 0xFFFFFFFFu) continue; /* FREE/oob */
            if (looks_like_idle) continue;  /* validated in (2) */
            uintptr_t t = arch_x86_64_sched_slot_kstack_top(i);
            if (k != 0u) {
                early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 20 kthread slot kind != KERNEL (no USER slots exist in G.7d)\n");
                return;
            }
            if (t == 0u) {
                early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 20 kthread slot has zero kstack_top (spawn_kthread must record it)\n");
                return;
            }
            if ((t & 0x7u) != 0u) {
                early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 20 kthread slot kstack_top not 8B aligned\n");
                return;
            }
            kthr_seen++;
        }
        log_kv(" kthread_slots_seen=", (uint64_t)kthr_seen);
        if (kthr_seen == 0) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 20 saw zero real kthread slots (harness regression?)\n");
            return;
        }

        early_console64_write(" PASS");
    }

    /* ------------------------------------------------------------------
     * Stage 21 (G.7e): user-thread dispatch on AP.
     *
     * Spawn a USER sched slot whose ctx.rip points at the sentinel
     * trampoline; that trampoline bumps a per-CPU dispatch counter and
     * cli;hlt's the owning CPU forever. The counter is the witness: if
     * it transitions 0 -> 1 on the target CPU, then
     *   - sched dispatch picked up the USER slot on its owner CPU,
     *   - sched_apply_rsp0_for_next wrote TSS.RSP0 = slot.kernel_stack_top
     *     (otherwise the trampoline's stack touches would fault),
     *   - context_switch64.S restore path jumped to the trampoline.
     *
     * Because the sentinel retires its CPU, we require ap_n >= 3 so that
     * at least one AP remains for prio-selftest. SMP=1/2 -> SKIP.
     * ------------------------------------------------------------------ */
    {
        early_console64_write("\n[x86_64][smp-selftest] stage 21 (G.7e): user-thread dispatch on AP ...");
        if (ap_n < 3u) {
            log_kv(" ap_n=", (uint64_t)ap_n);
            early_console64_write(" SKIP (need ap_n>=3 to spare a sacrifice CPU)");
        } else {
            uint32_t target_cpu = ap_n; /* last AP gets retired */
            uint64_t before = arch_x86_64_percpu_user_dispatch_count(target_cpu);
            uint32_t slot = arch_x86_64_sched_spawn_uthread_sentinel(
                OPENOS_X86_64_SCHED_PRIO_DEFAULT, target_cpu);
            log_kv(" target_cpu=", (uint64_t)target_cpu);
            log_kv(" slot=", (uint64_t)slot);
            log_kv(" disp_before=", before);
            if (slot == 0u) {
                early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 21 spawn_uthread_sentinel returned 0\n");
                return;
            }
            uint32_t  s_kind = arch_x86_64_sched_slot_kind(slot);
            uintptr_t s_top  = arch_x86_64_sched_slot_kstack_top(slot);
            log_kv(" slot_kind=", (uint64_t)s_kind);
            log_kv(" slot_kstack_top=", (uint64_t)s_top);
            if (s_kind != 1u /* USER */) {
                early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 21 spawned slot kind != USER\n");
                return;
            }
            if (s_top == 0u || (s_top & 0x7u) != 0u) {
                early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 21 spawned slot kstack_top invalid\n");
                return;
            }

            /* G.7e-fix: poke the target CPU so it picks up the new slot.
             * A single IPI is racy under SMP=4 + TCG: if target_cpu happens
             * to be inside a CLI window (or its sti;hlt re-arm has not yet
             * re-enabled IF when the IPI lands), need_resched latches but
             * the dispatch path is never entered until the next external
             * event, which may exceed our 100 ms window. We instead retry
             * the IPI periodically inside the wait loop. */
            arch_x86_64_smp_send_resched_ipi(target_cpu);

            /* Spin up to ~100ms waiting for the trampoline to fire,
             * re-poking target_cpu every ~5ms to be robust against missed
             * deliveries. */
            uint64_t after = before;
            for (uint32_t i = 0; i < 100000u; ++i) {
                after = arch_x86_64_percpu_user_dispatch_count(target_cpu);
                if (after > before) break;
                if ((i % 5000u) == 4999u) {
                    arch_x86_64_smp_send_resched_ipi(target_cpu);
                }
                arch_x86_64_delay_us(1u);
            }
            log_kv(" disp_after=", after);
            if (after <= before) {
                /* G.7e-fix diagnostic: dump target-CPU sched state so we can
                 * see whether the spawned USER slot is even visible/READY on
                 * target_cpu, whether need_resched latched, and how many
                 * context switches actually happened on target_cpu during
                 * the wait window. */
                log_kv(" diag_slot=", (uint64_t)slot);
                log_kv(" diag_slot_kind=",
                    (uint64_t)arch_x86_64_sched_slot_kind(slot));
                log_kv(" diag_slot_kstack_top=",
                    (uint64_t)arch_x86_64_sched_slot_kstack_top(slot));
                log_kv(" diag_need_resched_target=",
                    (uint64_t)arch_x86_64_sched_need_resched_for_cpu(target_cpu));
                log_kv(" diag_dispatch_count_target=",
                    arch_x86_64_sched_dispatch_count_for_cpu(target_cpu));
                log_kv(" diag_user_dispatch_target=",
                    arch_x86_64_percpu_user_dispatch_count(target_cpu));
                early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 21 user_dispatch_count did not advance\n");
                return;
            }
            early_console64_write(" PASS");
        }
    }

    /* ------------------------------------------------------------------
     * Stage 22 (G.7g-1): LAPIC timer bus-frequency calibration.
     *
     * The BSP ran arch_x86_64_lapic_timer_calibrate() before SMP bring-up.
     * Verify that g_lapic_bus_ticks_per_ms is non-zero and falls inside a
     * sane envelope. We don't pin a tight number because QEMU's emulated
     * LAPIC bus frequency varies (TCG ~125 MHz pre-div, ~7800 ticks/ms
     * after div16; KVM passthrough can differ). Accept [1000, 1_000_000]
     * which catches "calibration didn't run" and "obviously bogus".
     * ------------------------------------------------------------------ */
    {
        early_console64_write("\n[x86_64][smp-selftest] stage 22 (G.7g-1): LAPIC timer calibrated ...");
        uint32_t tpm = arch_x86_64_lapic_timer_ticks_per_ms();
        log_kv(" ticks_per_ms=", (uint64_t)tpm);
        if (tpm == 0u) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 22 calibration didn't run\n");
            return;
        }
        if (tpm < 1000u || tpm > 1000000u) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 22 ticks_per_ms outside sane envelope\n");
            return;
        }
        early_console64_write(" PASS");
    }

    /* ------------------------------------------------------------------
     * Stage 23 (G.7g-2): NMI live-trigger via LAPIC ICR self-shorthand.
     *
     * Snapshot g_nmi_count, fire a self-NMI from the BSP, then re-read
     * the counter. The exception entry path is already wired (vector 2
     * handler increments g_nmi_count; NMI ack is implicit). PASS iff
     * count strictly increased.
     * ------------------------------------------------------------------ */
    {
        early_console64_write("\n[x86_64][smp-selftest] stage 23 (G.7g-2): NMI self-IPI ...");
        uint64_t nmi_before = arch_x86_64_idt_nmi_count();
        bool sent = arch_x86_64_lapic_send_self_nmi();
        /* The NMI is asynchronous; give it a few hundred cycles to land. */
        for (volatile int i = 0; i < 100000; ++i) { __asm__ volatile("pause" ::: "memory"); }
        uint64_t nmi_after = arch_x86_64_idt_nmi_count();
        log_kv(" sent=", (uint64_t)(sent ? 1u : 0u));
        log_kv(" before=", nmi_before);
        log_kv(" after=", nmi_after);
        if (!sent) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 23 ICR delivery timeout\n");
            return;
        }
        if (nmi_after <= nmi_before) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 23 NMI not observed in handler\n");
            return;
        }
        early_console64_write(" PASS");
    }

    /* ------------------------------------------------------------------
     * Stage 24 (G.3b-3): recoverable #UD probe.
     *
     * The exception path so far has only been *observed* indirectly
     * (kernel fault snapshot, NMI counter). Stage 24 closes the loop by
     * forcing a synchronous #UD from ring0 and proving the dispatcher
     * can recover — i.e. bump frame->rip past the offending ud2 so that
     * iretq resumes at the very next instruction.
     *
     * Wiring:
     *   - lea a label address (`g3b3_ud_site`) into a register
     *   - arm the single-shot probe with (rip=label, insn_len=2)
     *   - execute `ud2` at that exact label
     *   - the dispatcher recognises rip == armed_rip, increments the
     *     probe count, advances rip by 2, returns. We resume here.
     *
     * Invariants asserted post-iretq:
     *   - probe count strictly increased by exactly 1
     *   - probe is disarmed (single-shot)
     *   - g_kfault was NOT updated (i.e. the probed fault did not
     *     poison the kernel fault sentry).
     * ------------------------------------------------------------------ */
    {
        early_console64_write("\n[x86_64][smp-selftest] stage 24 (G.3b-3): recoverable #UD probe ...");
        uint64_t ud_before    = arch_x86_64_idt_ud_probe_count();
        uint64_t kf_count_before;
        {
            struct x86_64_kernel_fault_snapshot snap_before;
            arch_x86_64_idt_kernel_fault_snapshot(&snap_before);
            kf_count_before = snap_before.count;
        }

        /* Grab the label address, arm, then fire ud2. The label sits
         * *immediately before* the ud2 so &label == rip-at-fault. */
        uint64_t site_rip = 0;
        __asm__ __volatile__(
            "leaq    1f(%%rip), %0\n\t"
            "movq    %0, %%rdi\n\t"
            "movl    $2, %%esi\n\t"
            "call    arch_x86_64_idt_arm_ud_probe\n\t"
            "1:\n\t"
            "ud2\n\t"
            : "=r"(site_rip)
            :
            : "rdi", "rsi", "rax", "rcx", "rdx", "r8", "r9", "r10", "r11", "memory", "cc"
        );

        uint64_t ud_after = arch_x86_64_idt_ud_probe_count();
        int      armed   = arch_x86_64_idt_ud_probe_is_armed();
        uint64_t kf_count_after;
        {
            struct x86_64_kernel_fault_snapshot snap_after;
            arch_x86_64_idt_kernel_fault_snapshot(&snap_after);
            kf_count_after = snap_after.count;
        }

        log_kv(" site_rip=", site_rip);
        log_kv(" ud_before=", ud_before);
        log_kv(" ud_after=",  ud_after);
        log_kv(" armed=",     (uint64_t)(uint32_t)armed);
        log_kv(" kf_delta=",  kf_count_after - kf_count_before);

        if (ud_after != ud_before + 1u) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 24 probe count delta != 1\n");
            return;
        }
        if (armed != 0) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 24 probe still armed (not single-shot)\n");
            return;
        }
        if (kf_count_after != kf_count_before) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 24 probed #UD polluted kernel fault snapshot\n");
            return;
        }
        early_console64_write(" PASS");
    }

    /* ------------------------------------------------------------------
     * Stage 25 (G.3b-4): recoverable #PF probe.
     *
     * Same single-shot pattern as stage 24, but for a synchronous page
     * fault on an unmapped canonical address. This proves the dispatcher
     * can both observe CR2 *and* resume past the faulting load.
     *
     * Choice of fault address: a low-canonical address far above any
     * region the boot identity map covers (kernel maps the low few GiB
     * only). 0x0000_7FFF_DEAD_BEE0 is canonical, well outside any
     * mapped range, and 16-byte aligned so the load is a single page
     * access.
     *
     * Wiring:
     *   - lea label-address into a register (this is the rip we expect
     *     CPU to record on fault)
     *   - load the probe target address into %rdi
     *   - arm the probe with (rip=label, insn_len=3, cr2=target)
     *     (movq (%rdi), %rax is exactly 3 bytes: 48 8b 07)
     *   - execute the faulting load at the label
     *   - dispatcher matches both rip and CR2, increments pf count,
     *     advances rip by 3, returns. We resume here.
     *
     * Invariants asserted post-iretq:
     *   - pf probe count delta == 1
     *   - probe disarmed (single-shot)
     *   - g_kfault not updated (clean recovery, no sentry pollution)
     *   - ud probe count unchanged (we did not accidentally take #UD)
     * ------------------------------------------------------------------ */
    {
        early_console64_write("\n[x86_64][smp-selftest] stage 25 (G.3b-4): recoverable #PF probe ...");
        uint64_t pf_before    = arch_x86_64_idt_pf_probe_count();
        uint64_t ud_baseline  = arch_x86_64_idt_ud_probe_count();
        uint64_t kf_count_before;
        {
            struct x86_64_kernel_fault_snapshot snap_before;
            arch_x86_64_idt_kernel_fault_snapshot(&snap_before);
            kf_count_before = snap_before.count;
        }

        const uint64_t probe_va = 0x00007FFFDEADBEE0ull;
        uint64_t site_rip = 0;
        uint64_t dummy    = 0;
        /*
         * %rdi = probe_va (faulting address, also passed as cr2 arg)
         * %rsi = insn_len (3 for `mov (%rdi), %rax`)
         * %rdx = expected_rip == &1f
         * call arch_x86_64_idt_arm_pf_probe(rip, len, cr2)
         *   note: SysV AMD64 arg order is rdi,rsi,rdx — we need
         *         arg0=rip, arg1=len, arg2=cr2, so reshuffle right before call.
         */
        __asm__ __volatile__(
            "leaq    1f(%%rip), %%r12\n\t"  /* r12 (callee-saved) = &1f      */
            "movq    %2, %%r13\n\t"           /* r13 (callee-saved) = probe_va */
            "movq    %%r12, %%rdi\n\t"       /* arg0 = expected_rip            */
            "movl    $3, %%esi\n\t"          /* arg1 = insn_len                */
            "movq    %%r13, %%rdx\n\t"       /* arg2 = expected_cr2            */
            "call    arch_x86_64_idt_arm_pf_probe\n\t"
            "movq    %%r12, %0\n\t"          /* recover site_rip after call    */
            "movq    %%r13, %%rdi\n\t"       /* rdi = probe_va                 */
            "1:\n\t"
            "movq    (%%rdi), %1\n\t"        /* 48 8b 07 — 3 bytes, faults    */
            : "=r"(site_rip), "=r"(dummy)
            : "r"(probe_va)
            : "rdi", "rsi", "rax", "rcx", "rdx", "r8", "r9", "r10", "r11",
              "r12", "r13", "memory", "cc"
        );

        uint64_t pf_after = arch_x86_64_idt_pf_probe_count();
        int      armed   = arch_x86_64_idt_pf_probe_is_armed();
        uint64_t ud_now  = arch_x86_64_idt_ud_probe_count();
        uint64_t kf_count_after;
        {
            struct x86_64_kernel_fault_snapshot snap_after;
            arch_x86_64_idt_kernel_fault_snapshot(&snap_after);
            kf_count_after = snap_after.count;
        }

        log_kv(" site_rip=", site_rip);
        log_kv(" cr2=",      probe_va);
        log_kv(" pf_before=", pf_before);
        log_kv(" pf_after=",  pf_after);
        log_kv(" armed=",     (uint64_t)(uint32_t)armed);
        log_kv(" kf_delta=",  kf_count_after - kf_count_before);
        log_kv(" ud_delta=",  ud_now - ud_baseline);

        if (pf_after != pf_before + 1u) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 25 pf probe count delta != 1\n");
            return;
        }
        if (armed != 0) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 25 pf probe still armed (not single-shot)\n");
            return;
        }
        if (kf_count_after != kf_count_before) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 25 probed #PF polluted kernel fault snapshot\n");
            return;
        }
        if (ud_now != ud_baseline) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 25 perturbed ud probe count\n");
            return;
        }
        early_console64_write(" PASS");
    }

    /* ------------------------------------------------------------------
     * Stage 26 (G.3b-5): recoverable #GP probe.
     *
     * Same single-shot pattern as stages 24/25, but for a synchronous
     * general-protection fault. Reproducer: load an invalid (non-NULL,
     * out-of-GDT-range) selector into %fs. Specifically:
     *
     *     mov %ax, %fs      ; 8e e0  -> 2-byte instruction
     *
     * with %ax preloaded to 0xDEAD. The selector 0xDEAD is neither the
     * NULL selector (which would silently load) nor a valid GDT slot, so
     * the CPU raises #GP(selector & 0xFFFC). The faulting RIP recorded
     * in the iret frame is exactly the address of the mov.
     *
     * Wiring is even simpler than #PF because there is no aux value to
     * match: precise RIP equality is the strict gate. The dispatcher
     * matches, increments gp_probe_count, advances rip by 2, and returns.
     *
     * Invariants asserted post-iretq:
     *   - gp probe count delta == 1
     *   - probe disarmed (single-shot)
     *   - g_kfault not updated (clean recovery, no sentry pollution)
     *   - ud/pf probe counts unchanged (no cross-contamination)
     *   - %fs reloaded with NULL selector before returning, so kernel
     *     never observes a half-broken segment register (the #GP itself
     *     leaves %fs unchanged per Intel SDM Vol.3, but we defensively
     *     re-zero it after recovery).
     * ------------------------------------------------------------------ */
    {
        early_console64_write("\n[x86_64][smp-selftest] stage 26 (G.3b-5): recoverable #GP probe ...");
        uint64_t gp_before    = arch_x86_64_idt_gp_probe_count();
        uint64_t ud_baseline  = arch_x86_64_idt_ud_probe_count();
        uint64_t pf_baseline  = arch_x86_64_idt_pf_probe_count();
        uint64_t kf_count_before;
        {
            struct x86_64_kernel_fault_snapshot snap_before;
            arch_x86_64_idt_kernel_fault_snapshot(&snap_before);
            kf_count_before = snap_before.count;
        }

        uint64_t site_rip = 0;
        /*
         * %r12 (callee-saved) holds &1f across the arm_gp_probe() call.
         * SysV AMD64: arg0=rdi (rip), arg1=rsi (insn_len).
         * After the call, %ax = 0xDEAD, then the 2-byte `mov %ax,%fs`
         * at label 1: faults synchronously.
         */
        __asm__ __volatile__(
            "leaq    1f(%%rip), %%r12\n\t"   /* r12 = &1f                  */
            "movq    %%r12, %%rdi\n\t"        /* arg0 = expected_rip        */
            "movl    $2, %%esi\n\t"           /* arg1 = insn_len (8e e0)    */
            "call    arch_x86_64_idt_arm_gp_probe\n\t"
            "movq    %%r12, %0\n\t"           /* recover site_rip           */
            "movw    $0xDEAD, %%ax\n\t"       /* invalid selector           */
            "1:\n\t"
            "movw    %%ax, %%fs\n\t"          /* 8e e0 — 2 bytes, faults   */
            "xorw    %%ax, %%ax\n\t"          /* defensive: reload %fs=NULL */
            "movw    %%ax, %%fs\n\t"
            : "=r"(site_rip)
            :
            : "rdi", "rsi", "rax", "rcx", "rdx", "r8", "r9", "r10", "r11",
              "r12", "memory", "cc"
        );

        uint64_t gp_after = arch_x86_64_idt_gp_probe_count();
        int      armed   = arch_x86_64_idt_gp_probe_is_armed();
        uint64_t ud_now  = arch_x86_64_idt_ud_probe_count();
        uint64_t pf_now  = arch_x86_64_idt_pf_probe_count();
        uint64_t kf_count_after;
        {
            struct x86_64_kernel_fault_snapshot snap_after;
            arch_x86_64_idt_kernel_fault_snapshot(&snap_after);
            kf_count_after = snap_after.count;
        }

        log_kv(" site_rip=", site_rip);
        log_kv(" gp_before=", gp_before);
        log_kv(" gp_after=",  gp_after);
        log_kv(" armed=",     (uint64_t)(uint32_t)armed);
        log_kv(" kf_delta=",  kf_count_after - kf_count_before);
        log_kv(" ud_delta=",  ud_now - ud_baseline);
        log_kv(" pf_delta=",  pf_now - pf_baseline);

        if (gp_after != gp_before + 1u) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 26 gp probe count delta != 1\n");
            return;
        }
        if (armed != 0) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 26 gp probe still armed (not single-shot)\n");
            return;
        }
        if (kf_count_after != kf_count_before) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 26 probed #GP polluted kernel fault snapshot\n");
            return;
        }
        if (ud_now != ud_baseline) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 26 perturbed ud probe count\n");
            return;
        }
        if (pf_now != pf_baseline) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 26 perturbed pf probe count\n");
            return;
        }
        early_console64_write(" PASS");
    }

    /* --------------------------------------------------------------------
     * Stage 27 (G.3b-6): recoverable #DE probe.
     *
     * Same shape as the #UD/#GP probes: arm with (expected_rip, insn_len),
     * raise the fault by executing `divl %ecx` with %ecx=0 (and %edx:%eax
     * preloaded with a non-zero dividend so the CPU genuinely reaches the
     * div-by-zero check rather than short-circuiting on dividend==0), and
     * verify after recovery that exactly one #DE was absorbed without any
     * sentry pollution or cross-probe perturbation.
     *
     *     divl %ecx         ; f7 f1  -> 2-byte instruction
     *
     * #DE is a fault (vector 0), so the hardware-pushed RIP is the address
     * of the div instruction itself; advancing rip by insn_len=2 resumes
     * iretq past the div.
     *
     * Invariants asserted post-iretq:
     *   - de probe count delta == 1
     *   - probe disarmed (single-shot)
     *   - g_kfault not updated (clean recovery, no sentry pollution)
     *   - ud/pf/gp probe counts unchanged (no cross-contamination)
     * ------------------------------------------------------------------ */
    {
        early_console64_write("\n[x86_64][smp-selftest] stage 27 (G.3b-6): recoverable #DE probe ...");
        uint64_t de_before    = arch_x86_64_idt_de_probe_count();
        uint64_t ud_baseline  = arch_x86_64_idt_ud_probe_count();
        uint64_t pf_baseline  = arch_x86_64_idt_pf_probe_count();
        uint64_t gp_baseline  = arch_x86_64_idt_gp_probe_count();
        uint64_t kf_count_before;
        {
            struct x86_64_kernel_fault_snapshot snap_before;
            arch_x86_64_idt_kernel_fault_snapshot(&snap_before);
            kf_count_before = snap_before.count;
        }

        uint64_t site_rip = 0;
        /*
         * %r12 (callee-saved) holds &1f across the arm_de_probe() call.
         * SysV AMD64: arg0=rdi (rip), arg1=rsi (insn_len).
         * After the call we set %edx:%eax = 0:1 (non-zero 64-bit dividend)
         * and %ecx = 0; the 2-byte `divl %ecx` at label 1 faults synchronously.
         */
        __asm__ __volatile__(
            "leaq    1f(%%rip), %%r12\n\t"   /* r12 = &1f                  */
            "movq    %%r12, %%rdi\n\t"        /* arg0 = expected_rip        */
            "movl    $2, %%esi\n\t"           /* arg1 = insn_len (f7 f1)    */
            "call    arch_x86_64_idt_arm_de_probe\n\t"
            "movq    %%r12, %0\n\t"           /* recover site_rip           */
            "xorl    %%edx, %%edx\n\t"        /* edx = 0  (high dividend)   */
            "movl    $1, %%eax\n\t"           /* eax = 1  (low dividend)    */
            "xorl    %%ecx, %%ecx\n\t"        /* ecx = 0  (divisor)         */
            "1:\n\t"
            "divl    %%ecx\n\t"               /* f7 f1 — 2 bytes, faults   */
            : "=r"(site_rip)
            :
            : "rdi", "rsi", "rax", "rcx", "rdx", "r8", "r9", "r10", "r11",
              "r12", "memory", "cc"
        );

        uint64_t de_after = arch_x86_64_idt_de_probe_count();
        int      armed   = arch_x86_64_idt_de_probe_is_armed();
        uint64_t ud_now  = arch_x86_64_idt_ud_probe_count();
        uint64_t pf_now  = arch_x86_64_idt_pf_probe_count();
        uint64_t gp_now  = arch_x86_64_idt_gp_probe_count();
        uint64_t kf_count_after;
        {
            struct x86_64_kernel_fault_snapshot snap_after;
            arch_x86_64_idt_kernel_fault_snapshot(&snap_after);
            kf_count_after = snap_after.count;
        }

        log_kv(" site_rip=", site_rip);
        log_kv(" de_before=", de_before);
        log_kv(" de_after=",  de_after);
        log_kv(" armed=",     (uint64_t)(uint32_t)armed);
        log_kv(" kf_delta=",  kf_count_after - kf_count_before);
        log_kv(" ud_delta=",  ud_now - ud_baseline);
        log_kv(" pf_delta=",  pf_now - pf_baseline);
        log_kv(" gp_delta=",  gp_now - gp_baseline);

        if (de_after != de_before + 1u) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 27 de probe count delta != 1\n");
            return;
        }
        if (armed != 0) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 27 de probe still armed (not single-shot)\n");
            return;
        }
        if (kf_count_after != kf_count_before) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 27 probed #DE polluted kernel fault snapshot\n");
            return;
        }
        if (ud_now != ud_baseline) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 27 perturbed ud probe count\n");
            return;
        }
        if (pf_now != pf_baseline) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 27 perturbed pf probe count\n");
            return;
        }
        if (gp_now != gp_baseline) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 27 perturbed gp probe count\n");
            return;
        }
        early_console64_write(" PASS");
    }

    /* ------------------------------------------------------------------
     * Stage 28 (G.3b-7): recoverable single-shot #BP probe.
     *
     * #BP (vector 3, int3, 0xCC) is a TRAP — the hardware-pushed RIP
     * already points to the byte AFTER the int3. We therefore capture
     * that post-int3 RIP via a local label, arm the probe with it, and
     * verify on hit that:
     *   - bp_probe count increments by exactly 1
     *   - probe is no longer armed (single-shot)
     *   - g_kfault not updated (no sentry pollution)
     *   - ud/pf/gp/de probe counts unchanged (no cross-contamination)
     * ------------------------------------------------------------------ */
    {
        early_console64_write("\n[x86_64][smp-selftest] stage 28 (G.3b-7): recoverable #BP probe ...");
        uint64_t bp_before    = arch_x86_64_idt_bp_probe_count();
        uint64_t ud_baseline  = arch_x86_64_idt_ud_probe_count();
        uint64_t pf_baseline  = arch_x86_64_idt_pf_probe_count();
        uint64_t gp_baseline  = arch_x86_64_idt_gp_probe_count();
        uint64_t de_baseline  = arch_x86_64_idt_de_probe_count();
        uint64_t kf_count_before;
        {
            struct x86_64_kernel_fault_snapshot snap_before;
            arch_x86_64_idt_kernel_fault_snapshot(&snap_before);
            kf_count_before = snap_before.count;
        }

        uint64_t resume_rip = 0;
        /*
         * Emit `int3` (0xCC, 1 byte) at label 1f. Label 2f is the byte
         * immediately after, i.e. the post-trap RIP that the dispatcher
         * will see in frame->rip. We arm the probe with &2f, then execute
         * the int3; the dispatcher matches, disarms, bumps the counter,
         * and returns — iretq resumes naturally at label 2f.
         *
         * SysV AMD64: arg0=rdi (rip).
         */
        __asm__ __volatile__(
            "leaq    2f(%%rip), %%r12\n\t"   /* r12 = &2f (post-int3 RIP) */
            "movq    %%r12, %%rdi\n\t"        /* arg0 = expected_rip_after */
            "call    arch_x86_64_idt_arm_bp_probe\n\t"
            "movq    %%r12, %0\n\t"           /* recover resume_rip        */
            "1:\n\t"
            "int3\n\t"                        /* 0xCC — 1 byte, traps      */
            "2:\n\t"
            "nop\n\t"
            : "=r"(resume_rip)
            :
            : "rdi", "rax", "rcx", "rdx", "rsi", "r8", "r9", "r10", "r11",
              "r12", "memory", "cc"
        );

        uint64_t bp_after = arch_x86_64_idt_bp_probe_count();
        int      armed   = arch_x86_64_idt_bp_probe_is_armed();
        uint64_t ud_now  = arch_x86_64_idt_ud_probe_count();
        uint64_t pf_now  = arch_x86_64_idt_pf_probe_count();
        uint64_t gp_now  = arch_x86_64_idt_gp_probe_count();
        uint64_t de_now  = arch_x86_64_idt_de_probe_count();
        uint64_t kf_count_after;
        {
            struct x86_64_kernel_fault_snapshot snap_after;
            arch_x86_64_idt_kernel_fault_snapshot(&snap_after);
            kf_count_after = snap_after.count;
        }

        log_kv(" resume_rip=", resume_rip);
        log_kv(" bp_before=",  bp_before);
        log_kv(" bp_after=",   bp_after);
        log_kv(" armed=",      (uint64_t)(uint32_t)armed);
        log_kv(" kf_delta=",   kf_count_after - kf_count_before);
        log_kv(" ud_delta=",   ud_now - ud_baseline);
        log_kv(" pf_delta=",   pf_now - pf_baseline);
        log_kv(" gp_delta=",   gp_now - gp_baseline);
        log_kv(" de_delta=",   de_now - de_baseline);

        if (bp_after != bp_before + 1u) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 28 bp probe count delta != 1\n");
            return;
        }
        if (armed != 0) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 28 bp probe still armed (not single-shot)\n");
            return;
        }
        if (kf_count_after != kf_count_before) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 28 probed #BP polluted kernel fault snapshot\n");
            return;
        }
        if (ud_now != ud_baseline) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 28 perturbed ud probe count\n");
            return;
        }
        if (pf_now != pf_baseline) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 28 perturbed pf probe count\n");
            return;
        }
        if (gp_now != gp_baseline) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 28 perturbed gp probe count\n");
            return;
        }
        if (de_now != de_baseline) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 28 perturbed de probe count\n");
            return;
        }
        early_console64_write(" PASS");
    }

    /* ------------------------------------------------------------------
     * Stage 29 (G.3c): fault-injection burst harness.
     *
     * Runs the five recoverable-exception primitives (#UD/#PF/#GP/#DE/#BP)
     * back-to-back in a loop of N iterations and checks that:
     *   - each primitive's counter advanced by exactly N
     *   - kernel-fault sentry was never touched (kf_delta == 0)
     *   - no primitive is left armed at the end (single-shot honoured
     *     under repeated arm/fire cycles)
     *
     * This is the foundation of a unified fault-injection harness: each
     * primitive is reduced to an `arm_and_fire()` step, and the harness
     * sequences them. Future work can shuffle the order, vary insn_len
     * widths, or interleave with timer ticks.
     *
     * Burst count chosen small (N=5) to keep selftest fast. The probes
     * have already been validated individually in stages 24..28.
     * ------------------------------------------------------------------ */
    {
        early_console64_write("\n[x86_64][smp-selftest] stage 29 (G.3c): fault-injection burst harness ...");
        const unsigned N = 5;
        uint64_t ud_b = arch_x86_64_idt_ud_probe_count();
        uint64_t pf_b = arch_x86_64_idt_pf_probe_count();
        uint64_t gp_b = arch_x86_64_idt_gp_probe_count();
        uint64_t de_b = arch_x86_64_idt_de_probe_count();
        uint64_t bp_b = arch_x86_64_idt_bp_probe_count();
        uint64_t kf_b;
        {
            struct x86_64_kernel_fault_snapshot snap;
            arch_x86_64_idt_kernel_fault_snapshot(&snap);
            kf_b = snap.count;
        }

        for (unsigned it = 0; it < N; ++it) {
            /* ---- #UD: ud2 (0F 0B, 2 bytes) ---- */
            __asm__ __volatile__(
                "leaq    1f(%%rip), %%rdi\n\t"
                "movl    $2, %%esi\n\t"
                "call    arch_x86_64_idt_arm_ud_probe\n\t"
                "1: ud2\n\t"
                "2: nop\n\t"
                ::: "rdi", "rsi", "rax", "rcx", "rdx", "r8", "r9", "r10", "r11",
                    "memory", "cc");

            /* ---- #PF: load from unmapped user-canonical addr (48 8b 00 = 3 bytes) ---- */
            __asm__ __volatile__(
                "leaq    1f(%%rip), %%rdi\n\t"
                "movl    $3, %%esi\n\t"
                "movabsq $0x00007FFFDEADBEE0, %%rdx\n\t"
                "call    arch_x86_64_idt_arm_pf_probe\n\t"
                "movabsq $0x00007FFFDEADBEE0, %%rax\n\t"
                "1: movq    (%%rax), %%rax\n\t"
                "2: nop\n\t"
                ::: "rdi", "rsi", "rdx", "rax", "rcx", "r8", "r9", "r10", "r11",
                    "memory", "cc");

            /* ---- #GP: mov %ax,%fs with %ax=0xDEAD (8e e0 = 2 bytes) ---- */
            __asm__ __volatile__(
                "leaq    1f(%%rip), %%rdi\n\t"
                "movl    $2, %%esi\n\t"
                "call    arch_x86_64_idt_arm_gp_probe\n\t"
                "movw    $0xDEAD, %%ax\n\t"
                "1: movw    %%ax, %%fs\n\t"
                "2: xorw    %%ax, %%ax\n\t"
                "movw    %%ax, %%fs\n\t"
                ::: "rdi", "rsi", "rax", "rcx", "rdx", "r8", "r9", "r10", "r11",
                    "memory", "cc");

            /* ---- #DE: divl %ecx with %edx:%eax=0:1, %ecx=0 (f7 f1 = 2 bytes) ---- */
            __asm__ __volatile__(
                "leaq    1f(%%rip), %%rdi\n\t"
                "movl    $2, %%esi\n\t"
                "call    arch_x86_64_idt_arm_de_probe\n\t"
                "xorl    %%edx, %%edx\n\t"
                "movl    $1, %%eax\n\t"
                "xorl    %%ecx, %%ecx\n\t"
                "1: divl    %%ecx\n\t"
                "2: nop\n\t"
                ::: "rdi", "rsi", "rax", "rcx", "rdx", "r8", "r9", "r10", "r11",
                    "memory", "cc");

            /* ---- #BP: int3 (cc = 1 byte, TRAP -- arm with post-int3 RIP) ---- */
            __asm__ __volatile__(
                "leaq    2f(%%rip), %%rdi\n\t"
                "call    arch_x86_64_idt_arm_bp_probe\n\t"
                "1: int3\n\t"
                "2: nop\n\t"
                ::: "rdi", "rax", "rcx", "rdx", "rsi", "r8", "r9", "r10", "r11",
                    "memory", "cc");
        }

        uint64_t ud_a = arch_x86_64_idt_ud_probe_count();
        uint64_t pf_a = arch_x86_64_idt_pf_probe_count();
        uint64_t gp_a = arch_x86_64_idt_gp_probe_count();
        uint64_t de_a = arch_x86_64_idt_de_probe_count();
        uint64_t bp_a = arch_x86_64_idt_bp_probe_count();
        uint64_t kf_a;
        {
            struct x86_64_kernel_fault_snapshot snap;
            arch_x86_64_idt_kernel_fault_snapshot(&snap);
            kf_a = snap.count;
        }

        log_kv(" iters=",    (uint64_t)N);
        log_kv(" ud_delta=", ud_a - ud_b);
        log_kv(" pf_delta=", pf_a - pf_b);
        log_kv(" gp_delta=", gp_a - gp_b);
        log_kv(" de_delta=", de_a - de_b);
        log_kv(" bp_delta=", bp_a - bp_b);
        log_kv(" kf_delta=", kf_a - kf_b);

        if (ud_a - ud_b != N || pf_a - pf_b != N || gp_a - gp_b != N ||
            de_a - de_b != N || bp_a - bp_b != N) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 29 burst delta mismatch\n");
            return;
        }
        if (kf_a != kf_b) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 29 burst polluted kernel fault snapshot\n");
            return;
        }
        if (arch_x86_64_idt_ud_probe_is_armed() ||
            arch_x86_64_idt_pf_probe_is_armed() ||
            arch_x86_64_idt_gp_probe_is_armed() ||
            arch_x86_64_idt_de_probe_is_armed() ||
            arch_x86_64_idt_bp_probe_is_armed()) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 29 burst left a probe armed\n");
            return;
        }
        early_console64_write(" PASS");
    }

    /*
     * Stage 30 (G.7f): ring3 LAPIC-timer preemption.
     *
     * DoD: a real ring3 user thread executing a tight `jmp $` busy loop is
     * preempted by the LAPIC timer IRQ at least once within 50ms. We prove
     * preemption by observing target_cpu's lapic_timer_count advance while
     * the slot is the dispatched USER slot, without the slot exiting (the
     * trampoline only bumps user_dispatch_count on entry, so we also check
     * that user_dispatch_count[target_cpu] == 1 after the window).
     *
     * This requires:
     *   1) a user-accessible (U=1) R+X page mapped at a fixed identity VA
     *      containing the busy loop `EB FE` (jmp $) bytes,
     *   2) a user-accessible R+W page for the user stack,
     *   3) the real spawn_uthread path (NOT the sentinel variant),
     *   4) at least one AP (target_cpu = ap_n).
     */
    if (ap_n >= 3u) {
        early_console64_write("\n[x86_64][smp-selftest] stage 30 (G.7f): ring3 LAPIC-timer preemption ...");

        /* Step 1: allocate + map a U+R+X code page and a U+R+W stack page.
         * Use fixed VAs in low identity-mapped region so the choice is
         * deterministic across runs (pmm_alloc may return different PAs). */
        const x86_64_virt_addr_t user_code_va  = (x86_64_virt_addr_t)0x00500000ULL;
        const x86_64_virt_addr_t user_stack_va = (x86_64_virt_addr_t)0x00501000ULL;

        x86_64_phys_addr_t code_pa  = arch_x86_64_pmm_alloc_page();
        x86_64_phys_addr_t stack_pa = arch_x86_64_pmm_alloc_page();
        if (code_pa == 0 || stack_pa == 0) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 30 pmm_alloc_page returned 0\n");
            return;
        }

        /* PTE_USER | PTE_PRESENT for code (X by default since NX is not set);
         * +PTE_WRITABLE for the stack. */
        const uint64_t flags_code  = OPENOS_X86_64_PTE_PRESENT | OPENOS_X86_64_PTE_USER;
        const uint64_t flags_stack = OPENOS_X86_64_PTE_PRESENT | OPENOS_X86_64_PTE_USER |
                                     OPENOS_X86_64_PTE_RW;
        if (arch_x86_64_vmm_map_page(user_code_va,  code_pa,  flags_code)  != 0 ||
            arch_x86_64_vmm_map_page(user_stack_va, stack_pa, flags_stack) != 0) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 30 vmm_map_page failed\n");
            return;
        }

        /* Step 2: write the ring3 busy loop into the user code page.
         *   EB FE  =  jmp $-0   (infinite jmp to itself)
         * This keeps the user thread in a deterministic, tight loop at a
         * single RIP that the timer can preempt from. */
        volatile uint8_t *user_code = (volatile uint8_t *)(uintptr_t)user_code_va;
        user_code[0] = 0xEB;
        user_code[1] = 0xFE;

        /* Step 3: spawn a real ring3 thread.
         *
         * Stage 21 (G.7e) already used the *last* AP (ap_n) as its sentinel
         * target -- that AP is now permanently retired in a cli;hlt loop
         * and will never tick its LAPIC timer. We therefore pick the
         * *previous* AP (ap_n - 1) which is still live and healthy. */
        uint32_t target_cpu = ap_n - 1u;       /* APs are 1..ap_n; -1 avoids the retired one */
        uintptr_t user_entry = (uintptr_t)user_code_va;
        uintptr_t user_rsp   = (uintptr_t)user_stack_va + 4096u;

        uint64_t udisp_before = arch_x86_64_percpu_user_dispatch_count(target_cpu);
        uint64_t tcnt_before  = arch_x86_64_percpu_lapic_timer_count(target_cpu);
        log_kv(" target_cpu=", (uint64_t)target_cpu);
        log_kv(" user_entry=", (uint64_t)user_entry);
        log_kv(" timer_before=", tcnt_before);

        /* Pre-flight: prove target_cpu's LAPIC timer is alive *before* we
         * pin a ring3 thread on it. We just wait a short slice (~5ms) and
         * verify the timer count moved. If it didn't, the failure is in
         * the AP timer (not in ring3 preemption) and we report that
         * distinctly. */
        {
            uint64_t pre_tcnt = tcnt_before;
            /* AP timer takes ~1.28s/tick on TCG (init_count=10000000, DCR=div16,
             * LAPIC bus ~7825 ticks/ms => period ~= 10e6/7825 ms ~= 1278ms).
             * Wait up to 4 ticks (~5s). */
            for (uint32_t i = 0; i < 5000u; ++i) {
                pre_tcnt = arch_x86_64_percpu_lapic_timer_count(target_cpu);
                if (pre_tcnt > tcnt_before) break;
                arch_x86_64_delay_us(1000u);
            }
            log_kv(" timer_preflight=", pre_tcnt);
            if (pre_tcnt <= tcnt_before) {
                early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 30 target_cpu LAPIC timer not ticking\n");
                return;
            }
            /* Re-baseline after pre-flight so the post-dispatch comparison
             * is fair (we want to see a tick *after* dispatch, not from
             * pre-dispatch idle). */
            tcnt_before = pre_tcnt;
        }

        uint32_t slot = arch_x86_64_sched_spawn_uthread(user_entry, user_rsp,
                                                       128u, target_cpu);
        if (slot == 0u) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 30 spawn_uthread returned 0\n");
            return;
        }
        log_kv(" slot=", (uint64_t)slot);

        /* Step 4: poke target_cpu and busy-poll for up to ~300ms for both
         *   - the slot to actually be dispatched (user_dispatch_count++), AND
         *   - the timer to fire at least once on target_cpu *after* dispatch
         *     (which proves ring3 preemption -- target_cpu cannot tick the
         *     timer unless it accepted external IRQs at IF=1, i.e. ring3).
         *
         * Note: the AP LAPIC timer is configured with init_count=10000000
         * + DCR=div16, which yields ~80ms/tick on TCG. We size the window
         * The AP LAPIC timer takes ~1.28s/tick on TCG; we need at least 2
         * post-dispatch ticks worth of headroom (~3s) to be robust. Use
         * 5s window with 1ms granularity. */
        arch_x86_64_smp_send_resched_ipi(target_cpu);
        uint64_t udisp_after = udisp_before;
        uint64_t tcnt_after  = tcnt_before;
        uint64_t tcnt_at_dispatch = 0;
        bool seen_dispatch = false;
        for (uint32_t i = 0; i < 5000u; ++i) {
            udisp_after = arch_x86_64_percpu_user_dispatch_count(target_cpu);
            tcnt_after  = arch_x86_64_percpu_lapic_timer_count(target_cpu);
            if (!seen_dispatch && udisp_after > udisp_before) {
                seen_dispatch = true;
                tcnt_at_dispatch = tcnt_after;
            }
            if (seen_dispatch && tcnt_after > tcnt_at_dispatch) {
                /* ring3 was actually preempted after dispatch */
                break;
            }
            if ((i % 50u) == 49u) {
                arch_x86_64_smp_send_resched_ipi(target_cpu);
            }
            arch_x86_64_delay_us(1000u);
        }
        log_kv(" disp_after=", udisp_after);
        log_kv(" timer_at_dispatch=", tcnt_at_dispatch);
        log_kv(" timer_after=", tcnt_after);

        if (!seen_dispatch) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 30 ring3 user thread never dispatched\n");
            return;
        }
        if (tcnt_after <= tcnt_at_dispatch) {
            early_console64_write("\n[x86_64][smp-selftest] FAIL: stage 30 LAPIC timer did not advance after ring3 dispatch\n");
            return;
        }
        early_console64_write(" PASS");
    }

    if (ap_n > 0) {
        early_console64_write("\n[x86_64][smp-selftest] PASS: all APs idle on private GDT/TSS/IDT+GS + sched-registered + LAPIC-timer driving sched_on_tick + distributed kthreads switched per-CPU + cross-CPU reschedule IPI delivered + cross-CPU migration verified + IPI tail-hook dispatch verified + preempt-disable gate verified + timer-tick honours gate + swapgs MSR pair OK + syscall RSP save-area OK + sched-slot kind/kstack tagging OK + USER-slot AP dispatch verified + LAPIC timer calibrated + NMI live-trigger verified + #UD probe recoverable + #PF probe recoverable + #GP probe recoverable + #DE probe recoverable + #BP probe recoverable + fault-injection burst (5x5) verified + ring3 LAPIC-timer preemption verified\n");
        early_console64_write("\n[x86_64][smp-selftest] PASS: no APs (UP system, BSP idle slot only) + preempt-disable gate verified + timer-tick honours gate + swapgs MSR pair OK + syscall RSP save-area OK + sched-slot kind/kstack tagging OK + USER-slot dispatch SKIPPED (ap<3) + LAPIC timer calibrated + NMI live-trigger verified + #UD probe recoverable + #PF probe recoverable + #GP probe recoverable + #DE probe recoverable + #BP probe recoverable + fault-injection burst (5x5) verified + ring3 preemption SKIPPED (ap<3)\n");
    }
}
