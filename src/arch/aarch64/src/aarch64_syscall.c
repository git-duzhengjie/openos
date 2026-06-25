#include "aarch64_syscall.h"

#include <stddef.h>

#include "aarch64_uart.h"

static uint64_t syscall_dispatch_count;
static uint32_t syscall_last_number;
static uint32_t syscall_last_exit_code;
static uint8_t syscall_exit_requested;

static uint64_t syscall_write(uint64_t fd, const char *buf, uint64_t len) {
    uint64_t i;

    if (fd != 1u && fd != 2u) {
        return (uint64_t)-1;
    }
    if (buf == NULL) {
        return (uint64_t)-1;
    }

    for (i = 0; i < len; ++i) {
        char ch = buf[i];
        if (ch == '\0') {
            break;
        }
        aarch64_uart_putc(ch);
    }
    return i;
}

void aarch64_syscall_init(void) {
    syscall_dispatch_count = 0;
    syscall_last_number = 0;
    syscall_last_exit_code = 0;
    syscall_exit_requested = 0;
}

uint64_t aarch64_syscall_dispatch(aarch64_trap_frame_t *frame) {
    uint64_t syscall_no;

    if (frame == NULL) {
        return (uint64_t)-1;
    }

    syscall_no = frame->x[8];
    ++syscall_dispatch_count;
    syscall_last_number = (uint32_t)syscall_no;

    switch (syscall_no) {
    case OPENOS_AARCH64_SYS_WRITE:
        return syscall_write(frame->x[0], (const char *)(uintptr_t)frame->x[1], frame->x[2]);
    case OPENOS_AARCH64_SYS_GETPID:
        return 1u;
    case OPENOS_AARCH64_SYS_YIELD:
        return 0u;
    case OPENOS_AARCH64_SYS_EXIT:
        syscall_last_exit_code = (uint32_t)frame->x[0];
        syscall_exit_requested = 1u;
        return 0u;
    default:
        return (uint64_t)-1;
    }
}

uint64_t aarch64_syscall_count(void) {
    return syscall_dispatch_count;
}

uint32_t aarch64_syscall_last_number(void) {
    return syscall_last_number;
}

uint32_t aarch64_syscall_last_exit_code(void) {
    return syscall_last_exit_code;
}

uint8_t aarch64_syscall_exit_requested(void) {
    return syscall_exit_requested;
}

void aarch64_syscall_print_status(void) {
    aarch64_uart_write("[aarch64][syscall] svc entry ready count=0x");
    /* Keep this file freestanding/minimal: print only the low nibble for now. */
    aarch64_uart_putc("0123456789abcdef"[syscall_dispatch_count & 0xfu]);
    aarch64_uart_write(" last=0x");
    aarch64_uart_putc("0123456789abcdef"[syscall_last_number & 0xfu]);
    aarch64_uart_write(" exit=0x");
    aarch64_uart_putc("0123456789abcdef"[syscall_last_exit_code & 0xfu]);
    aarch64_uart_write("\n");
}
