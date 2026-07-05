#ifndef OPENOS_ARCH_X86_64_SHELL64_H
#define OPENOS_ARCH_X86_64_SHELL64_H

void arch_x86_64_shell_init(void);
void arch_x86_64_shell_run_init(void);
void arch_x86_64_shell_print_status(void);

/* Execute a single shell command line (NUL-terminated). Public wrapper
 * around the internal line dispatcher; used by boot-time selftests and
 * future interactive front-ends. */
void arch_x86_64_shell_exec_line(const char *line);

#endif /* OPENOS_ARCH_X86_64_SHELL64_H */
