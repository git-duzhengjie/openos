#include <stdint.h>

#define OPENOS_AARCH64_SYS_EXIT  1u
#define OPENOS_AARCH64_SYS_WRITE 4u

static inline long openos_syscall3(uint64_t no, uint64_t a0, uint64_t a1, uint64_t a2)
{
    register uint64_t x0 __asm__("x0") = a0;
    register uint64_t x1 __asm__("x1") = a1;
    register uint64_t x2 __asm__("x2") = a2;
    register uint64_t x8 __asm__("x8") = no;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory");
    return (long)x0;
}

void _start(void)
{
    static const char message[] = "hello64 from OpenOS aarch64 EL0\n";
    (void)openos_syscall3(OPENOS_AARCH64_SYS_WRITE, 1u, (uint64_t)(uintptr_t)message, sizeof(message) - 1u);
    (void)openos_syscall3(OPENOS_AARCH64_SYS_EXIT, 0u, 0u, 0u);
    for (;;) {
        __asm__ volatile("wfe");
    }
}
