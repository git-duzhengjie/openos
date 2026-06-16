#include "../include/gdt64.h"
#include "../include/tss64.h"

static struct x86_64_tss tss64 __attribute__((aligned(16)));
static uint8_t tss64_rsp0_stack[OPENOS_X86_64_TSS_RSP0_STACK_SIZE] __attribute__((aligned(16)));
static uint8_t tss64_ist_stacks[OPENOS_X86_64_TSS_IST_COUNT][OPENOS_X86_64_TSS_IST_STACK_SIZE] __attribute__((aligned(16)));

static x86_64_stack_ptr_t stack_top_addr(uint8_t *stack_base, uint32_t stack_size) {
    return (x86_64_stack_ptr_t)(uintptr_t)(stack_base + stack_size);
}

void arch_x86_64_tss_init(void) {
    uint32_t i;

    tss64.reserved0 = 0;
    for (i = 0; i < OPENOS_X86_64_TSS_RSP_COUNT; ++i) {
        tss64.rsp[i] = 0;
    }
    tss64.reserved1 = 0;
    for (i = 0; i < OPENOS_X86_64_TSS_IST_COUNT; ++i) {
        tss64.ist[i] = stack_top_addr(tss64_ist_stacks[i], OPENOS_X86_64_TSS_IST_STACK_SIZE);
    }
    tss64.reserved2 = 0;
    tss64.reserved3 = 0;
    tss64.iomap_base = (uint16_t)sizeof(tss64);

    tss64.rsp[0] = stack_top_addr(tss64_rsp0_stack, OPENOS_X86_64_TSS_RSP0_STACK_SIZE);
}

void arch_x86_64_tss_load(void) {
    __asm__ __volatile__("ltr %0" : : "r"((uint16_t)OPENOS_X86_64_GDT_TSS) : "memory");
}

const struct x86_64_tss *arch_x86_64_tss_get(void) {
    return &tss64;
}

x86_64_virt_addr_t arch_x86_64_tss_base(void) {
    return (x86_64_virt_addr_t)(uintptr_t)&tss64;
}

uint32_t arch_x86_64_tss_limit(void) {
    return (uint32_t)(sizeof(tss64) - 1u);
}

x86_64_stack_ptr_t arch_x86_64_tss_rsp0(void) {
    return tss64.rsp[0];
}

x86_64_stack_ptr_t arch_x86_64_tss_ist(uint8_t ist_index) {
    if (ist_index == 0 || ist_index > OPENOS_X86_64_TSS_IST_COUNT) {
        return 0;
    }
    return tss64.ist[ist_index - 1u];
}
