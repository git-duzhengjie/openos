#ifndef OPENOS_ARCH_X86_64_TSS64_H
#define OPENOS_ARCH_X86_64_TSS64_H

#include <stdint.h>

#include "arch64_types.h"

#define OPENOS_X86_64_TSS_IST_COUNT 7u
#define OPENOS_X86_64_TSS_RSP_COUNT 3u
#define OPENOS_X86_64_TSS_RSP0_STACK_SIZE 16384u
#define OPENOS_X86_64_TSS_IST_STACK_SIZE 8192u

struct x86_64_tss {
    uint32_t reserved0;
    x86_64_stack_ptr_t rsp[OPENOS_X86_64_TSS_RSP_COUNT];
    uint64_t reserved1;
    x86_64_stack_ptr_t ist[OPENOS_X86_64_TSS_IST_COUNT];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

void arch_x86_64_tss_init(void);
void arch_x86_64_tss_load(void);

const struct x86_64_tss *arch_x86_64_tss_get(void);
x86_64_virt_addr_t arch_x86_64_tss_base(void);
uint32_t arch_x86_64_tss_limit(void);
x86_64_stack_ptr_t arch_x86_64_tss_rsp0(void);
x86_64_stack_ptr_t arch_x86_64_tss_ist(uint8_t ist_index);

#endif /* OPENOS_ARCH_X86_64_TSS64_H */
