#ifndef OPENOS_ARCH_X86_64_USERMODE64_H
#define OPENOS_ARCH_X86_64_USERMODE64_H

#include <stdint.h>

#include "arch64_types.h"

#define OPENOS_X86_64_USER_RFLAGS_RESERVED 0x2ULL
#define OPENOS_X86_64_USER_RFLAGS_IF       (1ULL << 9)
#define OPENOS_X86_64_USER_RFLAGS_DEFAULT  (OPENOS_X86_64_USER_RFLAGS_RESERVED | OPENOS_X86_64_USER_RFLAGS_IF)
#define OPENOS_X86_64_USER_CANONICAL_TOP   0x0000800000000000ULL

typedef struct x86_64_user_iretq_frame {
    x86_64_entry_t rip;
    uint64_t cs;
    uint64_t rflags;
    x86_64_stack_ptr_t rsp;
    uint64_t ss;
} x86_64_user_iretq_frame_t;

typedef struct x86_64_usermode_info {
    uint64_t prepared_frames;
    uint64_t rejected_frames;
    x86_64_entry_t last_entry;
    x86_64_stack_ptr_t last_stack;
} x86_64_usermode_info_t;

void arch_x86_64_usermode_init(void);
int arch_x86_64_prepare_user_iretq_frame(x86_64_user_iretq_frame_t *frame,
                                         x86_64_entry_t entry,
                                         x86_64_stack_ptr_t user_stack);
int arch_x86_64_validate_user_iretq_frame(const x86_64_user_iretq_frame_t *frame);
void arch_x86_64_iretq_enter_user(const x86_64_user_iretq_frame_t *frame) __attribute__((noreturn));
const x86_64_usermode_info_t *arch_x86_64_usermode_get_info(void);
void arch_x86_64_usermode_print_status(void);

#endif /* OPENOS_ARCH_X86_64_USERMODE64_H */
