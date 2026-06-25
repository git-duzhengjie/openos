#ifndef OPENOS_ARCH_X86_64_GDT64_H
#define OPENOS_ARCH_X86_64_GDT64_H

#include <stdint.h>

#define OPENOS_X86_64_GDT_NULL        0x00
#define OPENOS_X86_64_GDT_KERNEL_CODE 0x08
#define OPENOS_X86_64_GDT_KERNEL_DATA 0x10
#define OPENOS_X86_64_GDT_USER_DATA   0x18
#define OPENOS_X86_64_GDT_USER_CODE   0x20
#define OPENOS_X86_64_GDT_USER32_CODE 0x28
#define OPENOS_X86_64_GDT_TSS         0x30

void arch_x86_64_gdt_init(void);
void arch_x86_64_gdt_print_status(void);
uint16_t arch_x86_64_gdt_kernel_code_selector(void);
uint16_t arch_x86_64_gdt_kernel_data_selector(void);
uint16_t arch_x86_64_gdt_user_code_selector(void);
uint16_t arch_x86_64_gdt_user32_code_selector(void);
uint16_t arch_x86_64_gdt_user_data_selector(void);
uint16_t arch_x86_64_gdt_tss_selector(void);

#endif /* OPENOS_ARCH_X86_64_GDT64_H */
