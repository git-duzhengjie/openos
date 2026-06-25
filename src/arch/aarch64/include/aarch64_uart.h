#ifndef OPENOS_ARCH_AARCH64_UART_H
#define OPENOS_ARCH_AARCH64_UART_H

void aarch64_uart_init(void);
void aarch64_uart_putc(char ch);
void aarch64_uart_write(const char *text);

#endif /* OPENOS_ARCH_AARCH64_UART_H */
