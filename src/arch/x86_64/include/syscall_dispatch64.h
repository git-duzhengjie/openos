#ifndef OPENOS_ARCH_X86_64_SYSCALL_DISPATCH64_H
#define OPENOS_ARCH_X86_64_SYSCALL_DISPATCH64_H

#include <stdint.h>

/*
 * Architecture-neutral syscall dispatcher for the x86_64 kernel.
 *
 * Phase A goal: unify the two syscall entry paths (int 0x80 compat and the
 * native syscall instruction) so they share a single dispatch table. Both
 * paths now normalize their register state into the six-argument calling
 * convention below and forward to arch_x86_64_syscall_dispatch_common().
 *
 * Syscall numbering follows the canonical table in src/kernel/include/syscall.h
 * (the same table the i386 port uses). The current implementation only wires
 * a handful of syscalls (EXIT/WRITE/GETPID) to existing x86_64 subsystems;
 * unimplemented numbers return ENOSYS so the user program sees -1.
 *
 * Subsequent phases will replace ENOSYS branches with real backends as each
 * kernel subsystem (VFS/proc/net/...) is ported to x86_64.
 */
uint64_t arch_x86_64_syscall_dispatch_common(uint64_t num,
                                             uint64_t a0,
                                             uint64_t a1,
                                             uint64_t a2,
                                             uint64_t a3,
                                             uint64_t a4,
                                             uint64_t a5);

/* Reset internal counters (used by tests / re-init paths). */
void arch_x86_64_syscall_dispatch_reset(void);

/* Telemetry: total number of dispatches that reached the common layer. */
uint64_t arch_x86_64_syscall_dispatch_total(void);

/* Telemetry: total number of dispatches that returned ENOSYS. */
uint64_t arch_x86_64_syscall_dispatch_enosys(void);

#endif /* OPENOS_ARCH_X86_64_SYSCALL_DISPATCH64_H */
