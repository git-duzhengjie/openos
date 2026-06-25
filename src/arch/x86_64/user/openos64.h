#ifndef OPENOS_ARCH_X86_64_USER_OPENOS64_H
#define OPENOS_ARCH_X86_64_USER_OPENOS64_H

#include <stdint.h>

/* 系统调用号与内核 src/kernel/include/syscall.h 保持一致。
 * x86_64 与 i386 共享同一套 SYS_* ABI，仅入口指令不同（int 0x80 / syscall）。 */
#define OPENOS64_SYS_EXIT    1ULL
#define OPENOS64_SYS_GETPID  20ULL
#define OPENOS64_SYS_GETTID  21ULL
#define OPENOS64_SYS_READ    63ULL
#define OPENOS64_SYS_WRITE   64ULL
#define OPENOS64_SYS_MALLOC  73ULL
#define OPENOS64_SYS_FREE    74ULL
#define OPENOS64_SYS_YIELD   201ULL
#define OPENOS64_SYS_OPEN    225ULL
#define OPENOS64_SYS_CLOSE   226ULL

#define OPENOS64_STDIN_FILENO  0
#define OPENOS64_STDOUT_FILENO 1
#define OPENOS64_STDERR_FILENO 2

typedef uint64_t openos64_size_t;
typedef int64_t openos64_ssize_t;

typedef struct openos64_runtime_info {
    uint64_t abi_version;
    uint64_t syscall_instruction;
    uint64_t crt0_entry;
} openos64_runtime_info_t;

static inline long openos64_syscall0(uint64_t num) {
    uint64_t ret;
    __asm__ __volatile__("syscall"
                         : "=a"(ret)
                         : "a"(num)
                         : "rcx", "r11", "memory");
    return (long)ret;
}

static inline long openos64_syscall1(uint64_t num, uint64_t a0) {
    uint64_t ret;
    __asm__ __volatile__("syscall"
                         : "=a"(ret)
                         : "a"(num), "D"(a0)
                         : "rcx", "r11", "memory");
    return (long)ret;
}

static inline long openos64_syscall2(uint64_t num, uint64_t a0, uint64_t a1) {
    uint64_t ret;
    __asm__ __volatile__("syscall"
                         : "=a"(ret)
                         : "a"(num), "D"(a0), "S"(a1)
                         : "rcx", "r11", "memory");
    return (long)ret;
}

static inline long openos64_syscall3(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2) {
    uint64_t ret;
    __asm__ __volatile__("syscall"
                         : "=a"(ret)
                         : "a"(num), "D"(a0), "S"(a1), "d"(a2)
                         : "rcx", "r11", "memory");
    return (long)ret;
}

static inline long openos64_syscall4(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    uint64_t ret;
    register uint64_t r10 __asm__("r10") = a3;
    __asm__ __volatile__("syscall"
                         : "=a"(ret)
                         : "a"(num), "D"(a0), "S"(a1), "d"(a2), "r"(r10)
                         : "rcx", "r11", "memory");
    return (long)ret;
}

static inline long openos64_syscall5(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    uint64_t ret;
    register uint64_t r10 __asm__("r10") = a3;
    register uint64_t r8 __asm__("r8") = a4;
    __asm__ __volatile__("syscall"
                         : "=a"(ret)
                         : "a"(num), "D"(a0), "S"(a1), "d"(a2), "r"(r10), "r"(r8)
                         : "rcx", "r11", "memory");
    return (long)ret;
}

static inline openos64_ssize_t openos64_write(int fd, const void *buf, openos64_size_t len) {
    return (openos64_ssize_t)openos64_syscall3(OPENOS64_SYS_WRITE, (uint64_t)fd, (uint64_t)(uintptr_t)buf, len);
}

static inline openos64_ssize_t openos64_read(int fd, void *buf, openos64_size_t len) {
    return (openos64_ssize_t)openos64_syscall3(OPENOS64_SYS_READ, (uint64_t)fd, (uint64_t)(uintptr_t)buf, len);
}

static inline int openos64_open(const char *path, int flags, int mode) {
    return (int)openos64_syscall3(OPENOS64_SYS_OPEN, (uint64_t)(uintptr_t)path, (uint64_t)flags, (uint64_t)mode);
}

static inline int openos64_close(int fd) {
    return (int)openos64_syscall1(OPENOS64_SYS_CLOSE, (uint64_t)fd);
}

static inline long openos64_getpid(void) {
    return openos64_syscall0(OPENOS64_SYS_GETPID);
}

static inline void openos64_exit(int code) __attribute__((noreturn));
static inline void openos64_exit(int code) {
    (void)openos64_syscall1(OPENOS64_SYS_EXIT, (uint64_t)(uint32_t)code);
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

static inline void openos64_yield(void) {
    (void)openos64_syscall0(OPENOS64_SYS_YIELD);
}

openos64_size_t openos64_strlen(const char *text);
int openos64_main(int argc, char **argv);
void openos64_start(void) __attribute__((noreturn));
const openos64_runtime_info_t *openos64_runtime_get_info(void);

#endif /* OPENOS_ARCH_X86_64_USER_OPENOS64_H */
