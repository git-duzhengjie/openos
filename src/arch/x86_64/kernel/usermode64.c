#include "../include/usermode64.h"

#include <stddef.h>

#include "../include/early_console64.h"
#include "../include/gdt64.h"

extern void arch_x86_64_iretq_enter_user(const x86_64_user_iretq_frame_t *frame);

#define OPENOS_X86_64_USER_STACK_SIZE 0x4000U

static x86_64_user_iretq_frame_t prepared_user_frame;
static uint8_t usermode_ready;
static uint8_t usermode_running;
static uint8_t usermode_exited;
static int usermode_last_exit_code;
static uint64_t usermode_run_count;
static uint64_t usermode_exit_count;
static uintptr_t usermode_kernel_return_rsp;
static uint8_t bootstrap_user_stack[OPENOS_X86_64_USER_STACK_SIZE] __attribute__((aligned(16)));

void arch_x86_64_usermode_init(void) {
    usermode_ready = 1;
    usermode_running = 0;
    usermode_exited = 0;
    usermode_last_exit_code = 0;
    usermode_run_count = 0;
    usermode_exit_count = 0;
    usermode_kernel_return_rsp = 0;
    prepared_user_frame.rip = 0;
    prepared_user_frame.cs = (uint64_t)(OPENOS_X86_64_GDT_USER_CODE | 3u);
    prepared_user_frame.rflags = 0x202ULL;
    prepared_user_frame.rsp = (uint64_t)(uintptr_t)(bootstrap_user_stack + OPENOS_X86_64_USER_STACK_SIZE);
    prepared_user_frame.ss = (uint64_t)(OPENOS_X86_64_GDT_USER_DATA | 3u);
}

void arch_x86_64_usermode_prepare_iretq(x86_64_user_iretq_frame_t *frame,
                                         x86_64_entry_t entry,
                                         x86_64_virt_addr_t stack_top) {
    if (frame == NULL) {
        return;
    }
    frame->rip = (uint64_t)entry;
    frame->cs = (uint64_t)(OPENOS_X86_64_GDT_USER_CODE | 3u);
    frame->rflags = 0x202ULL;
    frame->rsp = (uint64_t)stack_top;
    frame->ss = (uint64_t)(OPENOS_X86_64_GDT_USER_DATA | 3u);
}

uint8_t arch_x86_64_usermode_validate_frame(const x86_64_user_iretq_frame_t *frame) {
    if (frame == NULL) {
        return 0;
    }
    if (frame->rip == 0 || frame->rsp == 0) {
        return 0;
    }
    if ((frame->cs & 3u) != 3u || (frame->ss & 3u) != 3u) {
        return 0;
    }
    return 1;
}

const x86_64_user_iretq_frame_t *arch_x86_64_usermode_get_prepared_frame(void) {
    return &prepared_user_frame;
}

uint8_t arch_x86_64_usermode_is_running(void) {
    return usermode_running;
}

uint8_t arch_x86_64_usermode_has_exited(void) {
    return usermode_exited;
}

int arch_x86_64_usermode_exit_code(void) {
    return usermode_last_exit_code;
}

int arch_x86_64_usermode_run(x86_64_entry_t entry) {
    if (!usermode_ready || entry == 0) {
        return -1;
    }

    arch_x86_64_usermode_prepare_iretq(&prepared_user_frame,
                                       entry,
                                       (x86_64_virt_addr_t)(uintptr_t)(bootstrap_user_stack + OPENOS_X86_64_USER_STACK_SIZE));
    if (!arch_x86_64_usermode_validate_frame(&prepared_user_frame)) {
        return -2;
    }

    usermode_exited = 0;
    usermode_last_exit_code = 0;
    usermode_running = 1;
    ++usermode_run_count;

    __asm__ __volatile__("movq %%rsp, %0" : "=m"(usermode_kernel_return_rsp) : : "memory");
    arch_x86_64_iretq_enter_user(&prepared_user_frame);

    usermode_running = 0;
    return usermode_exited ? usermode_last_exit_code : -3;
}

void arch_x86_64_usermode_mark_exited(int code) {
    usermode_last_exit_code = code;
    usermode_exited = 1;
    usermode_running = 0;
    ++usermode_exit_count;
}

void arch_x86_64_usermode_return_to_kernel(void) {
    if (usermode_kernel_return_rsp != 0) {
        __asm__ __volatile__("movq %0, %%rsp\n\tret" : : "r"(usermode_kernel_return_rsp) : "memory");
    }
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

void arch_x86_64_usermode_print_status(void) {
    early_console64_write("[x86_64][usermode] ready=");
    early_console64_write_hex64(usermode_ready);
    early_console64_write(" running=");
    early_console64_write_hex64(usermode_running);
    early_console64_write(" exited=");
    early_console64_write_hex64(usermode_exited);
    early_console64_write(" exit_code=");
    early_console64_write_hex64((uint64_t)(uint32_t)usermode_last_exit_code);
    early_console64_write(" runs=");
    early_console64_write_hex64(usermode_run_count);
    early_console64_write(" exits=");
    early_console64_write_hex64(usermode_exit_count);
    early_console64_write(" frame_rip=");
    early_console64_write_hex64(prepared_user_frame.rip);
    early_console64_write(" frame_rsp=");
    early_console64_write_hex64(prepared_user_frame.rsp);
    early_console64_write("\n");
}
