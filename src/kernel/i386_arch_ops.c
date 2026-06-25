#include "arch_ops.h"
#include "i386_arch_ops.h"
#include "gdt.h"
#include "idt.h"

static void i386_early_init(void) {
    gdt_init();
}

static void i386_interrupt_init(void) {
    idt_init();
}

static void i386_enable_interrupts(void) {
    __asm__ volatile ("sti");
}

static void i386_disable_interrupts(void) {
    __asm__ volatile ("cli");
}

static void i386_halt(void) {
    __asm__ volatile ("hlt");
}

static void i386_context_switch(void *from_context, void *to_context) {
    (void)from_context;
    (void)to_context;
}

static uint64_t i386_read_cycle_counter(void) {
    uint32_t lo;
    uint32_t hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static const OpenOSArchOps k_i386_arch_ops = {
    .name = "i386",
    .early_init = i386_early_init,
    .interrupt_init = i386_interrupt_init,
    .enable_interrupts = i386_enable_interrupts,
    .disable_interrupts = i386_disable_interrupts,
    .halt = i386_halt,
    .context_switch = i386_context_switch,
    .read_cycle_counter = i386_read_cycle_counter,
};

void openos_i386_arch_ops_init(void) {
    openos_arch_ops_register(&k_i386_arch_ops);
}
