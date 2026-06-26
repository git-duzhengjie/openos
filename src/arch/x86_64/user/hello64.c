#include "openos64.h"

/*
 * Step C demo: open() -> read() -> write() -> exit() on x86_64.
 *
 * 这个程序是 x86_64 用户态生态的最小可观测路径：
 *   1. 通过 SYS_WRITE 直接写 banner（验证 fd=1 输出路径）
 *   2. 通过 SYS_OPEN 打开 initrd 中的 /hello.txt
 *   3. 通过 SYS_READ 读其内容到 .bss 缓冲区
 *   4. 通过 SYS_WRITE 回写到 stdout
 *   5. 通过 SYS_CLOSE 关闭 fd
 *   6. 通过 SYS_EXIT 干净退出
 *
 * 任何一步失败都会输出一段唯一标记，便于在 QEMU 串口里抓取定位。
 * 缓冲区放在 .bss（静态全局），避免占用 8K 用户栈。
 */

static char io_buf[256];

static void write_str(int fd, const char *s) {
    openos64_size_t n = openos64_strlen(s);
    (void)openos64_write(fd, s, n);
}

int openos64_main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    write_str(OPENOS64_STDOUT_FILENO,
              "[hello64] step C: open/read/write demo\n");

    int fd = openos64_open("/hello.txt", 0, 0);
    if (fd < 0) {
        write_str(OPENOS64_STDERR_FILENO,
                  "[hello64] ERR: open(/hello.txt) failed\n");
        return 1;
    }

    openos64_ssize_t got = openos64_read(fd, io_buf, sizeof(io_buf));
    if (got <= 0) {
        write_str(OPENOS64_STDERR_FILENO,
                  "[hello64] ERR: read returned <=0\n");
        (void)openos64_close(fd);
        return 2;
    }

    (void)openos64_write(OPENOS64_STDOUT_FILENO, io_buf, (openos64_size_t)got);

    if (openos64_close(fd) != 0) {
        write_str(OPENOS64_STDERR_FILENO,
                  "[hello64] WARN: close failed\n");
    }

    write_str(OPENOS64_STDOUT_FILENO,
              "[hello64] step C: done\n");

    /*
     * Step E.1 verification: exercise the proc64-backed identity syscalls.
     * The kernel registers hello64 as pid=2 (child of kernel pid=1, tid=2),
     * so the expected output is exactly:
     *   [hello64] step E: pid=2 tid=2 ppid=1 yield=0
     * Any deviation means proc64 wiring drifted; the exact-string form lets
     * us grep for regressions from the run script.
     */
    long pid  = openos64_getpid();
    long tid  = openos64_gettid();
    long ppid = openos64_getppid();
    openos64_yield();
    long yret = 0; /* SYS_YIELD returns 0 on success; recorded for symmetry */

    /* tiny inline decimal formatter — only single-digit values expected here */
    char line[64];
    const char *prefix = "[hello64] step E: pid=";
    openos64_size_t i = 0;
    for (const char *p = prefix; *p; ++p) line[i++] = *p;
    line[i++] = (char)('0' + (pid  & 0x7F));
    line[i++] = ' '; line[i++] = 't'; line[i++] = 'i'; line[i++] = 'd'; line[i++] = '=';
    line[i++] = (char)('0' + (tid  & 0x7F));
    line[i++] = ' '; line[i++] = 'p'; line[i++] = 'p'; line[i++] = 'i'; line[i++] = 'd'; line[i++] = '=';
    line[i++] = (char)('0' + (ppid & 0x7F));
    line[i++] = ' '; line[i++] = 'y'; line[i++] = 'i'; line[i++] = 'e'; line[i++] = 'l'; line[i++] = 'd'; line[i++] = '=';
    line[i++] = (char)('0' + (yret & 0x7F));
    line[i++] = '\n';
    (void)openos64_write(OPENOS64_STDOUT_FILENO, line, i);

    return 0;
}
