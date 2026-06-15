/*
 * openos - orphan reparent regression helper
 *
 * Spawns a child and exits without waiting. The kernel should reparent
 * the child to init/reaper and later reap it automatically.
 */

#define SYS_EXIT 1
#define SYS_SPAWN 4

static inline int syscall1(int num, int a)
{
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(a));
    return ret;
}

static inline void syscall1_no_return(int num, int a)
{
    __asm__ volatile("int $0x80" : : "a"(num), "b"(a));
    for (;;) { }
}

void _start(void)
{
    int child = syscall1(SYS_SPAWN, (int)"/bin/exit42");
    if (child < 0) {
        syscall1_no_return(SYS_EXIT, 100);
    }

    syscall1_no_return(SYS_EXIT, 7);
}
