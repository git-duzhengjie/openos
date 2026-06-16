#ifndef OPENOS_ARCH_X86_64_SYSCALL64_H
#define OPENOS_ARCH_X86_64_SYSCALL64_H

#include <stdint.h>

#include "arch64_types.h"

#define OPENOS_X86_64_INT80_VECTOR 0x80u
#define OPENOS_X86_64_SYSCALL_ABI_INT80_COMPAT 1u
#define OPENOS_X86_64_SYSCALL_ABI_SYSCALL_SYSRET 2u

#define OPENOS_X86_64_MSR_EFER  0xC0000080u
#define OPENOS_X86_64_MSR_STAR  0xC0000081u
#define OPENOS_X86_64_MSR_LSTAR 0xC0000082u
#define OPENOS_X86_64_MSR_FMASK 0xC0000084u
#define OPENOS_X86_64_EFER_SCE  0x1u
#define OPENOS_X86_64_SYSCALL_FMASK 0x00000200u

typedef struct x86_64_int80_frame {
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
    x86_64_entry_t rip;
    uint64_t cs;
    uint64_t rflags;
    x86_64_stack_ptr_t rsp;
    uint64_t ss;
} x86_64_int80_frame_t;

typedef struct x86_64_syscall_frame {
    uint64_t r11;
    uint64_t rcx;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rax;
} x86_64_syscall_frame_t;

void arch_x86_64_syscall_init(void);
uint64_t arch_x86_64_int80_dispatch(x86_64_int80_frame_t *frame);
uint64_t arch_x86_64_syscall_dispatch(x86_64_syscall_frame_t *frame);
uint32_t arch_x86_64_syscall_current_abi(void);
uint8_t arch_x86_64_syscall_sysret_enabled(void);
void arch_x86_64_syscall_print_status(void);

#endif /* OPENOS_ARCH_X86_64_SYSCALL64_H */
