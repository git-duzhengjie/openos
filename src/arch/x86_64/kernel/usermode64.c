#include "../include/usermode64.h"

#include <stddef.h>

#include "../include/address_space64.h"
#include "../include/early_console64.h"
#include "../include/gdt64.h"
#include "../include/idt64.h"
#include "../include/pmm64.h"
#include "../include/proc64.h"

extern void arch_x86_64_iretq_enter_user(const x86_64_user_iretq_frame_t *frame);

/*
 * Step G.x: ring3-drop sentry state.
 *
 * usermode_canary is set to:
 *   0 before arch_x86_64_usermode_run() ever runs,
 *   1 right after the kernel stashes its longjmp context ("entered"),
 *   2 right after the inline-asm return path falls through `1:` and pops
 *     the callee-saved frame cleanly ("return path executed end-to-end").
 *
 * If a regression of commit 0b14358 reintroduces the misaligned saved-rsp
 * bug, the `ret` from arch_x86_64_usermode_return_to_kernel() lands at
 * something other than label 1 — the canary will stay at 1 (or change in
 * an unexpected way), and the selftest below panics.
 *
 * Paired with arch_x86_64_idt_kernel_fault_count(): even if the canary
 * somehow gets to 2 by accident, a #UD/#GP/#PF on the way there bumps the
 * kfault counter and the selftest still catches it.
 */
static volatile uint64_t usermode_kfault_before = 0;
static volatile uint64_t usermode_kfault_after = 0;

/*
 * Step D.2: the bootstrap user stack must live in memory that ring3 can
 * actually access through the active page tables.  Putting it in kernel .bss
 * lands it in the higher-half mapping, whose boot-time PD entries are
 * kernel-only -- ring3 takes a #PF on the very first 'and rsp / call' pair
 * even though the user code page itself is mapped U=1.
 *
 * Allocate a low-memory page from PMM and use it as the bootstrap stack.
 * PMM returns identity-mapped 4 KiB pages within the early identity range
 * (0..4 GiB in our boot tables), which is U-readable/writable.
 */
#define OPENOS_X86_64_USER_STACK_PAGES 4U
#define OPENOS_X86_64_USER_STACK_SIZE (OPENOS_X86_64_USER_STACK_PAGES * 0x1000U)

static uint8_t usermode_ready;
static uint8_t usermode_running;
static uint8_t usermode_exited;
static int usermode_last_exit_code;
static uint64_t usermode_run_count;
static uint64_t usermode_exit_count;

/*
 * A2.P1: the three pieces of per-thread state that the cooperative
 * longjmp core used to keep as file-scope statics now live in the PCB.
 * The macros below expand to lvalue references on the current thread's
 * PCB so the rest of this file can keep reading/writing them with
 * one-line expressions and one-operand inline-asm constraints. After
 * arch_x86_64_proc_init() runs (slot 0 = kernel PCB), the accessor
 * never returns NULL, and the cooperative scheduler only ever calls
 * into usermode64.c from kernel context, so the indirection is safe.
 */
#define PREPARED_USER_FRAME (arch_x86_64_proc_current()->saved_user_frame)
#define USERMODE_RETURN_RSP (arch_x86_64_proc_current()->kernel_return_rsp)
#define USERMODE_CANARY     (arch_x86_64_proc_current()->usermode_canary)
static x86_64_phys_addr_t bootstrap_user_stack_base;  /* phys == virt (identity) */

/*
 * H.3 execve trampoline state.
 *
 * On SYS_EXEC the dispatcher loads the new ELF, then calls
 * arch_x86_64_usermode_mark_exec(new_entry). That sets exited=1 +
 * pending_exec=1 + pending_exec_entry=new_entry and longjmps back to
 * the saved kernel context (same path SYS_EXIT uses). The outer kernel
 * driver inspects has_pending_exec() after usermode_run() returns and,
 * if set, calls usermode_run(take_pending_exec_entry()) to reenter ring3
 * on the freshly loaded image. The pid stays the same -- mark_exec does
 * NOT call arch_x86_64_proc_exit().
 */
