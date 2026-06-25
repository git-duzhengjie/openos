#include "aarch64_uart.h"

#define AARCH64_QEMU_VIRT_PL011_BASE 0x09000000UL
#define PL011_DR 0x000u
#define PL011_FR 0x018u
#define PL011_IBRD 0x024u
#define PL011_FBRD 0x028u
#define PL011_LCRH 0x02Cu
#define PL011_CR 0x030u
#define PL011_IMSC 0x038u
#define PL011_ICR 0x044u
#define PL011_FR_TXFF (1u << 5)

static volatile unsigned int *aarch64_uart_reg(unsigned int offset) {
    return (volatile unsigned int *)(AARCH64_QEMU_VIRT_PL011_BASE + offset);
}

void aarch64_uart_init(void) {
    *aarch64_uart_reg(PL011_CR) = 0u;
    *aarch64_uart_reg(PL011_ICR) = 0x7FFu;
    *aarch64_uart_reg(PL011_IBRD) = 1u;
    *aarch64_uart_reg(PL011_FBRD) = 40u;
    *aarch64_uart_reg(PL011_LCRH) = (3u << 5);
    *aarch64_uart_reg(PL011_IMSC) = 0u;
    *aarch64_uart_reg(PL011_CR) = (1u << 0) | (1u << 8) | (1u << 9);
}

void aarch64_uart_putc(char ch) {
    if (ch == '\n') aarch64_uart_putc('\r');
    while ((*aarch64_uart_reg(PL011_FR) & PL011_FR_TXFF) != 0u) {
        __asm__ volatile ("nop");
    }
    *aarch64_uart_reg(PL011_DR) = (unsigned int)ch;
}

void aarch64_uart_write(const char *text) {
    if (!text) return;
    while (*text) {
        aarch64_uart_putc(*text++);
    }
}
