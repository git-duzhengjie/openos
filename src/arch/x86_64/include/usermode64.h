#ifndef OPENOS_ARCH_X86_64_USERMODE64_H
#define OPENOS_ARCH_X86_64_USERMODE64_H

#include <stdint.h>

#include "arch64_types.h"

typedef struct x86_64_user_iretq_frame {
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} x86_64_user_iretq_frame_t;

void arch_x86_64_usermode_init(void);
void arch_x86_64_usermode_prepare_iretq(x86_64_user_iretq_frame_t *frame,
                                         x86_64_entry_t entry,
                                         x86_64_virt_addr_t stack_top);
uint8_t arch_x86_64_usermode_validate_frame(const x86_64_user_iretq_frame_t *frame);
const x86_64_user_iretq_frame_t *arch_x86_64_usermode_get_prepared_frame(void);
void arch_x86_64_usermode_print_status(void);
uint8_t arch_x86_64_usermode_is_running(void);
uint8_t arch_x86_64_usermode_has_exited(void);
int arch_x86_64_usermode_exit_code(void);
int arch_x86_64_usermode_run(x86_64_entry_t entry);
void arch_x86_64_usermode_mark_exited(int code);
void arch_x86_64_usermode_return_to_kernel(void) __attribute__((noreturn));

/*
 * Step G.x: post-EXIT kernel-fault sentry exports for the ring3 selftest.
 *
 * - canary: 0 before run, 1 inside the kernel-context save, 2 after the
 *   inline-asm return path. Selftest checks canary == 2.
 * - kfault_delta: how many ring0 exceptions the IDT saw while we were
 *   off in ring3 + on the return path. Healthy runs MUST yield 0.
 */
uint64_t arch_x86_64_usermode_canary(void);
uint64_t arch_x86_64_usermode_kfault_delta(void);

/*
 * H.3 execve support (trampoline-style, zero-asm-diff).
 *
 * arch_x86_64_usermode_mark_exec(entry):
 *   Called by the SYS_EXEC backend from inside ring0 after a successful
 *   ELF reload. Sets `usermode_pending_exec=1` + stashes the new entry,
 *   then control falls through to arch_x86_64_usermode_return_to_kernel()
 *   which longjmps back to the saved kernel context inside
 *   arch_x86_64_usermode_run(). The kernel side observes the pending_exec
 *   flag instead of `exited` and loops back into ring3 with the new entry.
 *
 * This deliberately keeps the syscall-entry asm untouched and reuses the
 * already-verified G.x exit trampoline.
 */
void arch_x86_64_usermode_mark_exec(x86_64_entry_t entry);
uint8_t arch_x86_64_usermode_has_pending_exec(void);
x86_64_entry_t arch_x86_64_usermode_take_pending_exec(void);
uint64_t arch_x86_64_usermode_exec_count(void);
uint64_t arch_x86_64_usermode_exec_fail_count(void);
void arch_x86_64_usermode_note_exec_fail(void);

#endif /* OPENOS_ARCH_X86_64_USERMODE64_H */