static volatile uint8_t usermode_pending_exec;
static volatile x86_64_entry_t usermode_pending_exec_entry;
static volatile uint64_t usermode_exec_count;
static volatile uint64_t usermode_exec_fail_count;

static uintptr_t bootstrap_user_stack_top(void) {
    if (bootstrap_user_stack_base == 0) {
        x86_64_phys_addr_t p = 0;
        for (unsigned i = 0; i < OPENOS_X86_64_USER_STACK_PAGES; ++i) {
            x86_64_phys_addr_t one = arch_x86_64_pmm_alloc_page();
            if (one == 0) {
                early_console64_write("[x86_64][usermode] PMM out of pages for user stack\n");
                return 0;
            }
            if (i == 0) {
                p = one;
            }
            /* PMM is bump-allocator; pages come back in order, so contiguous. */
        }
        bootstrap_user_stack_base = p;
    }
    return (uintptr_t)(bootstrap_user_stack_base + OPENOS_X86_64_USER_STACK_SIZE);
}

void arch_x86_64_usermode_init(void) {
    usermode_ready = 1;
    usermode_running = 0;
    usermode_exited = 0;
    usermode_last_exit_code = 0;
    usermode_run_count = 0;
    usermode_exit_count = 0;
    USERMODE_RETURN_RSP = 0;
    usermode_pending_exec = 0;
    usermode_pending_exec_entry = 0;
    usermode_exec_count = 0;
    usermode_exec_fail_count = 0;
    PREPARED_USER_FRAME.rip = 0;
    PREPARED_USER_FRAME.cs = (uint64_t)(OPENOS_X86_64_GDT_USER_CODE | 3u);
    PREPARED_USER_FRAME.rflags = 0x202ULL;
    PREPARED_USER_FRAME.rsp = (uint64_t)bootstrap_user_stack_top();
    PREPARED_USER_FRAME.ss = (uint64_t)(OPENOS_X86_64_GDT_USER_DATA | 3u);
}

void arch_x86_64_usermode_prepare_iretq(x86_64_user_iretq_frame_t *frame,
                                         x86_64_entry_t entry,
                                         x86_64_virt_addr_t stack_top) {
    if (frame == NULL) {
        return;
    }
    frame->rip = (uint64_t)entry;
    frame->cs = (uint64_t)(OPENOS_X86_64_GDT_USER_CODE | 3u);
    frame->rflags = 0x202ULL;
    frame->rsp = (uint64_t)stack_top;
    frame->ss = (uint64_t)(OPENOS_X86_64_GDT_USER_DATA | 3u);
}

uint8_t arch_x86_64_usermode_validate_frame(const x86_64_user_iretq_frame_t *frame) {
    if (frame == NULL) {
        return 0;
    }
    if (frame->rip == 0 || frame->rsp == 0) {
        return 0;
    }
    if ((frame->cs & 3u) != 3u || (frame->ss & 3u) != 3u) {
        return 0;
    }
    return 1;
}

const x86_64_user_iretq_frame_t *arch_x86_64_usermode_get_prepared_frame(void) {
    return &PREPARED_USER_FRAME;
}

uint8_t arch_x86_64_usermode_is_running(void) {
    return usermode_running;
}

uint8_t arch_x86_64_usermode_has_exited(void) {
    return usermode_exited;
}

int arch_x86_64_usermode_exit_code(void) {
    return usermode_last_exit_code;
}

