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
    return 0;
}
