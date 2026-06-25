#include "arch_ops.h"
#include "x86_64_arch_ops.h"
#include "gdt64.h"
#include "idt64.h"

static void x86_64_early_init(void) {
    arch_x86_64_gdt_init();
}

static void x86_64_interrupt_init(void) {
    arch_x86_64_idt_init();
}

static void x86_64_enable_interrupts(void) {
    __asm__ volatile ("sti");
}

static void x86_64_disable_interrupts(void) {
    __asm__ volatile ("cli");
}

static void x86_64_halt(void) {
    __asm__ volatile ("hlt");
}

static void x86_64_context_switch(void *from_context, void *to_context) {
    (void)from_context;
    (void)to_context;
}

static uint64_t x86_64_read_cycle_counter(void) {
    uint32_t lo;
    uint32_t hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static const OpenOSArchOps k_x86_64_arch_ops = {
    .name = "x86_64",
    .early_init = x86_64_early_init,
    .interrupt_init = x86_64_interrupt_init,
    .enable_interrupts = x86_64_enable_interrupts,
    .disable_interrupts = x86_64_disable_interrupts,
    .halt = x86_64_halt,
    .context_switch = x86_64_context_switch,
    .read_cycle_counter = x86_64_read_cycle_counter,
};

void openos_x86_64_arch_ops_init(void) {
    openos_arch_ops_register(&k_x86_64_arch_ops);
}
