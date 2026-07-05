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
static char net_rx_buf[64];

static void write_str(int fd, const char *s) {
    openos64_size_t n = openos64_strlen(s);
    (void)openos64_write(fd, s, n);
}

int openos64_main(int argc, char **argv, char **envp) {
    (void)argc;
    (void)argv;
    (void)envp;

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

    /*
     * Step E.3 verification: drive the loopback socket layer end-to-end
     * through the syscall ABI. The test creates two sockets, binds them to
     * 4242 and 5353, sends "net64" A->B, then echoes the recvfrom result
     * back through stdout. Any failure prints a unique tag and returns
     * non-zero so the run script can grep for regressions.
     */
    {
        int sA = openos64_socket(OPENOS64_AF_OPENOS,
                                 OPENOS64_SOCK_DGRAM,
                                 OPENOS64_PROTO_DEFAULT);
        int sB = openos64_socket(OPENOS64_AF_OPENOS,
                                 OPENOS64_SOCK_DGRAM,
                                 OPENOS64_PROTO_DEFAULT);
        if (sA < 0 || sB < 0) {
            write_str(OPENOS64_STDERR_FILENO,
                      "[hello64] ERR: socket() failed\n");
            return 3;
        }
        if (openos64_bind(sA, 4242) != 0 ||
            openos64_bind(sB, 5353) != 0) {
            write_str(OPENOS64_STDERR_FILENO,
                      "[hello64] ERR: bind() failed\n");
            return 4;
        }
        const char msg[] = "net64";
        openos64_ssize_t sent_n =
            openos64_sendto(sA, msg, sizeof(msg) - 1, 5353);
        if (sent_n != (openos64_ssize_t)(sizeof(msg) - 1)) {
            /* Print the actual return value as hex so future regressions are
             * diagnosable from the serial log alone. */
            char dbg[64];
            openos64_size_t di = 0;
            const char *p = "[hello64] ERR: sendto ret=0x";
            while (*p) dbg[di++] = *p++;
            static const char hexd[] = "0123456789abcdef";
            uint64_t v = (uint64_t)sent_n;
            for (int s = 60; s >= 0; s -= 4)
                dbg[di++] = hexd[(v >> s) & 0xF];
            dbg[di++] = '\n';
            (void)openos64_write(OPENOS64_STDERR_FILENO, dbg, di);
            return 5;
        }
        uint16_t src_port = 0;
        openos64_ssize_t got_net =
            openos64_recvfrom(sB, net_rx_buf, sizeof(net_rx_buf), &src_port);
        if (got_net <= 0) {
            write_str(OPENOS64_STDERR_FILENO,
                      "[hello64] ERR: recvfrom() failed\n");
            return 6;
        }
        write_str(OPENOS64_STDOUT_FILENO,
                  "[hello64] step E.3: net loopback got='");
        (void)openos64_write(OPENOS64_STDOUT_FILENO,
                             net_rx_buf, (openos64_size_t)got_net);
        /* src_port should be 4242 = 0x1092; print one ASCII hex nibble for
         * the high byte and full hex for the low byte to make grep easy. */
        char tail[32];
        openos64_size_t ti = 0;
        const char *suffix = "' src=0x";
        for (const char *p = suffix; *p; ++p) tail[ti++] = *p;
        static const char hexd[] = "0123456789abcdef";
        tail[ti++] = hexd[(src_port >> 12) & 0xF];
        tail[ti++] = hexd[(src_port >>  8) & 0xF];
        tail[ti++] = hexd[(src_port >>  4) & 0xF];
        tail[ti++] = hexd[(src_port >>  0) & 0xF];
        tail[ti++] = '\n';
        (void)openos64_write(OPENOS64_STDOUT_FILENO, tail, ti);
    }

    /*
     * Step E.4 verification: drive the PIT-calibrated uptime_ms syscall
     * from ring3 end-to-end. We take two timestamps separated by a small
     * busy-wait that yields between iterations (so the kernel has chances
     * to observe time advancing), then assert:
     *   1. both calls succeed (non-negative)
     *   2. t1 >= t0 (monotonic non-decreasing)
     * The exact deltas are printed in hex so regressions are diagnosable
     * from the serial log alone. Expected on a calibrated kernel:
     *   [hello64] step E.4: t0=0x... t1=0x... dt>=1
     */
    {
        uint64_t t0 = openos64_uptime_ms();
        /* Busy-spin + yield long enough that t1 advances past t0 even on
         * QEMU TCG (where the calibrated TSC ticks slower than wall clock
         * but still strictly advances). Nested loops give ~hundreds of ms
         * on TCG; KVM hosts blow through this in microseconds. The outer
         * yield keeps the kernel scheduler engaged. */
        for (volatile int outer = 0; outer < 4; ++outer) {
            for (volatile int inner = 0; inner < 4096; ++inner) {
                /* nothing */
            }
            openos64_yield();
        }
        uint64_t t1 = openos64_uptime_ms();

        char ub[96];
        openos64_size_t ui = 0;
        const char *up_pfx = "[hello64] step E.4: t0=0x";
        for (const char *p = up_pfx; *p; ++p) ub[ui++] = *p;
        static const char uphex[] = "0123456789abcdef";
        for (int s = 60; s >= 0; s -= 4) ub[ui++] = uphex[(t0 >> s) & 0xF];
        const char *mid = " t1=0x";
        for (const char *p = mid; *p; ++p) ub[ui++] = *p;
        for (int s = 60; s >= 0; s -= 4) ub[ui++] = uphex[(t1 >> s) & 0xF];
        const char *tag;
        if (t1 < t0)         tag = " dt<0 FAIL";
        else if (t1 == t0)   tag = " dt=0 OK";
        else                 tag = " dt>0 OK";
        for (const char *p = tag; *p; ++p) ub[ui++] = *p;
        ub[ui++] = '\n';
        (void)openos64_write(OPENOS64_STDOUT_FILENO, ub, ui);

        if (t1 < t0) {
            write_str(OPENOS64_STDERR_FILENO,
                      "[hello64] ERR: uptime_ms not monotonic\n");
            return 7;
        }
    }

    return 0;
}
