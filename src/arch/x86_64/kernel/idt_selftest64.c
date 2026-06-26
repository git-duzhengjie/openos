#include "../include/idt_selftest64.h"

#include <stdint.h>

#include "../include/early_console64.h"
#include "../include/gdt64.h"
#include "../include/idt64.h"

/*
 * Step F.1 IDT registration selftest.
 *
 * Purpose
 * -------
 * The IDT/TSS/ISR skeleton has actually been wired in since early commits, but
 * we never had a way to prove "every CPU exception vector still routes to the
 * intended handler" short of triggering the exception itself. That is exactly
 * the regression class we want to catch before flipping on IRQs in Step F.2
 * (PIT) and Step F.3 (preemption) — a silently-rewritten gate would otherwise
 * turn a #PF into a triple-fault and reset QEMU without a single log line.
 *
 * Strategy
 * --------
 * - Pure read-back via arch_x86_64_idt_query_gate(). No fault is taken.
 * - Validate vectors 0..31 are present, kernel-DPL, interrupt-gate, and that
 *   the handler offset lives above 1 MiB (rules out a NULL/low-mem entry).
 * - Validate vector 0x80 (legacy int 0x80 compat) is present with DPL=3 so
 *   ring3 can still issue it.
 * - Validate the selector field matches the kernel code segment we set in
 *   gdt64. A divergence here usually means someone resorted the GDT entries
 *   and forgot to refresh the IDT install path.
 *
 * Constraints
 * -----------
 * - Must run *after* arch_x86_64_idt_init().
 * - Must not touch IF (no sti / cli).
 * - Single-CPU only — when SMP lands, the BSP runs this; AP path will have
 *   its own selftest that re-reads its own IDTR.
 */

#define IDT_SELFTEST_VECTOR_COUNT 32u
#define IDT_SELFTEST_INT80_VECTOR 0x80u
#define IDT_SELFTEST_MIN_HANDLER  0x00100000ULL /* handlers live above 1 MiB */

static void idt_selftest_log_fail(const char *tag, uint8_t vector,
                                  const struct x86_64_idt_gate_info *info) {
    /*
     * Always print enough state to diff against the expected wiring without
     * relying on a debugger. Keeping these prints terse lets the selftest
     * stay quiet on the happy path while still being self-describing on
     * failure.
     */
    early_console64_write("[x86_64][idt-selftest] FAIL ");
    early_console64_write(tag);
    early_console64_write(" vector=");
    early_console64_write_hex64((uint64_t)vector);
    early_console64_write(" offset=");
    early_console64_write_hex64(info->offset);
    early_console64_write(" sel=");
    early_console64_write_hex64((uint64_t)info->selector);
    early_console64_write(" ist=");
    early_console64_write_hex64((uint64_t)info->ist);
    early_console64_write(" type=");
    early_console64_write_hex64((uint64_t)info->type_attr);
    early_console64_write("\n");
}

static int idt_selftest_check_common(uint8_t vector, uint8_t expected_dpl) {
    /*
     * One vector at a time. We pull a snapshot via the read-back API so the
     * checks below are operating on a stable copy — no risk of a racing
     * update mid-check (there is no other CPU yet, but the discipline keeps
     * us honest for the SMP follow-up).
     */
    struct x86_64_idt_gate_info info = {0};
    if (arch_x86_64_idt_query_gate(vector, &info) != 0) {
        early_console64_write("[x86_64][idt-selftest] FAIL query vector=");
        early_console64_write_hex64((uint64_t)vector);
        early_console64_write("\n");
        return 1;
    }

    /* Present bit (bit 7 of type_attr). A cleared P bit means the CPU will
     * raise #NP if the vector ever fires — exactly the silent-fail we are
     * trying to catch here. */
    if ((info.type_attr & 0x80u) == 0u) {
        idt_selftest_log_fail("present", vector, &info);
        return 2;
    }

    /* Gate type must be interrupt-gate (0xE) or trap-gate (0xF). int 0x80 is
     * installed as an interrupt-gate in this codebase; either is acceptable
     * here as long as it is one of the two. */
    uint8_t gate_type = (uint8_t)(info.type_attr & 0x0Fu);
    if (gate_type != 0x0Eu && gate_type != 0x0Fu) {
        idt_selftest_log_fail("gate-type", vector, &info);
        return 3;
    }

    /* DPL must match policy. Exceptions: DPL=0 so ring3 cannot raise them
     * via `int N`. int 0x80: DPL=3 so legacy syscall path stays callable. */
    uint8_t dpl = (uint8_t)((info.type_attr >> 5) & 0x03u);
    if (dpl != expected_dpl) {
        idt_selftest_log_fail("dpl", vector, &info);
        return 4;
    }

    /* Selector must be the kernel CS. If this diverges, the GDT layout was
     * shuffled without updating the IDT install path. */
    if (info.selector != arch_x86_64_gdt_kernel_code_selector()) {
        idt_selftest_log_fail("selector", vector, &info);
        return 5;
    }

    /* Handler must live in our text (above 1 MiB). A zero/low offset is the
     * classic symptom of an uninitialised gate. */
    if (info.offset < IDT_SELFTEST_MIN_HANDLER) {
        idt_selftest_log_fail("offset", vector, &info);
        return 6;
    }

    return 0;
}

int arch_x86_64_idt_selftest_run(void) {
    early_console64_write("[x86_64][idt-selftest] begin\n");

    /* Vectors 0..31: CPU exceptions, DPL=0. */
    for (uint32_t v = 0; v < IDT_SELFTEST_VECTOR_COUNT; ++v) {
        int rc = idt_selftest_check_common((uint8_t)v, /*expected_dpl=*/0u);
        if (rc != 0) {
            return rc;
        }
    }

    /* Legacy int 0x80 syscall compat gate, DPL=3. */
    int rc = idt_selftest_check_common((uint8_t)IDT_SELFTEST_INT80_VECTOR,
                                       /*expected_dpl=*/3u);
    if (rc != 0) {
        return rc;
    }

    early_console64_write("[x86_64][idt-selftest] PASS exceptions=0x20 int80=ok\n");
    return 0;
}
