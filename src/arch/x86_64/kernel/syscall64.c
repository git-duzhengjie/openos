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

void arch_x86_64_syscall_init(void) {
    syscall64_current_abi = OPENOS_X86_64_SYSCALL_ABI_INT80_COMPAT;
    int80_dispatch_count = 0;
    syscall_dispatch_count = 0;
    syscall_sysret_enabled = 0;
    arch_x86_64_syscall_dispatch_reset();
    configure_syscall_sysret();
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
 * For SYS_EXIT we must also repair rcx/r11 so sysret returns to the
 * usermode trampoline that flips the thread to "exited" cleanly. This
 * frame-level fixup stays here because it is an entry-path concern.
 */
uint64_t arch_x86_64_syscall_dispatch(x86_64_syscall_frame_t *frame) {
    uint64_t result;

    ++syscall_dispatch_count;
    if (frame == NULL) {
        return (uint64_t)-1;
    }

    result = arch_x86_64_syscall_dispatch_common(
        frame->rax,
        frame->rdi,
        frame->rsi,
        frame->rdx,
        frame->r10,
        frame->r8,
        frame->r9);

    /* Post-dispatch fixup for SYS_EXIT taken via syscall instruction:
     * spin in kernel after dispatch instead of sysretting -- the previous
     * approach pointed RCX at a kernel-address trampoline which ring3 cannot
     * fetch. Since SYS_EXIT semantically does not return to userspace, we
     * just halt here; arch_x86_64_usermode_run will observe usermode_exited
     * from the dispatch backend on its next poll. */
    if (frame->rax == SYS_EXIT) {
        early_console64_write("[x86_64][usermode] SYS_EXIT taken via syscall; halting\n");
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
