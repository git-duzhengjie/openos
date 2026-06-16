#ifndef OPENOS_ARCH_RISCV_EARLY_CONSOLE_RISCV_H
#define OPENOS_ARCH_RISCV_EARLY_CONSOLE_RISCV_H

#include <stdint.h>

void early_console_riscv_init(void);
void early_console_riscv_putc(char c);
void early_console_riscv_write(const char *text);
void early_console_riscv_write_hex64(uint64_t value);

#endif /* OPENOS_ARCH_RISCV_EARLY_CONSOLE_RISCV_H */
