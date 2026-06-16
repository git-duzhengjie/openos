#ifndef OPENOS_ARCH_ARM_EARLY_CONSOLE_ARM_H
#define OPENOS_ARCH_ARM_EARLY_CONSOLE_ARM_H

void early_console_arm_init(void);
void early_console_arm_putc(char c);
void early_console_arm_write(const char *text);
void early_console_arm_write_hex32(unsigned int value);

#endif /* OPENOS_ARCH_ARM_EARLY_CONSOLE_ARM_H */