int arch_x86_64_usermode_run(x86_64_entry_t entry) {
    if (!usermode_ready || entry == 0) {
        return -1;
    }

    /*
     * H.4: seed the user stack with the SysV program-startup frame so
     * that ring3 _start can `(rsp)` -> argc, `8(rsp)` -> argv. Any args
     * queued by arch_x86_64_usermode_set_args() are consumed here; when
     * none are queued seed_user_stack still lays out the minimal
     * argc=0/argv={NULL} frame to satisfy the ABI.
     */
    x86_64_virt_addr_t user_rsp =
        arch_x86_64_usermode_seed_user_stack((x86_64_virt_addr_t)bootstrap_user_stack_top());
    if (user_rsp == 0) {
        return -3;
    }

    /*
     * H.5b.2 step B: ring3 now executes from PML4[1] (USER_VBASE). The
     * bootstrap stack is still allocated in low identity-mapped memory
     * (so seed_user_stack can write argv/envp via the low alias), but the
     * VA handed to ring3 must live in the high half so that after we flip
     * CR3 to the per-PCB AS the stack is still reachable.
     *
     * Map the 4 stack pages into the current task's AS at
     * va = USER_VBASE + phys, then rewrite user_rsp to its high-half
     * alias.
     */
    {
        x86_64_address_space_t *as = arch_x86_64_proc_current_get_as();
        if (as != NULL) {
            uintptr_t stk_base_phys = (uintptr_t)bootstrap_user_stack_base;
            for (uintptr_t off = 0; off < OPENOS_X86_64_USER_STACK_SIZE;
                 off += OPENOS_X86_64_VMM_PAGE_SIZE) {
                x86_64_virt_addr_t hi =
                    (x86_64_virt_addr_t)(OPENOS_X86_64_USER_VBASE + stk_base_phys + off);
                x86_64_phys_addr_t ph =
                    (x86_64_phys_addr_t)(stk_base_phys + off);
                (void)arch_x86_64_as_map_user(as, hi, ph,
                    OPENOS_X86_64_VMM_PAGE_SIZE,
                    OPENOS_X86_64_AS_FLAG_RW);
            }
            user_rsp = (x86_64_virt_addr_t)(OPENOS_X86_64_USER_VBASE + user_rsp);
        }
    }

    arch_x86_64_usermode_prepare_iretq(&PREPARED_USER_FRAME,
                                       entry,
                                       user_rsp);
    if (!arch_x86_64_usermode_validate_frame(&PREPARED_USER_FRAME)) {
        return -2;
    }

    usermode_exited = 0;
    usermode_last_exit_code = 0;
    usermode_running = 1;
    ++usermode_run_count;

    /* Step G.x: take a snapshot of the ring0 fault counters before the
     * iretq drop. The kernel between here and the matching post-iretq
     * point below must observe zero ring0 exceptions — SYS_EXIT goes
     * through the syscall path, not the IDT, so the sentry's "total"
     * counter must not advance for a healthy run. */
    usermode_kfault_before = arch_x86_64_idt_kernel_fault_count();
    USERMODE_CANARY = 1;

    /*
     * Step D.3: save a real longjmp-style return context.
     *
     * Earlier we did `movq %rsp, saved; ...; iretq_enter_user(...)` and
     * relied on the syscall path to `mov saved, rsp; ret` back. But that
     * `saved` rsp pointed at the *current* frame's locals (the inline-asm
     * was emitted in the middle of the function), so the `ret` popped
     * a stack slot that was never a return address -- ring3 looked like
     * it kept running and #GP'd on the post-syscall `hlt`.
     *
     * Fix: stash rbx/rbp/r12-r15 + a real RIP label ("1:") on the stack,
     * record that exact rsp, then enter ring3. When ring3 SYS_EXIT calls
     * arch_x86_64_usermode_return_to_kernel(), we restore rsp and `ret`
     * straight to label 1, which falls through to the function epilogue.
     */
    int exited_local = 0;
    int code_local = 0;
    /*
     * H.5b.2 step B: flip CR3 onto the per-PCB address space right before
     * the iretq drop. PML4[0] is the shared boot identity mirror, so kernel
     * text/data/stack/IDT/GDT remain reachable both before and after the
     * load. On SYS_EXIT we restore CR3 to the boot PML4 so the rest of
     * the kernel code path returns to a known table.
     */
    {
        x86_64_address_space_t *as = arch_x86_64_proc_current_get_as();
        if (as != NULL) {
            arch_x86_64_as_activate(as);
        }
    }
    /*
     * Step D.4: stack layout (low -> high) right before iretq_enter_user:
     *   [rsp+ 0] = label-1 RIP   <-- saved_rsp points here
     *   [rsp+ 8] = r15
     *   [rsp+16] = r14
     *   [rsp+24] = r13
     *   [rsp+32] = r12
     *   [rsp+40] = rbx
     *   [rsp+48] = rbp
     * arch_x86_64_usermode_return_to_kernel does `mov saved,%rsp; ret`,
     * which pops the RIP slot and lands at label 1. Then we pop the six
     * callee-saved registers in reverse-push order and fall through to
     * the function epilogue. Previously saved_rsp pointed at the r15 slot
     * so `ret` popped r15's value as RIP -> #UD.
     */
    __asm__ __volatile__ (
        "pushq %%rbp\n\t"
        "pushq %%rbx\n\t"
        "pushq %%r12\n\t"
        "pushq %%r13\n\t"
        "pushq %%r14\n\t"
        "pushq %%r15\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"             /* RIP slot on top -- ret target */
        "movq %%rsp, %0\n\t"          /* publish kernel return rsp */
        "movq %3, %%rdi\n\t"
        "call arch_x86_64_iretq_enter_user\n\t"
        /* Should never fall through here -- iretq goes to ring3. */
        "ud2\n\t"
        "1:\n\t"                       /* return target from SYS_EXIT */
        "popq %%r15\n\t"
        "popq %%r14\n\t"
        "popq %%r13\n\t"
        "popq %%r12\n\t"
        "popq %%rbx\n\t"
        "popq %%rbp\n\t"
        : "=m"(USERMODE_RETURN_RSP),
          "=m"(exited_local),
          "=m"(code_local)
        : "r"(&PREPARED_USER_FRAME)
        : "rax", "rcx", "rdx", "rsi", "rdi",
          "r8", "r9", "r10", "r11",
          "memory", "cc"
    );
    (void)exited_local; (void)code_local;

    /*
     * H.5b.2 step B: ring3 has returned (via SYS_EXIT trampoline). Drop
     * back onto the boot PML4 so subsequent kernel paths -- selftest,
     * exec respawn, idle, smp -- see the same CR3 they entered with.
     */
    arch_x86_64_as_activate_boot();

    /* Step G.x: end of the ring3 round-trip. Mark canary=2 and sample the
     * kfault counter again. The exported helpers below let the selftest
     * verify (after_total - before_total) == 0. */
    USERMODE_CANARY = 2;
    usermode_kfault_after = arch_x86_64_idt_kernel_fault_count();

    usermode_running = 0;
    return usermode_exited ? usermode_last_exit_code : -3;
}

