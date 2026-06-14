/* openos - minimal ring3 ELF smoke test */

#define SYS_EXIT   1
#define SYS_GETPID 20
#define SYS_WRITE  64

static inline int syscall3(int num, int a, int b, int c) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a), "c"(b), "d"(c)
        : "memory"
    );
    return ret;
}

static inline void syscall1_no_return(int num, int a) {
    __asm__ volatile (
        "int $0x80"
        :
        : "a"(num), "b"(a)
        : "memory"
    );
    for (;;) {
        __asm__ volatile ("pause");
    }
}

static unsigned int cstrlen(const char *s) {
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}

void _start(void) {
    const char *msg = "Hello from ring3 ELF via int 0x80!\n";
    syscall3(SYS_WRITE, 1, (int)msg, (int)cstrlen(msg));

    int pid = syscall3(SYS_GETPID, 0, 0, 0);
    (void)pid;

    syscall1_no_return(SYS_EXIT, 0);
}
