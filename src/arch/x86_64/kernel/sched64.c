#include "../include/sched64.h"

#include <stddef.h>

#include "../include/early_console64.h"

static x86_64_context_t bootstrap_context;
static x86_64_context_t *current_context = &bootstrap_context;
static uint8_t sched64_ready;

static void sched64_thread_trampoline(void) {
    x86_64_thread_entry_t entry;
    void *arg;

    __asm__ __volatile__(
        "mov %%r12, %0\n"
        "mov %%r13, %1\n"
        : "=r"(entry), "=r"(arg)
        :
        : "memory");

    if (entry != NULL) {
        entry(arg);
    }

    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

void arch_x86_64_sched_init(void) {
    bootstrap_context.rflags = OPENOS_X86_64_CONTEXT_RFLAGS_IF;
    current_context = &bootstrap_context;
    sched64_ready = 1u;
}

void arch_x86_64_context_init(x86_64_thread_context_t *ctx,
                              x86_64_thread_entry_t entry,
                              void *arg,
                              x86_64_stack_ptr_t stack_top) {
    if (ctx == NULL) {
        return;
    }

    ctx->regs.rsp = stack_top & ~0xFULL;
    ctx->regs.rip = (x86_64_entry_t)(uintptr_t)sched64_thread_trampoline;
    ctx->regs.rflags = OPENOS_X86_64_CONTEXT_RFLAGS_IF;
    ctx->regs.rbx = 0;
    ctx->regs.rbp = 0;
    ctx->regs.r12 = (uint64_t)(uintptr_t)entry;
    ctx->regs.r13 = (uint64_t)(uintptr_t)arg;
    ctx->regs.r14 = 0;
    ctx->regs.r15 = 0;
    ctx->regs.r8 = 0;
    ctx->regs.r9 = 0;
    ctx->regs.r10 = 0;
    ctx->regs.r11 = 0;
}

const x86_64_context_t *arch_x86_64_current_context(void) {
    return current_context;
}

void arch_x86_64_sched_note_switch(x86_64_context_t *next) {
    if (next != NULL) {
        current_context = next;
    }
}

void arch_x86_64_sched_print_status(void) {
    early_console64_write("[x86_64][sched] context switch supports rsp/rip/rflags and r8-r15\n");
    early_console64_write("[x86_64][sched] ready=");
    early_console64_write_hex64(sched64_ready);
    early_console64_write(" current=");
    early_console64_write_hex64((uint64_t)(uintptr_t)current_context);
    early_console64_write("\n");
}