void arch_x86_64_usermode_mark_exited(int code) {
    usermode_last_exit_code = code;
    usermode_exited = 1;
    usermode_running = 0;
    ++usermode_exit_count;
    /* Step E.1: tear down the ring3 PCB so SYS_GETPID after exit reports
     * the kernel proc again. Safe even if the user program never spawned
     * (proc_exit is a no-op on the kernel slot). */
    arch_x86_64_proc_exit(code);
}

void arch_x86_64_usermode_mark_exec(x86_64_entry_t entry) {
    /*
     * H.3 execve: stash the new entry, mark pending_exec, and reuse the
     * SYS_EXIT trampoline back to the kernel context inside usermode_run().
     * Crucially we do NOT call arch_x86_64_proc_exit() -- the process
     * lives on with the same pid, just running a different image.
     */
    usermode_pending_exec_entry = entry;
    usermode_pending_exec = 1;
    usermode_last_exit_code = 0;
    usermode_exited = 1;
    usermode_running = 0;
    ++usermode_exec_count;
}

uint8_t arch_x86_64_usermode_has_pending_exec(void) {
    return usermode_pending_exec;
}

x86_64_entry_t arch_x86_64_usermode_take_pending_exec(void) {
    x86_64_entry_t e = usermode_pending_exec_entry;
    usermode_pending_exec = 0;
    usermode_pending_exec_entry = 0;
    return e;
}

uint64_t arch_x86_64_usermode_exec_count(void) {
    return usermode_exec_count;
}

uint64_t arch_x86_64_usermode_exec_fail_count(void) {
    return usermode_exec_fail_count;
}

