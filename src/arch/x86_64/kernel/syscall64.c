#include "../include/syscall64.h"

#include <stddef.h>

#include "../include/early_console64.h"
#include "../include/gdt64.h"

extern void x86_64_syscall_entry(void);

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
    configure_syscall_sysret();
}

uint64_t arch_x86_64_int80_dispatch(x86_64_int80_frame_t *frame) {
    uint64_t syscall_no;

    ++int80_dispatch_count;
    if (frame == NULL) {
        return (uint64_t)-1;
    }

    syscall_no = frame->rax;

    /*
     * Phase 1 keeps the OpenOS user ABI on the i386 int 0x80 contract.
     * The x86_64 port exposes vector 0x80 as a compatibility trap gate so
     * future ELF64 user mode can share syscall numbers while syscall/sysret
     * is implemented separately.
     */
    switch (syscall_no) {
    case 20u: /* SYS_GETPID */
        return 0;
    default:
        return (uint64_t)-1;
    }
}

uint64_t arch_x86_64_syscall_dispatch(x86_64_syscall_frame_t *frame) {
    uint64_t syscall_no;

    ++syscall_dispatch_count;
    if (frame == NULL) {
        return (uint64_t)-1;
    }

    syscall_no = frame->rax;
    switch (syscall_no) {
    case 20u: /* SYS_GETPID */
        return 0;
    default:
        return (uint64_t)-1;
    }
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
    early_console64_write("\n");
}
