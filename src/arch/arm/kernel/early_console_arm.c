#include "../include/early_console_arm.h"
#include "../include/arm_platform.h"

static volatile unsigned int *arm_uart0_dr;

static void arm_mmio_write32(volatile unsigned int *addr, unsigned int value) {
    *addr = value;
}

void early_console_arm_init(void) {
    arm_uart0_dr = (volatile unsigned int *)OPENOS_ARM_UART0_BASE;
}

void early_console_arm_putc(char c) {
    if (arm_uart0_dr == 0) {
        early_console_arm_init();
    }
    if (c == '\n') {
        arm_mmio_write32(arm_uart0_dr, (unsigned int)'\r');
    }
    arm_mmio_write32(arm_uart0_dr, (unsigned int)c);
}

void early_console_arm_write(const char *text) {
    if (!text) {
        return;
    }
    while (*text) {
        early_console_arm_putc(*text++);
    }
}

void early_console_arm_write_hex32(unsigned int value) {
    static const char hex[] = "0123456789ABCDEF";
    int i;
    early_console_arm_write("0x");
    for (i = 7; i >= 0; --i) {
        early_console_arm_putc(hex[(value >> (i * 4)) & 0xFu]);
    }
}