void arch_x86_64_usermode_note_exec_fail(void) {
    ++usermode_exec_fail_count;
}

void arch_x86_64_usermode_return_to_kernel(void) {
    if (USERMODE_RETURN_RSP != 0) {
        __asm__ __volatile__("movq %0, %%rsp\n\tret" : : "r"(USERMODE_RETURN_RSP) : "memory");
    }
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

void arch_x86_64_usermode_print_status(void) {
    early_console64_write("[x86_64][usermode] ready=");
    early_console64_write_hex64(usermode_ready);
    early_console64_write(" running=");
    early_console64_write_hex64(usermode_running);
    early_console64_write(" exited=");
    early_console64_write_hex64(usermode_exited);
    early_console64_write(" exit_code=");
    early_console64_write_hex64((uint64_t)(uint32_t)usermode_last_exit_code);
    early_console64_write(" runs=");
    early_console64_write_hex64(usermode_run_count);
    early_console64_write(" exits=");
    early_console64_write_hex64(usermode_exit_count);
    early_console64_write(" frame_rip=");
    early_console64_write_hex64(PREPARED_USER_FRAME.rip);
    early_console64_write(" frame_rsp=");
    early_console64_write_hex64(PREPARED_USER_FRAME.rsp);
    early_console64_write(" canary=");
    early_console64_write_hex64(USERMODE_CANARY);
    early_console64_write(" kfault_delta=");
    early_console64_write_hex64(usermode_kfault_after - usermode_kfault_before);
    early_console64_write(" exec_count=");
    early_console64_write_hex64(usermode_exec_count);
    early_console64_write(" exec_fail=");
    early_console64_write_hex64(usermode_exec_fail_count);
    early_console64_write(" pending_exec=");
    early_console64_write_hex64(usermode_pending_exec);
    early_console64_write("\n");
}

uint64_t arch_x86_64_usermode_canary(void) {
    return USERMODE_CANARY;
}

uint64_t arch_x86_64_usermode_kfault_delta(void) {
    return usermode_kfault_after - usermode_kfault_before;
}

/* ===================================================================
 * H.4: argv plumbing.
 * ===================================================================
 *
 * The kernel keeps an N-slot scratch table of argv strings owned by the
 * kernel data segment. The lifetime of these strings is independent of
 * the ring3 image, which is critical because:
 *   1. SYS_EXEC may receive an argv pointer that lives inside the
 *      current ring3 image's .rodata at the very VA elf64_load_image()
 *      is about to overwrite (H.3 path-lifetime hazard).
 *   2. The newly loaded ring3 image has no idea what arguments were
 *      passed; the kernel must inject them by writing onto the user
 *      stack of the *new* program before iretq.
 *
 * The table is single-threaded by construction: the kernel is BSP-only
 * during initial spawn (H.3 baseline) and SYS_EXEC runs under the
 * current task's syscall stack with interrupts disabled by the trap
 * gate. No locking is needed at this stage; when SMP scheduling lands
 * the table will become per-task in the PCB.
 */

static char     usermode_arg_storage[X86_64_USER_ARGV_MAX][X86_64_USER_ARG_MAX];
static int      usermode_arg_count = 0;

/* H.5a: envp snapshot table, same lifetime rationale as argv. */
static char     usermode_env_storage[X86_64_USER_ENVP_MAX][X86_64_USER_ENV_MAX];
static int      usermode_env_count = 0;

static x86_64_size_t k_strlen_u(const char *s) {
    x86_64_size_t n = 0;
    if (!s) return 0;
    while (s[n] != '\0') ++n;
    return n;
}

void arch_x86_64_usermode_clear_args(void) {
    usermode_arg_count = 0;
    for (unsigned i = 0; i < X86_64_USER_ARGV_MAX; ++i) {
        usermode_arg_storage[i][0] = '\0';
    }
}

int arch_x86_64_usermode_pending_argc(void) {
    return usermode_arg_count;
}

void arch_x86_64_usermode_set_args(int argc, const char *const *argv) {
    arch_x86_64_usermode_clear_args();
    if (argc <= 0 || argv == NULL) {
        return;
    }
    int cap = (int)X86_64_USER_ARGV_MAX;
    int n = argc < cap ? argc : cap;
    for (int i = 0; i < n; ++i) {
        const char *src = argv[i];
        if (src == NULL) {
            usermode_arg_storage[i][0] = '\0';
            continue;
        }
        x86_64_size_t len = k_strlen_u(src);
        if (len >= X86_64_USER_ARG_MAX) {
            len = X86_64_USER_ARG_MAX - 1;
        }
        for (x86_64_size_t j = 0; j < len; ++j) {
            usermode_arg_storage[i][j] = src[j];
        }
        usermode_arg_storage[i][len] = '\0';
    }
    usermode_arg_count = n;
}

/*
 * H.5a envp snapshot helpers, mirror of set_args/clear_args/pending_argc.
 *
 * Same single-threaded-by-construction rationale: today only the BSP
 * touches this on the spawn path or while executing SYS_EXEC under a
 * trap gate. When SMP scheduling lands these tables will migrate into
 * the PCB along with argv.
 */
void arch_x86_64_usermode_clear_envs(void) {
    usermode_env_count = 0;
    for (unsigned i = 0; i < X86_64_USER_ENVP_MAX; ++i) {
        usermode_env_storage[i][0] = '\0';
    }
}

int arch_x86_64_usermode_pending_envc(void) {
    return usermode_env_count;
}

void arch_x86_64_usermode_set_envs(int envc, const char *const *envp) {
    arch_x86_64_usermode_clear_envs();
    if (envc <= 0 || envp == NULL) {
        return;
    }
    int cap = (int)X86_64_USER_ENVP_MAX;
    int n = envc < cap ? envc : cap;
    for (int i = 0; i < n; ++i) {
        const char *src = envp[i];
        if (src == NULL) {
            usermode_env_storage[i][0] = '\0';
            continue;
        }
        x86_64_size_t len = k_strlen_u(src);
        if (len >= X86_64_USER_ENV_MAX) {
            len = X86_64_USER_ENV_MAX - 1;
        }
        for (x86_64_size_t j = 0; j < len; ++j) {
            usermode_env_storage[i][j] = src[j];
        }
        usermode_env_storage[i][len] = '\0';
    }
    usermode_env_count = n;
}

/*
 * Compose the SysV program-startup frame at the top of the user stack.
 *
 * Stack image (high address at top):
 *   ...untouched user-stack reserve...
 *   stack_top_in -> +---------------------+
 *                   | argv strings (NUL)  |  copied top-down, packed
 *                   +---------------------+
 *                   | 8-byte zero pad     |  (alignment scratch)
 *                   +---------------------+
 *                   | argv[N] = NULL      |
 *                   | argv[N-1]           |
 *                   | ...                 |
 *                   | argv[0]             |
 *                   +---------------------+
 *                   | argc (64-bit)       |  <- returned RSP, % 16 == 0
 *                   +---------------------+
 *
 * We round the final RSP down to a 16-byte boundary so that crt0.S's
 * `andq $-16, %rsp` is a no-op and `call openos64_start` lands inside
 * the C ABI's usual rsp % 16 == 8 contract.
 */
x86_64_virt_addr_t arch_x86_64_usermode_seed_user_stack(x86_64_virt_addr_t stack_top_in) {
    if (stack_top_in == 0) {
        return 0;
    }

    int argc = usermode_arg_count;
    if (argc < 0) argc = 0;
    if (argc > (int)X86_64_USER_ARGV_MAX) argc = (int)X86_64_USER_ARGV_MAX;

    int envc = usermode_env_count;
    if (envc < 0) envc = 0;
    if (envc > (int)X86_64_USER_ENVP_MAX) envc = (int)X86_64_USER_ENVP_MAX;

    /* 1) Copy env strings to the very top of the user stack first, then
     *    argv strings below them. Both grow downward. */
    uint8_t *sp = (uint8_t *)(uintptr_t)stack_top_in;

    uintptr_t env_va[X86_64_USER_ENVP_MAX];
    for (int i = envc - 1; i >= 0; --i) {
        const char *s = usermode_env_storage[i];
        x86_64_size_t len = k_strlen_u(s) + 1; /* incl. NUL */
        sp -= len;
        for (x86_64_size_t j = 0; j < len; ++j) {
            sp[j] = (uint8_t)s[j];
        }
        env_va[i] = (uintptr_t)sp;
    }

    uintptr_t arg_va[X86_64_USER_ARGV_MAX];
    for (int i = argc - 1; i >= 0; --i) {
        const char *s = usermode_arg_storage[i];
        x86_64_size_t len = k_strlen_u(s) + 1; /* incl. NUL */
        sp -= len;
        for (x86_64_size_t j = 0; j < len; ++j) {
            sp[j] = (uint8_t)s[j];
        }
        arg_va[i] = (uintptr_t)sp;
    }

    /* 2) 16-byte alignment scratch so the final RSP lands on a 16-byte
     *    boundary regardless of how many slots / bytes we pushed.
     *
     *    H.5a math: after the base align we push, top-down:
     *      envp NULL terminator   : 1 slot
     *      envp[envc-1 .. 0]      : envc slots
     *      argv NULL terminator   : 1 slot
     *      argv[argc-1 .. 0]      : argc slots
     *      argc                   : 1 slot
     *    Total = 8 * (argc + envc + 3) bytes. For 16-alignment we need
     *    (argc + envc + 3) to be even, i.e. (argc + envc) odd. When the
     *    sum is even we prepend an 8-byte zero pad here (lives above
     *    the envp NULL terminator, doesn't affect any pointer slot). */
    uintptr_t cursor = (uintptr_t)sp;
    cursor &= ~((uintptr_t)15);
    if (((argc + envc) & 1) == 0) {
        cursor -= 8;
        *(uint64_t *)cursor = 0;
    }

    /* 3) envp[envc] = NULL terminator, then envp[envc-1..0]. */
    cursor -= 8;
    *(uint64_t *)cursor = 0;
    for (int i = envc - 1; i >= 0; --i) {
        cursor -= 8;
        *(uint64_t *)cursor = (uint64_t)env_va[i];
    }
    uintptr_t envp_base = cursor; /* envp[0] (or terminator when envc==0) */

    /* 4) argv[argc] = NULL terminator, then argv[argc-1..0]. */
    cursor -= 8;
    *(uint64_t *)cursor = 0;
    for (int i = argc - 1; i >= 0; --i) {
        cursor -= 8;
        *(uint64_t *)cursor = (uint64_t)arg_va[i];
    }
    uintptr_t argv_base = cursor; /* argv[0] */

    /* 5) push argc; (rsp) reads it in crt0.S. Final SysV layout for crt0:
     *      0(rsp)                       = argc
     *      8(rsp)                       = argv[0]
     *      8 + (argc+1)*8 (rsp)         = envp[0]
     */
    cursor -= 8;
    *(uint64_t *)cursor = (uint64_t)(uint32_t)argc;

    /* sanity: argv_base == cursor + 8, envp_base == argv_base + (argc+1)*8. */
    (void)argv_base;
    (void)envp_base;

    /* 6) final alignment check: SysV mandates rsp % 16 == 0 *at* _start. */
    if ((cursor & 15ULL) != 0ULL) {
        /* Should never happen given the design but bail gracefully. */
        return 0;
    }

    early_console64_write("[x86_64][usermode] seed_user_stack argc=");
    early_console64_write_hex64((uint64_t)(uint32_t)argc);
    early_console64_write(" envc=");
    early_console64_write_hex64((uint64_t)(uint32_t)envc);
    early_console64_write(" rsp=");
    early_console64_write_hex64((uint64_t)cursor);
    early_console64_write("\n");

    /* H.5a: arg+env tables are one-shot per usermode_run/exec round. Clear
     * them here so a subsequent run/exec that forgets to set them sees a
     * clean argc=0 envc=0 frame. */
    arch_x86_64_usermode_clear_args();
    arch_x86_64_usermode_clear_envs();

    return (x86_64_virt_addr_t)cursor;
}
