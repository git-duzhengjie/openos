/* openos - 简单用户态测试程序 */
/* 编译: gcc -m32 -ffreestanding -nostdlib -fno-pie -fno-pic -O2 -o hello.elf hello.c */

void _start(void) {
    /* sys_write(1, msg, len) - SYS_WRITE=64 */
    const char *msg = "Hello from user program!\n";
    int len = 25;
    __asm__ volatile (
        "movl $64, %%eax\n\t"   /* SYS_WRITE */
        "movl $1, %%ebx\n\t"    /* fd = 1 (stdout) */
        "movl %0, %%ecx\n\t"    /* buf */
        "movl %1, %%edx\n\t"    /* len */
        "int $0x80\n\t"
        :: "r"(msg), "r"(len)
        : "eax", "ebx", "ecx", "edx"
    );

    /* sys_exit(0) - SYS_EXIT=1 */
    __asm__ volatile (
        "movl $1, %%eax\n\t"    /* SYS_EXIT */
        "movl $0, %%ebx\n\t"
        "int $0x80\n\t"
        ::: "eax", "ebx"
    );
}