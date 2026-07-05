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
    /*
     * A2.P3-B-β: optional callee-saved register snapshot.
     * Loaded by arch_x86_64_iretq_enter_user_full() before iretq.
     * The default iretq_enter_user stub IGNORES these and zeros all GPRs
     * (correct for a fresh user thread spawn). The _full variant exists
     * specifically for fork resume_child so the child observes the same
     * callee-saved state the parent had right before the SYS_FORK call.
     */
    uint64_t rbx;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
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

/*
 * Reusable program launcher for the interactive shell.
 * Loads `path` (VFS/RAMFS first, initrd fallback), builds a fresh address
 * space, spawns a user PCB, drops to ring3, and drives execve/fork rounds
 * until the tree exits. Returns the exit code (>=0) or -1 on failure.
 * argc/argv and envc/envp may be 0/NULL (defaults argv[0]=path).
 */
int arch_x86_64_usermode_launch_path(const char *path,
                                     int argc, const char **argv,
                                     int envc, const char **envp);
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

/*
 * H.4 argv plumbing.
 *
 * arch_x86_64_usermode_set_args():
 *   Called by the kernel (either from kernel64.c at initial spawn or from
 *   the SYS_EXEC backend after a successful ELF reload). Copies up to
 *   X86_64_USER_ARGV_MAX strings, each truncated to X86_64_USER_ARG_MAX-1
 *   bytes, into a kernel-side scratch buffer. The strings live in kernel
 *   memory which is *not* about to be overwritten by elf64_load_image
 *   (unlike the source argv which may live in the current ring3 image's
 *   .rodata at the same VA as the new image). This is the H.3 path-lifetime
 *   workaround applied to argv.
 *
 * arch_x86_64_usermode_seed_user_stack():
 *   Called by arch_x86_64_usermode_run() right before it composes the
 *   iretq frame. Lays out the SysV ABI program-startup frame at the top
 *   of the user stack:
 *       [argv strings...]            <- top of stack, NUL terminated
 *       [16-byte alignment pad]
 *       [argv[N] = NULL]
 *       [argv[N-1]]
 *       ...
 *       [argv[0]]                    <- pointed to by 8(%rsp)
 *       [argc]                       <- 0(%rsp)
 *   Returns the (already 16-byte aligned) new stack top that should be
 *   used as the iretq frame's user RSP. When no args are queued the
 *   returned value equals stack_top_in and the frame collapses to
 *   argc=0, argv={NULL}.
 */
#define X86_64_USER_ARGV_MAX 8u
#define X86_64_USER_ARG_MAX  128u

/* H.5a: independent env table sized like argv (kept symmetric for simplicity). */
#define X86_64_USER_ENVP_MAX 8u
#define X86_64_USER_ENV_MAX  128u

void arch_x86_64_usermode_set_args(int argc, const char *const *argv);
void arch_x86_64_usermode_clear_args(void);
int arch_x86_64_usermode_pending_argc(void);

/*
 * H.5a env plumbing.
 *
 * Same lifetime rules as set_args: kernel snapshots the strings before the
 * ELF loader gets to clobber them. envp == NULL or envc == 0 clears the
 * table; the seeded user stack then collapses to envp = { NULL }.
 */
void arch_x86_64_usermode_set_envs(int envc, const char *const *envp);
void arch_x86_64_usermode_clear_envs(void);
int arch_x86_64_usermode_pending_envc(void);

x86_64_virt_addr_t arch_x86_64_usermode_seed_user_stack(x86_64_virt_addr_t stack_top_in);

/*
 * P4.c.1 (PML4[0] U-bit removal prep): same as seed_user_stack, but
 * every pointer value written into the user stack (argv[i], envp[i])
 * and the returned RSP are biased by `pointer_va_bias` before being
 * exposed to ring3. The kernel-side writes still go through the
 * `stack_top_in` view (which must be kernel-accessible), so the caller
 * is responsible for ensuring the physical pages backing
 * `stack_top_in` are also reachable at `stack_top_in + pointer_va_bias`
 * from ring3 (typically via an aliasing mapping at PML4[1]).
 *
 * NULL pointers (argv/envp terminators) are kept as 0 -- they are not
 * biased. Setting pointer_va_bias == 0 makes this function behave
 * exactly like the legacy seed_user_stack().
 */
x86_64_virt_addr_t arch_x86_64_usermode_seed_user_stack_ex(
    x86_64_virt_addr_t stack_top_in,
    x86_64_virt_addr_t pointer_va_bias);

/*
 * A2.P3-B (vfork-flavored fork, alpha cut).
 *
 * arch_x86_64_usermode_resume_child():
 *   Re-enters ring3 using the trapframe snapshot the SYS_FORK wrapper
 *   stashed into the current PCB (see proc64.h::fork_pending et al.).
 *   The frame is reconstructed so that:
 *     - rip = parent's next-after-syscall PC
 *         (int80 path: saved_fork_frame_int80.rip;
 *          syscall path: saved_fork_frame_sysc.rcx, which the SYSCALL
 *          instruction itself parked the return PC into.)
 *     - rsp = parent's user rsp at fork time
 *         (int80 path: in-frame rsp; syscall path: PCB.fork_user_rsp
 *          read from %gs:syscall_user_rsp at wrapper entry.)
 *     - rflags = parent's user rflags
 *         (int80 path: in-frame rflags; syscall path: saved r11.)
 *     - cs/ss = ring3 user code/data selectors.
 *   GPRs are zeroed by arch_x86_64_iretq_enter_user, so the child sees
 *   rax == 0 (the fork-returns-0-in-child convention). Caller-saved
 *   registers being clobbered is acceptable: the user-side openos64_fork()
 *   wrapper marks them as such ("memory" + GPR clobber list).
 *
 *   Clears fork_pending before returning to ring3 so this is idempotent.
 *   Does NOT save a kernel-side resume context: in P3-B-alpha the kernel
 *   does not plan to come back to the parent (parent return is P3-B-beta).
 *   The child therefore behaves like the only ring3 thread once resumed,
 *   and on SYS_EXIT the regular usermode_return_to_kernel longjmp pops
 *   us back to the original arch_x86_64_usermode_run() invocation.
 */
void arch_x86_64_usermode_resume_child(void);

/* A2.P2: drive the current pending vfork-style child from wait()/waitpid(). */
int arch_x86_64_usermode_run_pending_child_for_wait(void);

#endif /* OPENOS_ARCH_X86_64_USERMODE64_H */
