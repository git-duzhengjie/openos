/*
 * x86_64 syscall entry layer.
 *
 * This file owns the two architecture-specific entry paths:
 *   1) int 0x80 compat trap (used by ELF32 / shared-ABI user code)
 *   2) syscall/sysret native path (used by ELF64 user code)
 *
 * "Which syscall does what" lives in syscall_dispatch64.c. The two entries
 * below only translate register state into the six-argument canonical form
 * and forward to arch_x86_64_syscall_dispatch_common().
 */

#include "../include/syscall64.h"

#include <stddef.h>

#include "../include/early_console64.h"
#include "../include/gdt64.h"
#include "../include/percpu64.h"
#include "../include/proc64.h"
#include "../include/sched64.h"        /* gamma.3b-alpha: PARKED child slot */
#include "../include/smp64.h"          /* gamma.3b-S2a Seg-5: cpu_count for target CPU */
#include "../include/syscall_dispatch64.h"
#include "../include/usermode64.h"
#include "syscall.h" /* canonical SYS_* numbers (shared with i386) */

extern void x86_64_syscall_entry(void);
extern void arch_x86_64_usermode_syscall_return_trampoline(void);

static uint32_t syscall64_current_abi;
static uint64_t int80_dispatch_count;
static uint64_t syscall_dispatch_count;
static uint8_t syscall_sysret_enabled;

