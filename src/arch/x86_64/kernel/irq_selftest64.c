#include "../include/irq_selftest64.h"
#include "../include/idt64.h"
#include "../include/pic64.h"
#include "../include/pit64.h"
#include "../include/tsc64.h"
#include "../include/early_console64.h"

#include <stdint.h>

/* Step F.2 — IRQ0 self-test.
 *
 * Design constraints:
 *   - Must NOT depend on the scheduler (F.3 will). We poll only.
 *   - Must NOT leave interrupts enabled on exit. Higher-level code (the
 *     scheduler in F.3) owns the policy decision of when to globally
 *     unmask IRQs.
 *   - Tolerance band is generous on purpose. The PIT is asynchronous to
 *     the TSC, so a 200 ms wait can legitimately count 18..22 ticks at
 *     100 Hz. Tightening this would just produce flaky tests.
 */

#define IRQ_SELFTEST_WAIT_MS    200u
#define IRQ_SELFTEST_MIN_TICKS  18u
#define IRQ_SELFTEST_MAX_TICKS  22u

static inline void cli(void) { __asm__ __volatile__("cli"); }
static inline void sti(void) { __asm__ __volatile__("sti"); }
static inline void pause(void) { __asm__ __volatile__("pause"); }

static void log_kv_hex(const char *key, uint64_t val) {
    early_console64_write(key);
    early_console64_write_hex64(val);
}

int arch_x86_64_irq_selftest_run(void) {
    early_console64_write("\n[x86_64][irq-selftest] begin");

    /* Debug: dump IDT[0x20] gate descriptor before sti so any mis-write
     * is caught here rather than as a #GP on iretq. */
    {
        struct x86_64_idt_gate_info info;
        if (arch_x86_64_idt_query_gate(0x20u, &info) == 0) {
            log_kv_hex("\n[x86_64][irq-selftest] idt[0x20] offset=", info.offset);
            log_kv_hex(" sel=", info.selector);
            log_kv_hex(" ist=", info.ist);
            log_kv_hex(" type_attr=", info.type_attr);
        } else {
            early_console64_write("\n[x86_64][irq-selftest] idt[0x20] query FAIL");
        }
    }

    /* Sanity: TSC must already be calibrated. Without it we cannot bound
     * the wait, and any PASS would be meaningless. */
    uint64_t per_ms = arch_x86_64_tsc_per_ms();
    if (per_ms == 0u) {
        early_console64_write("\n[x86_64][irq-selftest] FAIL tsc not calibrated\n");
        return 1;
    }

    uint64_t before_ticks = arch_x86_64_pit_get_ticks();

    /* Pre-condition: IRQs globally disabled. Make it explicit. */
    cli();
    arch_x86_64_pic_unmask(0u);
    sti();

    uint64_t t0 = arch_x86_64_tsc_uptime_ms();
    uint64_t deadline = t0 + IRQ_SELFTEST_WAIT_MS;
    while (arch_x86_64_tsc_uptime_ms() < deadline) {
        pause();
    }
    uint64_t t1 = arch_x86_64_tsc_uptime_ms();

    cli();
    arch_x86_64_pic_mask(0u);

    uint64_t after_ticks = arch_x86_64_pit_get_ticks();
    uint64_t delta = after_ticks - before_ticks;

    log_kv_hex("\n[x86_64][irq-selftest] before_ticks=", before_ticks);
    log_kv_hex(" after_ticks=", after_ticks);
    log_kv_hex(" delta=", delta);
    log_kv_hex(" t0_ms=", t0);
    log_kv_hex(" t1_ms=", t1);

    int ok = (delta >= IRQ_SELFTEST_MIN_TICKS && delta <= IRQ_SELFTEST_MAX_TICKS);
    if (ok) {
        early_console64_write("\n[x86_64][irq-selftest] PASS\n");
        return 0;
    }
    early_console64_write("\n[x86_64][irq-selftest] FAIL out-of-range\n");
    return 1;
}
