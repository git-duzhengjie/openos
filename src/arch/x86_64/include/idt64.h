#ifndef OPENOS_ARCH_X86_64_IDT64_H
#define OPENOS_ARCH_X86_64_IDT64_H

#include <stdint.h>

#include "arch64_types.h"

#define OPENOS_X86_64_IDT_ENTRY_COUNT 256u
#define OPENOS_X86_64_EXCEPTION_COUNT 32u
#define OPENOS_X86_64_IDT_INTERRUPT_GATE 0x8Eu
#define OPENOS_X86_64_IDT_TRAP_GATE 0x8Fu

struct x86_64_exception_frame {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rbp;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;
    uint64_t vector;
    uint64_t error_code;
    x86_64_entry_t rip;
    uint64_t cs;
    uint64_t rflags;
    x86_64_stack_ptr_t rsp;
    uint64_t ss;
} __attribute__((packed));

void arch_x86_64_idt_init(void);

/*
 * Step G.6.1: AP-side IDT activation.
 *
 * The IDT itself (table + gate descriptors) is built once on the BSP and is
 * read-only thereafter, so every CPU shares the same `idt64[]` storage. What
 * is NOT shared is IDTR — each CPU has its own. This helper performs a bare
 * `lidt` against the existing global idt64_ptr without touching the table
 * contents, so APs can route exceptions/interrupts through the same handlers
 * as the BSP.
 */
void arch_x86_64_idt_load_ap(void);
void arch_x86_64_idt_print_status(void);
void arch_x86_64_exception_dispatch(struct x86_64_exception_frame *frame);

/*
 * Step F.1: read-only inspector for the installed IDT. Used by the IDT
 * registration selftest to confirm that all 32 CPU-exception vectors plus
 * the legacy int 0x80 compat gate carry the right offset / selector / DPL /
 * IST without ever actually triggering an exception.
 */
struct x86_64_idt_gate_info {
    uint64_t offset;       /* reassembled 64-bit handler entry point */
    uint16_t selector;     /* code segment selector loaded by the gate */
    uint8_t ist;           /* IST index (0 = no IST stack switch) */
    uint8_t type_attr;     /* raw type_attr byte (P|DPL|0|gate-type) */
};

int arch_x86_64_idt_query_gate(uint8_t vector, struct x86_64_idt_gate_info *out);

/*
 * Step F.2: register an external-interrupt gate (used by pic64/pit64 for
 * IRQ0..IRQ15 wired to CPU vectors 0x20..0x2F). DPL=0, interrupt gate,
 * IST=0. Returns 0 on success, -1 if the vector is outside the PIC range
 * (we deliberately refuse to overwrite exception or syscall gates).
 */
int arch_x86_64_idt_register_irq(uint8_t cpu_vector, void (*handler)(void));

/*
 * Step G.x: post-EXIT kernel-fault sentry.
 *
 * Earlier work fixed a real bug where, after a ring3 program SYS_EXIT'd,
 * the kernel return path landed at a bogus RIP and immediately #UD'd in
 * ring0 (see commit 0b14358). The fix shipped, but there was no explicit
 * counter to catch a regression — a silently-broken return path would
 * once again just look like a triple-fault reset.
 *
 * These counters are bumped by arch_x86_64_exception_dispatch() whenever
 * a CPU exception is taken with the saved CS pointing at a ring0
 * selector. Selftests around the ring3 drop sample them before/after and
 * fail loudly on any delta.
 *
 * The "first hit" snapshot stores vector / error_code / RIP / RSP from
 * the very first ring0 exception seen, so a regression dump points
 * straight at the offending instruction.
 */
struct x86_64_kernel_fault_snapshot {
    uint64_t count;        /* total ring0 exceptions observed */
    uint64_t ud_count;     /* #UD subset (vector 6) */
    uint64_t gp_count;     /* #GP subset (vector 13) */
    uint64_t pf_count;     /* #PF subset (vector 14) */
    uint64_t first_vector;
    uint64_t first_error;
    uint64_t first_rip;
    uint64_t first_rsp;
};

uint64_t arch_x86_64_idt_kernel_fault_count(void);
uint64_t arch_x86_64_idt_kernel_ud_count(void);
void arch_x86_64_idt_kernel_fault_snapshot(struct x86_64_kernel_fault_snapshot *out);
void arch_x86_64_idt_print_kernel_fault_stats(void);

/* G.7g-2: monotonic NMI delivery counter (global, all CPUs). */
uint64_t arch_x86_64_idt_nmi_count(void);

/*
 * G.3b-3: recoverable single-shot fault probe.
 *
 * arm_ud_probe(rip, len): the very next #UD whose frame->rip exactly matches
 *   `rip` is treated as expected — the dispatcher increments ud_probe_count,
 *   advances frame->rip by `len` bytes (so iretq resumes past the faulting
 *   instruction), and returns WITHOUT polluting the kernel fault snapshot or
 *   halting. The probe disarms itself on hit so it is genuinely single-shot.
 *
 * Intended use: selftest places a `ud2` (len=2) at a known label, arms with
 *   (&label, 2), executes ud2, and asserts ud_probe_count incremented by 1.
 *   Together with G.7g-2 NMI live-fire, this validates that the IDT plumbing
 *   not only routes exceptions but can also *recover* from them.
 */
void     arch_x86_64_idt_arm_ud_probe(uint64_t expected_rip, uint32_t insn_len);
void     arch_x86_64_idt_disarm_ud_probe(void);
uint64_t arch_x86_64_idt_ud_probe_count(void);
int      arch_x86_64_idt_ud_probe_is_armed(void);

/*
 * G.3b-4: recoverable single-shot #PF probe.
 *
 * arm_pf_probe(rip, len, cr2): the very next #PF whose frame->rip matches
 *   `rip` AND whose CR2 matches `cr2` exactly is treated as expected — the
 *   dispatcher increments pf_probe_count, advances frame->rip by `len`
 *   bytes (skipping the faulting load/store), and returns WITHOUT polluting
 *   the kernel fault snapshot or halting. The probe disarms itself on hit.
 *
 * The dual (rip,cr2) match guarantees we never silently swallow an
 * unrelated #PF — an unexpected fault at the same RIP but different CR2,
 * or vice versa, still falls through to the normal fatal path.
 *
 * Note: this does NOT install a mapping for `cr2`. If the same instruction
 *   re-executes after probe disarm, it will fault for real.
 */
void     arch_x86_64_idt_arm_pf_probe(uint64_t expected_rip, uint32_t insn_len, uint64_t expected_cr2);
void     arch_x86_64_idt_disarm_pf_probe(void);
uint64_t arch_x86_64_idt_pf_probe_count(void);
int      arch_x86_64_idt_pf_probe_is_armed(void);

#endif /* OPENOS_ARCH_X86_64_IDT64_H */