static uint64_t rdmsr64(uint32_t msr) {
    uint32_t lo;
    uint32_t hi;
    __asm__ __volatile__("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static void wrmsr64(uint32_t msr, uint64_t value) {
    __asm__ __volatile__("wrmsr" : : "c"(msr), "a"((uint32_t)value), "d"((uint32_t)(value >> 32)) : "memory");
}

static void configure_syscall_sysret(void) {
    uint64_t star;
    uint64_t efer;

    star = ((uint64_t)arch_x86_64_gdt_kernel_code_selector() << 32) |
           ((uint64_t)(OPENOS_X86_64_GDT_USER_CODE | 3u) << 48);
    wrmsr64(OPENOS_X86_64_MSR_STAR, star);
    wrmsr64(OPENOS_X86_64_MSR_LSTAR, (x86_64_entry_t)(uintptr_t)x86_64_syscall_entry);
    wrmsr64(OPENOS_X86_64_MSR_FMASK, OPENOS_X86_64_SYSCALL_FMASK);

    efer = rdmsr64(OPENOS_X86_64_MSR_EFER);
    wrmsr64(OPENOS_X86_64_MSR_EFER, efer | OPENOS_X86_64_EFER_SCE);
    syscall_sysret_enabled = 1u;
}

/* γ.3b-S2a: AP-side syscall init. Only wires per-CPU MSRs
 * (STAR/LSTAR/FMASK/EFER.SCE) so that `syscall` doesn't #UD on APs.
 * Does NOT touch global counters/ABI state (those belong to the BSP). */
void arch_x86_64_syscall_init_ap(void) {
    configure_syscall_sysret();
}

void arch_x86_64_syscall_init(void) {
    syscall64_current_abi = OPENOS_X86_64_SYSCALL_ABI_INT80_COMPAT;
    int80_dispatch_count = 0;
    syscall_dispatch_count = 0;
    syscall_sysret_enabled = 0;
    arch_x86_64_syscall_dispatch_reset();
    configure_syscall_sysret();
}

/* ------------------------------------------------------------------
 * A2.P3-B — vfork-semantics SYS_FORK short-circuit.
 *
 * Reached from BOTH wrappers below before the canonical dispatch_common()
 * call, because dispatch_common() doesn't see the architectural trapframe
 * and we need that frame to re-enter ring3 as the child later.
 *
 * Snapshots:
 *   - the trapframe into the PCB (one of two unions, picked by via_syscall),
 *   - for the SYSCALL path: %gs:syscall_user_rsp into PCB.fork_user_rsp
 *     (the syscall trapframe carries kernel rsp, not user rsp; user rsp
 *      was stashed at entry by syscall_sysret64.S).
 *
 * Sets fork_pending=1. The main loop in kernel64 picks this up after the
 * parent's usermode_run() returns (via SYS_EXIT) and re-enters ring3 from
 * the saved frame with %rax=0.
 *
 * Returns the child PID (placeholder 2 — no allocator yet) to the parent
 * in %rax via the wrapper's normal return path. dispatch_common() is NOT
 * called for the parent's fork.
 */
static uint64_t arch_x86_64_syscall_fork_capture(
    void *frame, uint8_t via_syscall) {
    early_console64_write("[fork:capture] enter via_syscall=");
    early_console64_write_hex64((uint64_t)via_syscall);
    early_console64_write("\n");
    x86_64_proc_t *parent = arch_x86_64_proc_current();
    if (parent == NULL) {
        early_console64_write(
            "[x86_64][fork] no current proc -- refusing fork.\n");
        return (uint64_t)-1;
    }
    if (parent->child_slot != OPENOS_X86_64_PROC_INVALID_INDEX) {
        /* Another child is already queued/running; γ.2.a serialises. */
        early_console64_write(
            "[x86_64][fork] nested fork attempt -- refusing.\n");
        return (uint64_t)-1;
    }

    /* γ.2.a: give the child its own PCB slot. AS is still shared with the
     * parent (as_clone lands in γ.2.b/γ.3). fork_pending / fork_user_rsp /
     * saved_fork_frame_* now live on the CHILD PCB, so wait() can drive
     * the child by switching current_index to child->slot. */
    x86_64_proc_t *child = arch_x86_64_proc_fork_alloc_child(parent);
    if (child == NULL) {
        early_console64_write(
            "[x86_64][fork] child slot allocation failed -- refusing.\n");
        return (uint64_t)-1;
    }

    child->fork_via_syscall = via_syscall;
    if (via_syscall) {
        /* Byte copy (avoid dragging in libc string.h). */
        const uint8_t *src = (const uint8_t *)frame;
        uint8_t       *dst = (uint8_t *)&child->saved_fork_frame_sysc;
        for (x86_64_size_t i = 0; i < sizeof(x86_64_syscall_frame_t); ++i)
            dst[i] = src[i];
        /* Read user rsp stashed at syscall entry via %gs:syscall_user_rsp. */
        arch_x86_64_percpu_t *pc = arch_x86_64_this_cpu_ptr();
        child->fork_user_rsp = pc->syscall_user_rsp;
    } else {
        const uint8_t *src = (const uint8_t *)frame;
        uint8_t       *dst = (uint8_t *)&child->saved_fork_frame_int80;
        for (x86_64_size_t i = 0; i < sizeof(x86_64_int80_frame_t); ++i)
            dst[i] = src[i];
        child->fork_user_rsp = 0; /* int80 frame carries user rsp natively */
    }
    child->fork_pending = 1;

    /* gamma.3b-S2a Seg-5: allocate a PARKED USER sched_slot for the child
     * with the parent's callee-saved GPRs plumbed into the child's
     * initial iretq frame.
     *
     * Change vs alpha (γ.3b-alpha):
     *   - We call the _full variant so the child's dispatch trampoline
     *     restores rbx/rbp/r12-r15 to the parent's syscall-entry values
     *     before iretq. This is what makes the child, on its first user
     *     instruction, see "the state right after fork() returned 0".
     *   - We bind the slot's owner_proc back-pointer so proc_current()
     *     works on the AP that ends up dispatching it.
     *   - We pin the slot to CPU 1 when SMP is up (target_cpu=1); on UP
     *     we fall back to CPU 0 (== BSP) and the legacy fork_pending
     *     path picks it up as before.
     *
     * Alpha's failure-is-non-fatal contract still holds: on spawn error
     * child_sched_slot stays 0 and the legacy resume path runs. */
    {
        uint64_t rip_u, rsp_u, rfl_u;
        uint64_t init_rbx, init_rbp, init_r12, init_r13, init_r14, init_r15;
        if (via_syscall) {
            rip_u = child->saved_fork_frame_sysc.rcx;
            rfl_u = child->saved_fork_frame_sysc.r11;
            rsp_u = child->fork_user_rsp;
            init_rbx = child->saved_fork_frame_sysc.rbx;
            init_rbp = child->saved_fork_frame_sysc.rbp;
            init_r12 = child->saved_fork_frame_sysc.r12;
            init_r13 = child->saved_fork_frame_sysc.r13;
            init_r14 = child->saved_fork_frame_sysc.r14;
            init_r15 = child->saved_fork_frame_sysc.r15;
        } else {
            rip_u = (uint64_t)child->saved_fork_frame_int80.rip;
            rfl_u = child->saved_fork_frame_int80.rflags;
            rsp_u = (uint64_t)child->saved_fork_frame_int80.rsp;
            init_rbx = child->saved_fork_frame_int80.rbx;
            init_rbp = child->saved_fork_frame_int80.rbp;
            init_r12 = child->saved_fork_frame_int80.r12;
            init_r13 = child->saved_fork_frame_int80.r13;
            init_r14 = child->saved_fork_frame_int80.r14;
            init_r15 = child->saved_fork_frame_int80.r15;
        }
        uint16_t ucs = (uint16_t)(OPENOS_X86_64_GDT_USER_CODE | 3u);
        uint16_t uss = (uint16_t)(OPENOS_X86_64_GDT_USER_DATA | 3u);
        /* Target CPU = 1 when SMP has >= 2 CPUs, else 0. arch_x86_64_smp_
         * cpu_count() is the canonical count; falling back to 0 keeps
         * the legacy fork_pending path working on UP builds and QEMU
         * -smp 1 runs. */
        /* S2a-dbg2: stage 21 selftest leaves a sentinel task on CPU=cpu_count-1
         * with `cli;hlt` (IF=0) which permanently blocks IPIs on that CPU.
         * Route fork child to CPU=cpu_count-2 (typically CPU=2 on -smp 4)
         * which is left idle after stage 30/31. Falls back gracefully:
         *   cpu_count >= 3 -> cpu_count - 2
         *   cpu_count == 2 -> 1
         *   cpu_count <  2 -> 0 (BSP, UP mode) */
        uint32_t _cpu_cnt = arch_x86_64_smp_cpu_count();
        uint32_t target_cpu =
            (_cpu_cnt >= 3u) ? (_cpu_cnt - 2u)
                             : ((_cpu_cnt >= 2u) ? 1u : 0u);
        uint32_t slot_idx = arch_x86_64_sched_spawn_uthread_parked_full(
            rip_u, rsp_u, rfl_u, ucs, uss,
            OPENOS_X86_64_SCHED_PRIO_DEFAULT, target_cpu,
            init_rbx, init_rbp, init_r12, init_r13, init_r14, init_r15);
        if (slot_idx == 0xFFFFFFFFu) {
            early_console64_write(
                "[fork:capture][S2a] parked slot alloc FAILED"
                " (continuing on legacy fork_pending path)\n");
            child->fork_child_sched_slot = 0u;
        } else {
            (void)arch_x86_64_sched_slot_set_as(slot_idx, child->as);
            (void)arch_x86_64_sched_slot_set_owner_proc(slot_idx, child);
            child->fork_child_sched_slot = slot_idx;
            early_console64_write("[fork:capture][S2a] parked slot=");
            early_console64_write_hex64((uint64_t)slot_idx);
            early_console64_write(" rip=");
            early_console64_write_hex64(rip_u);
            early_console64_write(" rsp=");
            early_console64_write_hex64(rsp_u);
            early_console64_write(" target_cpu=");
            early_console64_write_hex64((uint64_t)target_cpu);
            early_console64_write("\n");

            /* Wakeup only when we truly have SMP + AP available.
             * On UP fall back to legacy fork_pending path (kmain will
             * see child->fork_pending and hand-dispatch it). */
            if (target_cpu != 0u) {
                uint32_t wk = arch_x86_64_sched_slot_wakeup(slot_idx);
                early_console64_write("[fork:capture][S2a] wakeup rc=");
                early_console64_write_hex64((uint64_t)wk);
                early_console64_write("\n");
                /* Once the child is READY on the AP, we do NOT need the
                 * legacy fork_pending path for this child. Clear the
                 * flag so kmain's main loop skips the hand-dispatch
                 * fallback. do_wait / do_exit will handle synchronization
                 * (S2a step 6/7). */
                child->fork_pending = 0u;
            }
        }
    }

    early_console64_write("[fork:capture] child slot=");
    early_console64_write_hex64((uint64_t)parent->child_slot);
    early_console64_write(" pid=");
    early_console64_write_hex64((uint64_t)child->pid);
    early_console64_write(" via_syscall=");
    early_console64_write_hex64((uint64_t)(uint32_t)via_syscall);
    early_console64_write("\n");
    if (via_syscall) {
        early_console64_write("[fork:capture] saved rcx(rip)=");
        early_console64_write_hex64(child->saved_fork_frame_sysc.rcx);
        early_console64_write(" r11(rfl)=");
        early_console64_write_hex64(child->saved_fork_frame_sysc.r11);
        early_console64_write(" user_rsp=");
        early_console64_write_hex64(child->fork_user_rsp);
        early_console64_write("\n");
    }

    return child->pid;
}

/*
 * int 0x80 compat path.
 *
 * Register layout matches the i386 ABI (preserved to keep a single user-mode
 * syscall stub working for both architectures during phase 1):
 *     rax = syscall number
 *     rbx = arg0   rcx = arg1   rdx = arg2
 *     rsi = arg3   rdi = arg4   rbp = arg5
 *
 * The low 32 bits of each register carry the real value when ELF32 user
 * binaries are executed; the upper bits are simply zero-extended.
 */
uint64_t arch_x86_64_int80_dispatch(x86_64_int80_frame_t *frame) {
    ++int80_dispatch_count;
    if (frame == NULL) {
        return (uint64_t)-1;
    }

    /* A2.P3-B: short-circuit SYS_FORK before dispatch_common (needs frame). */
    if (frame->rax == SYS_FORK) {
        return arch_x86_64_syscall_fork_capture(frame, /*via_syscall=*/0);
    }

    return arch_x86_64_syscall_dispatch_common(
        frame->rax,
        frame->rbx,
        frame->rcx,
        frame->rdx,
        frame->rsi,
        frame->rdi,
        frame->rbp);
}

/*
 * Native syscall/sysret path.
 *
 * Follows the SysV AMD64 syscall convention:
 *     rax = syscall number
 *     rdi = arg0   rsi = arg1   rdx = arg2
 *     r10 = arg3   r8  = arg4   r9  = arg5
 * (rcx/r11 are clobbered by the syscall instruction itself.)
 *
 * Contract for non-returning syscalls (SYS_EXIT / SYS_EXECVE):
 *   The dispatch backend in syscall_dispatch64.c MUST longjmp back to the
 *   kernel stack via arch_x86_64_usermode_return_to_kernel() before it
 *   reaches this wrapper. If we ever observe such a syscall returning here
 *   it means the backend regressed and the caller is about to sysretq into
 *   a stale ring3 RIP -- which would #GP at CPL=3. We trip a loud assert in
 *   that case rather than silently sysret.
 */
uint64_t arch_x86_64_syscall_dispatch(x86_64_syscall_frame_t *frame) {
    uint64_t result;

    ++syscall_dispatch_count;
    if (frame == NULL) {
        return (uint64_t)-1;
    }

    /* A2.P3-B: short-circuit SYS_FORK before dispatch_common (needs frame). */
    if (frame->rax == SYS_FORK) {
        return arch_x86_64_syscall_fork_capture(frame, /*via_syscall=*/1);
    }

    result = arch_x86_64_syscall_dispatch_common(
        frame->rax,
        frame->rdi,
        frame->rsi,
        frame->rdx,
        frame->r10,
        frame->r8,
        frame->r9);

    /*
     * A2.P0 hardening: SYS_EXIT must never return to this wrapper.
     *
     * do_exit() in syscall_dispatch64.c calls
     * arch_x86_64_usermode_return_to_kernel(), which restores the kernel
     * stack saved by usermode_run() and `ret`s directly back into the
     * cooperative scheduler loop. Hitting the code below means that contract
     * is broken (regression). Log it and halt LOUDLY -- the previous
     * "silent cli; hlt" hid the failure mode and the old comment claiming
     * "usermode_run will observe usermode_exited on its next poll" was wrong
     * for the syscall path because there is no poll: the wrapper would
     * otherwise sysretq into ring3 with a stale RIP and #GP.
     */
    if (frame->rax == SYS_EXIT) {
        early_console64_write(
            "[x86_64][syscall] BUG: SYS_EXIT returned to entry wrapper -- "
            "do_exit() must longjmp via usermode_return_to_kernel().\n");
        for (;;) {
            __asm__ __volatile__("cli; hlt");
        }
    }

    return result;
}

uint32_t arch_x86_64_syscall_current_abi(void) {
    return syscall64_current_abi;
}

uint8_t arch_x86_64_syscall_sysret_enabled(void) {
    return syscall_sysret_enabled;
}

void arch_x86_64_syscall_print_status(void) {
    early_console64_write("[x86_64][syscall] phase1 ABI=int 0x80 compat vector=");
    early_console64_write_hex64(OPENOS_X86_64_INT80_VECTOR);
    early_console64_write(" int80_dispatches=");
    early_console64_write_hex64(int80_dispatch_count);
    early_console64_write(" syscall_sysret=");
    early_console64_write_hex64(syscall_sysret_enabled);
    early_console64_write(" syscall_dispatches=");
    early_console64_write_hex64(syscall_dispatch_count);
    early_console64_write(" common_total=");
    early_console64_write_hex64(arch_x86_64_syscall_dispatch_total());
    early_console64_write(" common_enosys=");
    early_console64_write_hex64(arch_x86_64_syscall_dispatch_enosys());
    early_console64_write("\n");
}
