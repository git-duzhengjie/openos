/*
 * openos - exit status regression helper
 */

#define SYS_EXIT 1

static inline void syscall1_no_return(int num, int a)
{
    __asm__ volatile("int $0x80" : : "a"(num), "b"(a));
    for (;;) { }
}

void _start(void)
{
    syscall1_no_return(SYS_EXIT, 42);
}
