#include "../include/early_console_riscv.h"
#include "../include/riscv_platform.h"

static volatile uint8_t *riscv_uart0_thr;

static void riscv_mmio_write8(volatile uint8_t *addr, uint8_t value) {
    *addr = value;
}

void early_console_riscv_init(void) {
    riscv_uart0_thr = (volatile uint8_t *)(uintptr_t)OPENOS_RISCV_UART0_BASE;
}

void early_console_riscv_putc(char c) {
    if (riscv_uart0_thr == 0) {
        early_console_riscv_init();
    }
    if (c == '\n') {
        riscv_mmio_write8(riscv_uart0_thr, (uint8_t)'\r');
    }
    riscv_mmio_write8(riscv_uart0_thr, (uint8_t)c);
}

void early_console_riscv_write(const char *text) {
    if (!text) {
        return;
    }
    while (*text) {
        early_console_riscv_putc(*text++);
    }
}

void early_console_riscv_write_hex64(uint64_t value) {
    static const char hex[] = "0123456789ABCDEF";
    int i;

    early_console_riscv_write("0x");
    for (i = 15; i >= 0; --i) {
        early_console_riscv_putc(hex[(value >> ((uint64_t)i * 4ull)) & 0xFull]);
    }
}
