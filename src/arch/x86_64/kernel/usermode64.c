#include "../include/usermode64.h"

#include <stddef.h>

#include "../include/early_console64.h"
#include "../include/gdt64.h"
#include "../include/pmm64.h"
#include "../include/proc64.h"

extern void arch_x86_64_iretq_enter_user(const x86_64_user_iretq_frame_t *frame);

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

static x86_64_user_iretq_frame_t prepared_user_frame;
static uint8_t usermode_ready;
static uint8_t usermode_running;
static uint8_t usermode_exited;
static int usermode_last_exit_code;
static uint64_t usermode_run_count;
static uint64_t usermode_exit_count;
static uintptr_t usermode_kernel_return_rsp;
static x86_64_phys_addr_t bootstrap_user_stack_base;  /* phys == virt (identity) */

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
    usermode_kernel_return_rsp = 0;
    prepared_user_frame.rip = 0;
    prepared_user_frame.cs = (uint64_t)(OPENOS_X86_64_GDT_USER_CODE | 3u);
    prepared_user_frame.rflags = 0x202ULL;
    prepared_user_frame.rsp = (uint64_t)bootstrap_user_stack_top();
    prepared_user_frame.ss = (uint64_t)(OPENOS_X86_64_GDT_USER_DATA | 3u);
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
    return &prepared_user_frame;
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

    arch_x86_64_usermode_prepare_iretq(&prepared_user_frame,
                                       entry,
                                       (x86_64_virt_addr_t)bootstrap_user_stack_top());
    if (!arch_x86_64_usermode_validate_frame(&prepared_user_frame)) {
        return -2;
    }

    usermode_exited = 0;
    usermode_last_exit_code = 0;
    usermode_running = 1;
    ++usermode_run_count;

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
        : "=m"(usermode_kernel_return_rsp),
          "=m"(exited_local),
          "=m"(code_local)
        : "r"(&prepared_user_frame)
        : "rax", "rcx", "rdx", "rsi", "rdi",
          "r8", "r9", "r10", "r11",
          "memory", "cc"
    );
    (void)exited_local; (void)code_local;

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

void arch_x86_64_usermode_return_to_kernel(void) {
    if (usermode_kernel_return_rsp != 0) {
        __asm__ __volatile__("movq %0, %%rsp\n\tret" : : "r"(usermode_kernel_return_rsp) : "memory");
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
    early_console64_write_hex64(prepared_user_frame.rip);
    early_console64_write(" frame_rsp=");
    early_console64_write_hex64(prepared_user_frame.rsp);
    early_console64_write("\n");
}
