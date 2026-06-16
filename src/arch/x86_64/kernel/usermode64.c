#include "../include/usermode64.h"

#include <stddef.h>

#include "../include/early_console64.h"
#include "../include/gdt64.h"

static x86_64_usermode_info_t usermode64_info;

static int is_low_canonical_user_addr(uint64_t value) {
    return value != 0 && value < OPENOS_X86_64_USER_CANONICAL_TOP;
}

void arch_x86_64_usermode_init(void) {
    usermode64_info.prepared_frames = 0;
    usermode64_info.rejected_frames = 0;
    usermode64_info.last_entry = 0;
    usermode64_info.last_stack = 0;
}

int arch_x86_64_validate_user_iretq_frame(const x86_64_user_iretq_frame_t *frame) {
    if (frame == NULL) {
        return -1;
    }
    if (!is_low_canonical_user_addr(frame->rip) || !is_low_canonical_user_addr(frame->rsp)) {
        return -1;
    }
    if ((frame->cs & 3ULL) != 3ULL || (frame->ss & 3ULL) != 3ULL) {
        return -1;
    }
    if (frame->cs != (uint64_t)arch_x86_64_gdt_user_code_selector() ||
        frame->ss != (uint64_t)arch_x86_64_gdt_user_data_selector()) {
        return -1;
    }
    if ((frame->rflags & OPENOS_X86_64_USER_RFLAGS_RESERVED) == 0) {
        return -1;
    }
    return 0;
}

int arch_x86_64_prepare_user_iretq_frame(x86_64_user_iretq_frame_t *frame,
                                         x86_64_entry_t entry,
                                         x86_64_stack_ptr_t user_stack) {
    if (frame == NULL || !is_low_canonical_user_addr(entry) || !is_low_canonical_user_addr(user_stack)) {
        ++usermode64_info.rejected_frames;
        return -1;
    }

    frame->rip = entry;
    frame->cs = arch_x86_64_gdt_user_code_selector();
    frame->rflags = OPENOS_X86_64_USER_RFLAGS_DEFAULT;
    frame->rsp = user_stack & ~0xFULL;
    frame->ss = arch_x86_64_gdt_user_data_selector();

    if (arch_x86_64_validate_user_iretq_frame(frame) != 0) {
        ++usermode64_info.rejected_frames;
        return -1;
    }

    ++usermode64_info.prepared_frames;
    usermode64_info.last_entry = frame->rip;
    usermode64_info.last_stack = frame->rsp;
    return 0;
}

const x86_64_usermode_info_t *arch_x86_64_usermode_get_info(void) {
    return &usermode64_info;
}

void arch_x86_64_usermode_print_status(void) {
    early_console64_write("[x86_64][usermode] iretq return frame ready prepared=");
    early_console64_write_hex64(usermode64_info.prepared_frames);
    early_console64_write(" rejected=");
    early_console64_write_hex64(usermode64_info.rejected_frames);
    early_console64_write(" last_entry=");
    early_console64_write_hex64(usermode64_info.last_entry);
    early_console64_write(" last_stack=");
    early_console64_write_hex64(usermode64_info.last_stack);
    early_console64_write("\n");
}
