#ifndef OPENOS_ARCH_X86_64_IDT64_H
#define OPENOS_ARCH_X86_64_IDT64_H

#include <stdint.h>

#include "arch64_types.h"

#define OPENOS_X86_64_IDT_ENTRY_COUNT 256u
#define OPENOS_X86_64_EXCEPTION_COUNT 32u
#define OPENOS_X86_64_IDT_INTERRUPT_GATE 0x8Eu
#define OPENOS_X86_64_IDT_TRAP_GATE 0x8Fu

struct x86_64_exception_frame {
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
    uint64_t vector;
    uint64_t error_code;
    x86_64_entry_t rip;
    uint64_t cs;
    uint64_t rflags;
    x86_64_stack_ptr_t rsp;
    uint64_t ss;
} __attribute__((packed));

void arch_x86_64_idt_init(void);
void arch_x86_64_exception_dispatch(const struct x86_64_exception_frame *frame);

#endif /* OPENOS_ARCH_X86_64_IDT64_H */
