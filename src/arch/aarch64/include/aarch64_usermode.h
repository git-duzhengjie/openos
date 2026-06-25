#ifndef OPENOS_ARCH_AARCH64_USERMODE_H
#define OPENOS_ARCH_AARCH64_USERMODE_H

#include <stdint.h>

#include "aarch64_elf64.h"

#define AARCH64_USER_STACK_SIZE (64UL * 1024UL)

typedef struct aarch64_user_process {
    uintptr_t entry;
    uintptr_t stack_bottom;
    uintptr_t stack_top;
    uint32_t pid;
    uint32_t reserved;
    uint32_t exited;
    uint32_t exit_code;
} aarch64_user_process_t;

void aarch64_usermode_init(void);
int aarch64_user_process_create_from_elf(const aarch64_elf64_image_t *image, aarch64_user_process_t *process);
void aarch64_user_enter_el0(uintptr_t entry, uintptr_t stack_top);
int aarch64_run_embedded_hello64(void);
const aarch64_user_process_t *aarch64_usermode_last_process(void);

#endif /* OPENOS_ARCH_AARCH64_USERMODE_H */
