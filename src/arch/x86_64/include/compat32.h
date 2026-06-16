#ifndef OPENOS_ARCH_X86_64_COMPAT32_H
#define OPENOS_ARCH_X86_64_COMPAT32_H

#include <stdint.h>

#define OPENOS_X86_64_COMPAT32_STATUS_READY       0x00000001ULL
#define OPENOS_X86_64_COMPAT32_STATUS_GDT         0x00000002ULL
#define OPENOS_X86_64_COMPAT32_STATUS_INT80       0x00000004ULL
#define OPENOS_X86_64_COMPAT32_STATUS_ELF32_TODO  0x00000008ULL
#define OPENOS_X86_64_COMPAT32_STATUS_STACK_TODO  0x00000010ULL
#define OPENOS_X86_64_COMPAT32_STATUS_PTR_TODO    0x00000020ULL

typedef struct x86_64_compat32_info {
    uint64_t status_flags;
    uint16_t user32_code_selector;
    uint16_t user32_data_selector;
    uint8_t int80_entry_available;
    uint8_t elf32_loader_required;
    uint8_t user_stack_required;
    uint8_t pointer_thunk_required;
} x86_64_compat32_info_t;

void arch_x86_64_compat32_init(void);
const x86_64_compat32_info_t *arch_x86_64_compat32_get_info(void);
int arch_x86_64_compat32_is_ready(void);
void arch_x86_64_compat32_print_status(void);

#endif /* OPENOS_ARCH_X86_64_COMPAT32_H */
