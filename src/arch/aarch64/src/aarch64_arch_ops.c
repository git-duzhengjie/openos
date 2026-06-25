#include "arch_ops.h"
#include "aarch64_arch_ops.h"

static void aarch64_noop(void) {
}

static void aarch64_enable_interrupts(void) {
    __asm__ volatile ("msr daifclr, #2" ::: "memory");
}

static void aarch64_disable_interrupts(void) {
    __asm__ volatile ("msr daifset, #2" ::: "memory");
}

static void aarch64_halt(void) {
    __asm__ volatile ("wfi");
}

static void aarch64_context_switch(void *from_context, void *to_context) {
    (void)from_context;
    (void)to_context;
}

static uint64_t aarch64_read_cycle_counter(void) {
    uint64_t value;
    __asm__ volatile ("mrs %0, cntvct_el0" : "=r"(value));
    return value;
}

static const OpenOSArchOps k_aarch64_arch_ops = {
    .name = "aarch64",
    .early_init = aarch64_noop,
    .interrupt_init = aarch64_noop,
    .enable_interrupts = aarch64_enable_interrupts,
    .disable_interrupts = aarch64_disable_interrupts,
    .halt = aarch64_halt,
    .context_switch = aarch64_context_switch,
    .read_cycle_counter = aarch64_read_cycle_counter,
};

void openos_aarch64_arch_ops_init(void) {
    openos_arch_ops_register(&k_aarch64_arch_ops);
}
