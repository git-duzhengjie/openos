#ifndef OPENOS_ARCH_X86_64_IDT_SELFTEST64_H
#define OPENOS_ARCH_X86_64_IDT_SELFTEST64_H

/*
 * Step F.1: post-init read-back of the installed IDT.
 *
 * Walks vectors 0..31 (CPU exceptions) plus the legacy int 0x80 compat gate
 * and validates:
 *   - present bit set
 *   - gate type is the expected interrupt/trap gate
 *   - DPL matches policy (0 for exceptions, 3 for int 0x80)
 *   - selector points at the kernel CS
 *   - handler offset is non-zero and lives above 1 MiB (i.e. in our text)
 *
 * Returns 0 on PASS, non-zero on the first failing vector. Never triggers a
 * real exception, never modifies the IDT. Safe to call right after
 * arch_x86_64_idt_init().
 */
int arch_x86_64_idt_selftest_run(void);

#endif /* OPENOS_ARCH_X86_64_IDT_SELFTEST64_H */
