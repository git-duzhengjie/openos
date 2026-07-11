/*
 * x86_64 syscall 自测：在内核态直接打 dispatch_common，验证 Step C 串起来的
 *   open -> read -> write -> close
 * 链路。即便 OVMF/UEFI 启动还没跑通，也能在 early_console（serial+VGA）里
 * 看到端到端的可观测结果。
 *
 * 设计原则：
 *   1. 只用 dispatch_common，不绕过任何中间层（fdtable64/vfs64/initrd64 全部经过）
 *   2. 错误返回唯一标记（步骤号），便于失败回归定位
 *   3. 缓冲区使用静态 .bss，避免占用调用方栈
 */

#include "../include/syscall_selftest64.h"

#include <stddef.h>
#include <stdint.h>

#include "../include/early_console64.h"
#include "../include/syscall_dispatch64.h"
#include "syscall.h" /* canonical SYS_* */
#include "../include/pipe64.h" /* M4.3a: pipe poll/waitq unit checks */
#include "../include/fifo64.h" /* M4.3b: named pipe (FIFO) unit checks */
#include "../include/shm64.h" /* M4.3c: shared memory unit checks */
#include "../include/tty64.h" /* M4.4a: TTY line discipline unit checks */
#include "../include/proc64.h" /* M4.2b: user-mode signal trampoline checks */
#include "../include/signal64.h" /* M4.2b: sigregs / sigcontext ABI */
#include "../../../kernel/core/fs/vfs.h" /* M4.1a: O_CREAT / O_WRONLY */

static char selftest_buf[256];
static const char selftest_target_path[] = "/hello.txt";

/* M4.2b: identity-mapped user-memory callbacks for the trampoline round-trip.
 * The selftest's scratch stack is a plain kernel buffer, so read/write are
 * direct memcpy -- mirroring the syscall glue's identity-mapped user access. */
static int syscall64_uwrite_stub(void *uctx, uint64_t dst, const void *src,
                                 uint64_t n) {
    (void)uctx;
    if (dst == 0) { return -1; }
    __builtin_memcpy((void *)(uintptr_t)dst, src, (size_t)n);
    return 0;
}
static int syscall64_uread_stub(void *uctx, void *dst, uint64_t src,
                                uint64_t n) {
    (void)uctx;
    if (src == 0) { return -1; }
    __builtin_memcpy(dst, (const void *)(uintptr_t)src, (size_t)n);
    return 0;
}

/* M4.1: static out-buffers for the time-syscall dry-run (avoid caller stack) */
static openos_timespec_t selftest_ts;
static openos_timespec_t selftest_req;

/* M4.1a: static stat out-buffers for the file-metadata dry-run */
static openos_stat_t selftest_st;
static openos_stat_t selftest_st2;

static uint64_t do_syscall(uint64_t num,
                           uint64_t a0,
                           uint64_t a1,
                           uint64_t a2) {
    return arch_x86_64_syscall_dispatch_common(num, a0, a1, a2, 0, 0, 0);
}

static void log_kv_hex(const char *key, uint64_t value) {
    early_console64_write(key);
    early_console64_write_hex64(value);
    early_console64_write("\n");
}

int arch_x86_64_syscall_selftest_run(void) {
    early_console64_write("[x86_64][selftest] step C kernel-side dispatch dry-run\n");

    /* 1) write banner via SYS_WRITE(fd=1) */
    static const char banner[] = "[selftest] hello from kernel-side syscall\n";
    int64_t rv = (int64_t)do_syscall(SYS_WRITE, 1u, (uint64_t)(uintptr_t)banner, sizeof(banner) - 1u);
    if (rv < 0) {
        log_kv_hex("[x86_64][selftest] FAIL step1 write banner rv=", (uint64_t)rv);
        return 1;
    }

    /* 2) open /hello.txt */
    int64_t fd = (int64_t)do_syscall(SYS_OPEN, (uint64_t)(uintptr_t)selftest_target_path, 0u, 0u);
    if (fd < 0) {
        log_kv_hex("[x86_64][selftest] FAIL step2 open rv=", (uint64_t)fd);
        return 2;
    }
    log_kv_hex("[x86_64][selftest] open(/hello.txt) fd=", (uint64_t)fd);

    /* 3) read into static buffer */
    int64_t got = (int64_t)do_syscall(SYS_READ, (uint64_t)fd, (uint64_t)(uintptr_t)selftest_buf, sizeof(selftest_buf));
    if (got <= 0) {
        log_kv_hex("[x86_64][selftest] FAIL step3 read rv=", (uint64_t)got);
        (void)do_syscall(SYS_CLOSE, (uint64_t)fd, 0, 0);
        return 3;
    }
    log_kv_hex("[x86_64][selftest] read bytes=", (uint64_t)got);

    /* 4) echo bytes back to stdout */
    rv = (int64_t)do_syscall(SYS_WRITE, 1u, (uint64_t)(uintptr_t)selftest_buf, (uint64_t)got);
    if (rv < 0) {
        log_kv_hex("[x86_64][selftest] FAIL step4 write echo rv=", (uint64_t)rv);
        (void)do_syscall(SYS_CLOSE, (uint64_t)fd, 0, 0);
        return 4;
    }

    /* 5) close */
    int64_t cls = (int64_t)do_syscall(SYS_CLOSE, (uint64_t)fd, 0, 0);
    if (cls != 0) {
        log_kv_hex("[x86_64][selftest] FAIL step5 close rv=", (uint64_t)cls);
        return 5;
    }

    /* 6) final marker */
    static const char done[] = "[selftest] step C kernel dispatch path OK\n";
    (void)do_syscall(SYS_WRITE, 1u, (uint64_t)(uintptr_t)done, sizeof(done) - 1u);

    /*
     * M4.1: time / clock syscall dry-run. Validates the three new dispatch
     * cases (SYS_CLOCK_GETTIME / SYS_SLEEP / SYS_NANOSLEEP) end-to-end from
     * the kernel side, so the result is observable even when the ring3
     * hello64 path stalls earlier. Uses do_uptime_ms as ground truth.
     */
    {
        /* a) clock_gettime(MONOTONIC) -> selftest_ts */
        int64_t cg = (int64_t)do_syscall(SYS_CLOCK_GETTIME,
                                         (uint64_t)OPENOS_CLOCK_MONOTONIC,
                                         (uint64_t)(uintptr_t)&selftest_ts, 0u);
        log_kv_hex("[x86_64][selftest] M4.1 clock_gettime rv=", (uint64_t)cg);
        log_kv_hex("[x86_64][selftest] M4.1   tv_sec=", (uint64_t)selftest_ts.tv_sec);
        log_kv_hex("[x86_64][selftest] M4.1   tv_nsec=", (uint64_t)selftest_ts.tv_nsec);
        if (cg != 0 || selftest_ts.tv_nsec < 0 || selftest_ts.tv_nsec >= 1000000000) {
            early_console64_write("[x86_64][selftest] FAIL M4.1 clock_gettime bad result\n");
            return 6;
        }

        /* b) nanosleep ~50ms, assert uptime advanced */
        uint64_t t_before = do_syscall(SYS_UPTIME_MS, 0, 0, 0);
        selftest_req.tv_sec  = 0;
        selftest_req.tv_nsec = 50000000; /* 50 ms */
        int64_t ns = (int64_t)do_syscall(SYS_NANOSLEEP,
                                         (uint64_t)(uintptr_t)&selftest_req, 0u, 0u);
        uint64_t t_after = do_syscall(SYS_UPTIME_MS, 0, 0, 0);
        log_kv_hex("[x86_64][selftest] M4.1 nanosleep rv=", (uint64_t)ns);
        log_kv_hex("[x86_64][selftest] M4.1   dt_ms=", t_after - t_before);
        if (ns != 0 || t_after < t_before) {
            early_console64_write("[x86_64][selftest] FAIL M4.1 nanosleep\n");
            return 7;
        }

        early_console64_write("[x86_64][selftest] M4.1 time-syscall path OK\n");
    }

    /*
     * M4.1a: file metadata syscalls dry-run. Exercises the new dispatch
     * cases SYS_STAT / SYS_FSTAT / SYS_MKDIR / SYS_UNLINK / SYS_RENAME and
     * confirms the syscall fd is now VFS-backed (fd >= 3, via the sfd
     * indirection table). Everything runs on the unified VFS (ramfs64).
     */
    {
        /* a) stat(/hello.txt) -> size must match what step3 read. */
        int64_t sr = (int64_t)do_syscall(SYS_STAT,
                                         (uint64_t)(uintptr_t)selftest_target_path,
                                         (uint64_t)(uintptr_t)&selftest_st, 0u);
        log_kv_hex("[x86_64][selftest] M4.1a stat rv=", (uint64_t)sr);
        log_kv_hex("[x86_64][selftest] M4.1a   st.size=", (uint64_t)selftest_st.size);
        log_kv_hex("[x86_64][selftest] M4.1a   st.mode=", (uint64_t)selftest_st.mode);
        if (sr != 0 || selftest_st.size == 0) {
            early_console64_write("[x86_64][selftest] FAIL M4.1a stat\n");
            return 8;
        }

        /* b) open + fstat: the two size views must agree. */
        int64_t fd2 = (int64_t)do_syscall(SYS_OPEN,
                                          (uint64_t)(uintptr_t)selftest_target_path, 0u, 0u);
        if (fd2 < 3) { /* must be a VFS-backed syscall fd, never 0/1/2 */
            log_kv_hex("[x86_64][selftest] FAIL M4.1a open fd2=", (uint64_t)fd2);
            return 9;
        }
        int64_t fsr = (int64_t)do_syscall(SYS_FSTAT, (uint64_t)fd2,
                                          (uint64_t)(uintptr_t)&selftest_st2, 0u);
        log_kv_hex("[x86_64][selftest] M4.1a fstat rv=", (uint64_t)fsr);
        log_kv_hex("[x86_64][selftest] M4.1a   fst.size=", (uint64_t)selftest_st2.size);
        (void)do_syscall(SYS_CLOSE, (uint64_t)fd2, 0, 0);
        if (fsr != 0 || selftest_st2.size != selftest_st.size) {
            early_console64_write("[x86_64][selftest] FAIL M4.1a fstat mismatch\n");
            return 10;
        }

        /* c) mkdir -> stat(dir) is a directory -> create file -> rename
         *    -> unlink. Uses a scratch tree under /m4tmp. */
        int64_t mk = (int64_t)do_syscall(SYS_MKDIR,
                                         (uint64_t)(uintptr_t)"/m4tmp", 0755u, 0u);
        log_kv_hex("[x86_64][selftest] M4.1a mkdir rv=", (uint64_t)mk);
        if (mk != 0) {
            early_console64_write("[x86_64][selftest] FAIL M4.1a mkdir\n");
            return 11;
        }

        /* create a file inside via open(create) + write */
        int64_t fdw = (int64_t)do_syscall(SYS_OPEN,
                                          (uint64_t)(uintptr_t)"/m4tmp/a.txt",
                                          (uint64_t)(O_CREAT | O_WRONLY), 0644u);
        if (fdw < 3) {
            log_kv_hex("[x86_64][selftest] FAIL M4.1a create fdw=", (uint64_t)fdw);
            return 12;
        }
        static const char payload[] = "m4.1a-scratch";
        (void)do_syscall(SYS_WRITE, (uint64_t)fdw,
                         (uint64_t)(uintptr_t)payload, sizeof(payload) - 1u);
        (void)do_syscall(SYS_CLOSE, (uint64_t)fdw, 0, 0);

        /* rename a.txt -> b.txt */
        int64_t rn = (int64_t)do_syscall(SYS_RENAME,
                                         (uint64_t)(uintptr_t)"/m4tmp/a.txt",
                                         (uint64_t)(uintptr_t)"/m4tmp/b.txt", 0u);
        log_kv_hex("[x86_64][selftest] M4.1a rename rv=", (uint64_t)rn);
        if (rn != 0) {
            early_console64_write("[x86_64][selftest] FAIL M4.1a rename\n");
            return 13;
        }
        /* old name must be gone, new name must stat OK */
        int64_t s_old = (int64_t)do_syscall(SYS_STAT,
                                            (uint64_t)(uintptr_t)"/m4tmp/a.txt",
                                            (uint64_t)(uintptr_t)&selftest_st, 0u);
        int64_t s_new = (int64_t)do_syscall(SYS_STAT,
                                            (uint64_t)(uintptr_t)"/m4tmp/b.txt",
                                            (uint64_t)(uintptr_t)&selftest_st, 0u);
        if (s_old == 0 || s_new != 0) {
            log_kv_hex("[x86_64][selftest] FAIL M4.1a rename view s_old=", (uint64_t)s_old);
            log_kv_hex("[x86_64][selftest] FAIL M4.1a rename view s_new=", (uint64_t)s_new);
            return 14;
        }

        /* unlink b.txt */
        int64_t ul = (int64_t)do_syscall(SYS_UNLINK,
                                         (uint64_t)(uintptr_t)"/m4tmp/b.txt", 0u, 0u);
        log_kv_hex("[x86_64][selftest] M4.1a unlink rv=", (uint64_t)ul);
        int64_t s_gone = (int64_t)do_syscall(SYS_STAT,
                                             (uint64_t)(uintptr_t)"/m4tmp/b.txt",
                                             (uint64_t)(uintptr_t)&selftest_st, 0u);
        if (ul != 0 || s_gone == 0) {
            early_console64_write("[x86_64][selftest] FAIL M4.1a unlink\n");
            return 15;
        }

        early_console64_write("[x86_64][selftest] M4.1a file-metadata path OK\n");
    }

    /* ---------------------------------------------------------------------
     * M4.1d: time / device-control / signal syscalls dry-run.
     *   1) gettimeofday   -> tv_sec monotonic, tv_usec in [0,1000000)
     *   2) ioctl on a file -> must fail cleanly (ENOTTY/-1), never trap
     *   3) kill(self, 0)   -> existence probe on our own pid returns 0
     *   4) kill(bogus, 0)  -> unknown pid returns -1 (ESRCH)
     * ------------------------------------------------------------------- */
    {
        openos_timeval_t tv1 = { 0, 0 };
        openos_timeval_t tv2 = { 0, 0 };
        int64_t g1 = (int64_t)do_syscall(SYS_GETTIMEOFDAY,
                                         (uint64_t)(uintptr_t)&tv1, 0u, 0u);
        log_kv_hex("[x86_64][selftest] M4.1d gettimeofday rv=", (uint64_t)g1);
        log_kv_hex("[x86_64][selftest] M4.1d   tv_sec=", (uint64_t)tv1.tv_sec);
        log_kv_hex("[x86_64][selftest] M4.1d   tv_usec=", (uint64_t)tv1.tv_usec);
        if (g1 != 0 || tv1.tv_usec < 0 || tv1.tv_usec >= 1000000) {
            early_console64_write("[x86_64][selftest] FAIL M4.1d gettimeofday\n");
            return 16;
        }
        /* monotonic: a second sample must be >= the first */
        (void)do_syscall(SYS_GETTIMEOFDAY, (uint64_t)(uintptr_t)&tv2, 0u, 0u);
        if (tv2.tv_sec < tv1.tv_sec) {
            early_console64_write("[x86_64][selftest] FAIL M4.1d gettimeofday monotonic\n");
            return 17;
        }

        /* ioctl on stdout: no TTY surface yet -> must return -1, not trap */
        int64_t io = (int64_t)do_syscall(SYS_IOCTL, 1u /*stdout*/, 0x5401u /*TCGETS*/, 0u);
        log_kv_hex("[x86_64][selftest] M4.1d ioctl rv=", (uint64_t)io);
        if (io != -1) {
            early_console64_write("[x86_64][selftest] FAIL M4.1d ioctl (expected -1)\n");
            return 18;
        }

        /* kill existence probe: our own pid must exist */
        uint64_t self_pid = do_syscall(SYS_GETPID, 0u, 0u, 0u);
        int64_t k_self = (int64_t)do_syscall(SYS_KILL, self_pid, 0u /*sig 0*/, 0u);
        log_kv_hex("[x86_64][selftest] M4.1d kill(self,0) rv=", (uint64_t)k_self);
        if (k_self != 0) {
            early_console64_write("[x86_64][selftest] FAIL M4.1d kill(self,0)\n");
            return 19;
        }
        /* kill a pid that cannot exist -> ESRCH (-1) */
        int64_t k_bad = (int64_t)do_syscall(SYS_KILL, 0xDEADu, 0u, 0u);
        log_kv_hex("[x86_64][selftest] M4.1d kill(bogus,0) rv=", (uint64_t)k_bad);
        if (k_bad != -1) {
            early_console64_write("[x86_64][selftest] FAIL M4.1d kill(bogus,0) (expected -1)\n");
            return 20;
        }

        early_console64_write("[x86_64][selftest] M4.1d time/ioctl/kill path OK\n");
    }

    /* ---------------------------------------------------------------------
     * M4.1c: pipe / dup / dup2 IPC dry-run.
     *   1) pipe(fds)          -> fds[0] read end, fds[1] write end (both >=3)
     *   2) write(w, msg)      -> bytes buffered in the ring
     *   3) read(r, buf)       -> same bytes come back in order
     *   4) dup(w) -> w2       -> shares the same pipe write end
     *   5) write(w2, ...)     -> readable via the original read end (shared)
     *   6) dup2(r, target)    -> target aliases the read end
     *   7) close all ends     -> pipe recycled, no leak
     * ------------------------------------------------------------------- */
    {
        int pfds[2] = { -1, -1 };
        int64_t pr = (int64_t)do_syscall(SYS_PIPE,
                                         (uint64_t)(uintptr_t)pfds, 0u, 0u);
        log_kv_hex("[x86_64][selftest] M4.1c pipe rv=", (uint64_t)pr);
        log_kv_hex("[x86_64][selftest] M4.1c   rfd=", (uint64_t)pfds[0]);
        log_kv_hex("[x86_64][selftest] M4.1c   wfd=", (uint64_t)pfds[1]);
        if (pr != 0 || pfds[0] < 3 || pfds[1] < 3 || pfds[0] == pfds[1]) {
            early_console64_write("[x86_64][selftest] FAIL M4.1c pipe\n");
            return 21;
        }

        /* write then read back the same bytes in order */
        static const char pmsg[] = "pipe-hello";
        const uint32_t plen = 10; /* strlen("pipe-hello") */
        int64_t wn = (int64_t)do_syscall(SYS_WRITE, (uint64_t)pfds[1],
                                         (uint64_t)(uintptr_t)pmsg, plen);
        log_kv_hex("[x86_64][selftest] M4.1c write rv=", (uint64_t)wn);
        if (wn != (int64_t)plen) {
            early_console64_write("[x86_64][selftest] FAIL M4.1c write\n");
            return 22;
        }
        char pbuf[16];
        for (int i = 0; i < 16; ++i) pbuf[i] = 0;
        int64_t rn = (int64_t)do_syscall(SYS_READ, (uint64_t)pfds[0],
                                         (uint64_t)(uintptr_t)pbuf, 16u);
        log_kv_hex("[x86_64][selftest] M4.1c read rv=", (uint64_t)rn);
        if (rn != (int64_t)plen) {
            early_console64_write("[x86_64][selftest] FAIL M4.1c read len\n");
            return 23;
        }
        for (uint32_t i = 0; i < plen; ++i) {
            if (pbuf[i] != pmsg[i]) {
                early_console64_write("[x86_64][selftest] FAIL M4.1c read content\n");
                return 24;
            }
        }

        /* dup the write end; data via the clone must reach the read end */
        int64_t w2 = (int64_t)do_syscall(SYS_DUP, (uint64_t)pfds[1], 0u, 0u);
        log_kv_hex("[x86_64][selftest] M4.1c dup(wfd) rv=", (uint64_t)w2);
        if (w2 < 3 || w2 == pfds[1]) {
            early_console64_write("[x86_64][selftest] FAIL M4.1c dup\n");
            return 25;
        }
        static const char pmsg2[] = "DUP";
        (void)do_syscall(SYS_WRITE, (uint64_t)w2,
                         (uint64_t)(uintptr_t)pmsg2, 3u);
        for (int i = 0; i < 16; ++i) pbuf[i] = 0;
        int64_t rn2 = (int64_t)do_syscall(SYS_READ, (uint64_t)pfds[0],
                                          (uint64_t)(uintptr_t)pbuf, 16u);
        log_kv_hex("[x86_64][selftest] M4.1c read-after-dup rv=", (uint64_t)rn2);
        if (rn2 != 3 || pbuf[0] != 'D' || pbuf[1] != 'U' || pbuf[2] != 'P') {
            early_console64_write("[x86_64][selftest] FAIL M4.1c dup shared write\n");
            return 26;
        }

        /* dup2 the read end onto an explicit target slot */
        int target = 50;
        int64_t d2 = (int64_t)do_syscall(SYS_DUP2, (uint64_t)pfds[0],
                                         (uint64_t)target, 0u);
        log_kv_hex("[x86_64][selftest] M4.1c dup2 rv=", (uint64_t)d2);
        if (d2 != target) {
            early_console64_write("[x86_64][selftest] FAIL M4.1c dup2\n");
            return 27;
        }
        /* write via wfd, read via the dup2'd target -> shared read end */
        (void)do_syscall(SYS_WRITE, (uint64_t)pfds[1],
                         (uint64_t)(uintptr_t)pmsg2, 3u);
        for (int i = 0; i < 16; ++i) pbuf[i] = 0;
        int64_t rn3 = (int64_t)do_syscall(SYS_READ, (uint64_t)target,
                                          (uint64_t)(uintptr_t)pbuf, 16u);
        log_kv_hex("[x86_64][selftest] M4.1c read-via-dup2 rv=", (uint64_t)rn3);
        if (rn3 != 3) {
            early_console64_write("[x86_64][selftest] FAIL M4.1c dup2 shared read\n");
            return 28;
        }

        /* close every end; the pipe must be fully released */
        (void)do_syscall(SYS_CLOSE, (uint64_t)pfds[0], 0u, 0u);
        (void)do_syscall(SYS_CLOSE, (uint64_t)pfds[1], 0u, 0u);
        (void)do_syscall(SYS_CLOSE, (uint64_t)w2, 0u, 0u);
        (void)do_syscall(SYS_CLOSE, (uint64_t)target, 0u, 0u);

        early_console64_write("[x86_64][selftest] M4.1c pipe/dup/dup2 path OK\n");
    }

    /* -------- M4.3a: pipe poll + waiter-queue bookkeeping -------- */
    {
        /* Allocate a raw pipe directly at the pipe64 layer so we can drive
         * the poll / waitq primitives without the syscall fd indirection. */
        int rawp = pipe64_create();
        if (rawp < 0) {
            early_console64_write("[x86_64][selftest] FAIL M4.3a pipe64_create\n");
            return 30;
        }
        log_kv_hex("[x86_64][selftest] M4.3a raw pipe id=", (uint64_t)rawp);

        /* Fresh pipe: empty + both ends open -> WRITABLE, not READABLE. */
        int pl = pipe64_poll(rawp);
        log_kv_hex("[x86_64][selftest] M4.3a poll(empty)=", (uint64_t)pl);
        if (!(pl & PIPE64_POLL_WRITABLE) || (pl & PIPE64_POLL_READABLE)) {
            early_console64_write("[x86_64][selftest] FAIL M4.3a empty poll bits\n");
            return 31;
        }

        /* Write some bytes -> now READABLE too, buffered count matches. */
        const char *msg = "abcdef";
        int wn = pipe64_write(rawp, msg, 6);
        if (wn != 6) {
            early_console64_write("[x86_64][selftest] FAIL M4.3a raw write\n");
            return 32;
        }
        pl = pipe64_poll(rawp);
        log_kv_hex("[x86_64][selftest] M4.3a poll(data)=", (uint64_t)pl);
        if (!(pl & PIPE64_POLL_READABLE)) {
            early_console64_write("[x86_64][selftest] FAIL M4.3a data not readable\n");
            return 33;
        }
        if (pipe64_buffered(rawp) != 6 ||
            pipe64_space(rawp) != (int)(PIPE64_CAPACITY - 6)) {
            early_console64_write("[x86_64][selftest] FAIL M4.3a buffered/space\n");
            return 34;
        }

        /* Waiter queue: register two slots on the read side, dedup a repeat,
         * then drain them back out. */
        if (pipe64_wait_add(rawp, 0, 11) != 0 ||
            pipe64_wait_add(rawp, 0, 22) != 0 ||
            pipe64_wait_add(rawp, 0, 11) != 0 /* dup coalesced */) {
            early_console64_write("[x86_64][selftest] FAIL M4.3a wait_add\n");
            return 35;
        }
        if (pipe64_wait_count(rawp, 0) != 2) {
            early_console64_write("[x86_64][selftest] FAIL M4.3a wait dedup\n");
            return 36;
        }
        uint32_t drained[PIPE64_WAITERS_MAX];
        int dn = pipe64_wait_drain(rawp, 0, drained);
        log_kv_hex("[x86_64][selftest] M4.3a drained=", (uint64_t)dn);
        if (dn != 2 || pipe64_wait_count(rawp, 0) != 0) {
            early_console64_write("[x86_64][selftest] FAIL M4.3a wait_drain\n");
            return 37;
        }

        /* Drain the buffered bytes; with a writer still open an empty pipe is
         * NOT readable (would block), i.e. no EOF yet. */
        char rb[8];
        int rn = pipe64_read(rawp, rb, 6);
        if (rn != 6) {
            early_console64_write("[x86_64][selftest] FAIL M4.3a raw read\n");
            return 38;
        }
        pl = pipe64_poll(rawp);
        if (pl & PIPE64_POLL_READABLE) {
            early_console64_write("[x86_64][selftest] FAIL M4.3a premature EOF\n");
            return 39;
        }

        /* Close the write end -> empty pipe now reports READABLE (EOF) + HUP. */
        pipe64_close_end(rawp, 1 /*is_write*/);
        pl = pipe64_poll(rawp);
        log_kv_hex("[x86_64][selftest] M4.3a poll(eof)=", (uint64_t)pl);
        if (!(pl & PIPE64_POLL_READABLE) || !(pl & PIPE64_POLL_HUP)) {
            early_console64_write("[x86_64][selftest] FAIL M4.3a EOF poll bits\n");
            return 44;
        }
        if (pipe64_has_writer(rawp) != 0) {
            early_console64_write("[x86_64][selftest] FAIL M4.3a writer still open\n");
            return 45;
        }

        /* Release the read end too -> pipe fully freed. */
        pipe64_close_end(rawp, 0 /*is_read*/);

        early_console64_write("[x86_64][selftest] M4.3a pipe poll/waitq path OK\n");
    }

    /* -------- M4.3b: named pipes (FIFO) via syscall path -------- */
    {
        fifo64_reset();
        const char *fp = "/tmp/fifo0";

        /* mkfifo via syscall dispatch. */
        long mrc = do_syscall(SYS_MKFIFO,
                              (uint64_t)(uintptr_t)fp,
                              0666, 0);
        log_kv_hex("[x86_64][selftest] M4.3b mkfifo rc=", (uint64_t)mrc);
        if (mrc != 0) {
            early_console64_write("[x86_64][selftest] FAIL M4.3b mkfifo\n");
            return 46;
        }
        /* Duplicate mkfifo must fail (EEXIST). */
        long mrc2 = do_syscall(SYS_MKFIFO,
                               (uint64_t)(uintptr_t)fp,
                               0666, 0);
        if (mrc2 == 0) {
            early_console64_write("[x86_64][selftest] FAIL M4.3b dup mkfifo\n");
            return 47;
        }
        if (fifo64_active_count() != 1) {
            early_console64_write("[x86_64][selftest] FAIL M4.3b active_count\n");
            return 48;
        }

        /* Open write end (O_WRONLY=1) and read end (O_RDONLY=0). */
        long wfd = do_syscall(SYS_OPEN,
                              (uint64_t)(uintptr_t)fp,
                              O_WRONLY, 0);
        long rfd = do_syscall(SYS_OPEN,
                              (uint64_t)(uintptr_t)fp,
                              O_RDONLY, 0);
        log_kv_hex("[x86_64][selftest] M4.3b wfd=", (uint64_t)wfd);
        log_kv_hex("[x86_64][selftest] M4.3b rfd=", (uint64_t)rfd);
        if (wfd < 3 || rfd < 3) {
            early_console64_write("[x86_64][selftest] FAIL M4.3b open fds\n");
            return 49;
        }
        /* Both opens share ONE backing ring. */
        if (fifo64_pipe_id(fp) < 0) {
            early_console64_write("[x86_64][selftest] FAIL M4.3b no ring\n");
            return 51;
        }

        /* Write through the FIFO write fd, read back through the read fd. */
        const char *payload = "FIFO!";
        long wn = do_syscall(SYS_WRITE, (uint64_t)wfd,
                             (uint64_t)(uintptr_t)payload, 5);
        log_kv_hex("[x86_64][selftest] M4.3b write n=", (uint64_t)wn);
        if (wn != 5) {
            early_console64_write("[x86_64][selftest] FAIL M4.3b write\n");
            return 52;
        }
        char rbuf[8] = {0};
        long rn = do_syscall(SYS_READ, (uint64_t)rfd,
                             (uint64_t)(uintptr_t)rbuf, 5);
        log_kv_hex("[x86_64][selftest] M4.3b read n=", (uint64_t)rn);
        if (rn != 5 || rbuf[0] != 'F' || rbuf[4] != '!') {
            early_console64_write("[x86_64][selftest] FAIL M4.3b roundtrip\n");
            return 53;
        }

        /* Close the write end: a subsequent read must see EOF (0), not block. */
        do_syscall(SYS_CLOSE, (uint64_t)wfd, 0, 0);
        long eof = do_syscall(SYS_READ, (uint64_t)rfd,
                              (uint64_t)(uintptr_t)rbuf, 5);
        log_kv_hex("[x86_64][selftest] M4.3b eof read=", (uint64_t)eof);
        if (eof != 0) {
            early_console64_write("[x86_64][selftest] FAIL M4.3b EOF\n");
            return 54;
        }
        do_syscall(SYS_CLOSE, (uint64_t)rfd, 0, 0);

        /* Unlink the FIFO name -> active count back to zero. */
        if (fifo64_unlink(fp) != 0 || fifo64_active_count() != 0) {
            early_console64_write("[x86_64][selftest] FAIL M4.3b unlink\n");
            return 55;
        }

        early_console64_write("[x86_64][selftest] M4.3b FIFO path OK\n");
    }

    /* -------- M4.3c: shared memory (shmget/shmat/shmdt/shmctl) -------- */
    {
        shm64_reset();
        const uint32_t key = 0x1234;
        const uint64_t sz  = 8192; /* 2 pages */

        /* shmget: create a keyed segment. */
        long id = do_syscall(SYS_SHM_CREATE, key, sz, 0);
        log_kv_hex("[x86_64][selftest] M4.3c shmget id=", (uint64_t)id);
        if (id < 0) {
            early_console64_write("[x86_64][selftest] FAIL M4.3c shmget\n");
            return 56;
        }
        /* Second shmget with the same key returns the SAME segment. */
        long id2 = do_syscall(SYS_SHM_CREATE, key, sz, 0);
        if (id2 != id) {
            early_console64_write("[x86_64][selftest] FAIL M4.3c key reuse\n");
            return 57;
        }
        if (shm64_active_count() != 1) {
            early_console64_write("[x86_64][selftest] FAIL M4.3c active_count\n");
            return 58;
        }

        /* Two independent attaches model two processes sharing the segment. */
        long baseA = do_syscall(SYS_SHM_MAP, (uint64_t)id, 0, 0);
        long baseB = do_syscall(SYS_SHM_MAP, (uint64_t)id, 0, 0);
        log_kv_hex("[x86_64][selftest] M4.3c baseA=", (uint64_t)baseA);
        log_kv_hex("[x86_64][selftest] M4.3c baseB=", (uint64_t)baseB);
        if (baseA == (long)SHM64_ATTACH_FAILED ||
            baseB == (long)SHM64_ATTACH_FAILED ||
            baseA != baseB) {
            early_console64_write("[x86_64][selftest] FAIL M4.3c attach base\n");
            return 59;
        }
        /* nattch must now be 2. */
        long nat = do_syscall(SYS_SHM_INFO, (uint64_t)id, 1, 0);
        if (nat != 2) {
            early_console64_write("[x86_64][selftest] FAIL M4.3c nattch\n");
            return 61;
        }

        /* Writer (attach A) stores a pattern; reader (attach B) sees it since
         * both map the same identity-mapped physical pages. */
        volatile uint8_t *pa = (volatile uint8_t *)(uintptr_t)baseA;
        volatile uint8_t *pb = (volatile uint8_t *)(uintptr_t)baseB;
        pa[0] = 0x5A;
        pa[4096] = 0xC3;   /* second page */
        pa[8191] = 0x7E;   /* last byte    */
        if (pb[0] != 0x5A || pb[4096] != 0xC3 || pb[8191] != 0x7E) {
            early_console64_write("[x86_64][selftest] FAIL M4.3c shared write\n");
            return 62;
        }

        /* Detach both; nattch returns to zero. */
        if (do_syscall(SYS_SHM_DETACH, (uint64_t)id, 0, 0) != 0 ||
            do_syscall(SYS_SHM_DETACH, (uint64_t)id, 0, 0) != 0) {
            early_console64_write("[x86_64][selftest] FAIL M4.3c detach\n");
            return 63;
        }
        if (shm64_nattch((int)id) != 0) {
            early_console64_write("[x86_64][selftest] FAIL M4.3c nattch!=0\n");
            return 64;
        }
        /* Extra detach must error (-1) since count is already zero. */
        if (do_syscall(SYS_SHM_DETACH, (uint64_t)id, 0, 0) == 0) {
            early_console64_write("[x86_64][selftest] FAIL M4.3c over-detach\n");
            return 65;
        }

        /* IPC_RMID: with no attachers the segment frees immediately. */
        if (do_syscall(SYS_SHM_DESTROY, (uint64_t)id, 0, 0) != 0) {
            early_console64_write("[x86_64][selftest] FAIL M4.3c rmid\n");
            return 66;
        }
        if (shm64_active_count() != 0) {
            early_console64_write("[x86_64][selftest] FAIL M4.3c not freed\n");
            return 67;
        }

        early_console64_write("[x86_64][selftest] M4.3c shared memory path OK\n");
    }

    /* -------- M4.4a: TTY line discipline (canonical/raw/echo/ioctl) -------- */
    {
        int t = tty64_create();
        if (t < 0) {
            early_console64_write("[x86_64][selftest] FAIL M4.4a tty create\n");
            return 60;
        }
        log_kv_hex("[x86_64][selftest] M4.4a tty id=", (uint64_t)t);

        /* Canonical: an incomplete line is NOT readable until '\n'. */
        tty64_input_str(t, "abc");
        if (tty64_readable(t) != 0) {
            early_console64_write("[x86_64][selftest] FAIL M4.4a partial line readable\n");
            return 61;
        }
        /* Complete the line -> whole line becomes readable (4 = 'a','b','c','\n'). */
        tty64_input_byte(t, (uint8_t)'\n');
        int rd = tty64_readable(t);
        log_kv_hex("[x86_64][selftest] M4.4a readable-after-nl=", (uint64_t)rd);
        if (rd != 4) {
            early_console64_write("[x86_64][selftest] FAIL M4.4a line count\n");
            return 62;
        }
        /* read() returns exactly one line. */
        int n = tty64_read(t, selftest_buf, sizeof(selftest_buf));
        log_kv_hex("[x86_64][selftest] M4.4a read-len=", (uint64_t)n);
        if (n != 4 || selftest_buf[0] != 'a' || selftest_buf[3] != '\n') {
            early_console64_write("[x86_64][selftest] FAIL M4.4a canonical read\n");
            return 63;
        }

        /* ERASE (^H=DEL 0x7F): 'x','y',ERASE,'z','\n' -> "xz\n" (3 bytes). */
        tty64_input_byte(t, (uint8_t)'x');
        tty64_input_byte(t, (uint8_t)'y');
        tty64_input_byte(t, 0x7F);
        tty64_input_byte(t, (uint8_t)'z');
        tty64_input_byte(t, (uint8_t)'\n');
        n = tty64_read(t, selftest_buf, sizeof(selftest_buf));
        log_kv_hex("[x86_64][selftest] M4.4a erase-read-len=", (uint64_t)n);
        if (n != 3 || selftest_buf[0] != 'x' || selftest_buf[1] != 'z') {
            early_console64_write("[x86_64][selftest] FAIL M4.4a erase edit\n");
            return 64;
        }

        /* KILL (^U=0x15): wipe the whole pending line. */
        tty64_input_str(t, "garbage");
        tty64_input_byte(t, 0x15);
        tty64_input_str(t, "ok\n");
        n = tty64_read(t, selftest_buf, sizeof(selftest_buf));
        if (n != 3 || selftest_buf[0] != 'o' || selftest_buf[1] != 'k') {
            early_console64_write("[x86_64][selftest] FAIL M4.4a kill edit\n");
            return 65;
        }

        /* INTR (^C=0x03) with ISIG: discards line + latches SIGINT(2). */
        tty64_input_str(t, "junk");
        tty64_input_byte(t, 0x03);
        int sig = tty64_take_signal(t);
        log_kv_hex("[x86_64][selftest] M4.4a intr-signal=", (uint64_t)sig);
        if (sig != 2) {
            early_console64_write("[x86_64][selftest] FAIL M4.4a intr signal\n");
            return 66;
        }
        if (tty64_readable(t) != 0) {
            early_console64_write("[x86_64][selftest] FAIL M4.4a intr did not flush\n");
            return 67;
        }

        /* EOF (^D=0x04) on empty line -> next read returns 0 (end of file). */
        tty64_input_byte(t, 0x04);
        n = tty64_read(t, selftest_buf, sizeof(selftest_buf));
        if (n != 0) {
            early_console64_write("[x86_64][selftest] FAIL M4.4a eof empty\n");
            return 68;
        }

        /* ECHO: printable chars mirror to output ring; ^C shows as "^C". */
        {
            /* drain any stale echo first */
            char eb[64];
            tty64_drain_output(t, eb, sizeof(eb));
            tty64_input_str(t, "hi\n");
            int op = tty64_output_pending(t);
            log_kv_hex("[x86_64][selftest] M4.4a echo-pending=", (uint64_t)op);
            if (op != 3) { /* 'h','i','\n' */
                early_console64_write("[x86_64][selftest] FAIL M4.4a echo count\n");
                return 69;
            }
            n = tty64_drain_output(t, eb, sizeof(eb));
            if (n != 3 || eb[0] != 'h' || eb[1] != 'i' || eb[2] != '\n') {
                early_console64_write("[x86_64][selftest] FAIL M4.4a echo bytes\n");
                return 70;
            }
            /* consume the committed line so it doesn't leak into later checks */
            tty64_read(t, selftest_buf, sizeof(selftest_buf));
        }

        /* RAW mode via TCSETS: clear ICANON -> each byte instantly readable. */
        {
            tty64_termios_t tio;
            if (tty64_tcgets(t, &tio) != 0) {
                early_console64_write("[x86_64][selftest] FAIL M4.4a tcgets\n");
                return 71;
            }
            tio.c_lflag &= ~TTY64_ICANON;
            if (tty64_tcsets(t, &tio) != 0) {
                early_console64_write("[x86_64][selftest] FAIL M4.4a tcsets\n");
                return 72;
            }
            tty64_input_byte(t, (uint8_t)'Q');
            if (tty64_readable(t) != 1) {
                early_console64_write("[x86_64][selftest] FAIL M4.4a raw not immediate\n");
                return 73;
            }
            n = tty64_read(t, selftest_buf, sizeof(selftest_buf));
            if (n != 1 || selftest_buf[0] != 'Q') {
                early_console64_write("[x86_64][selftest] FAIL M4.4a raw read\n");
                return 74;
            }
        }

        /* ioctl wiring on fd 1 (console tty): TCGETS/TCSETS round-trip. */
        {
            tty64_termios_t tio;
            int64_t rc = (int64_t)do_syscall(SYS_IOCTL, 1u, 0x5401u /*TCGETS*/, (uint64_t)&tio);
            log_kv_hex("[x86_64][selftest] M4.4a ioctl TCGETS rc=", (uint64_t)rc);
            if (rc != 0) {
                early_console64_write("[x86_64][selftest] FAIL M4.4a ioctl TCGETS\n");
                return 75;
            }
            /* winsize query should report the default 25x80 */
            tty64_winsize_t ws;
            rc = (int64_t)do_syscall(SYS_IOCTL, 1u, 0x5413u /*TIOCGWINSZ*/, (uint64_t)&ws);
            log_kv_hex("[x86_64][selftest] M4.4a winsize rows=", (uint64_t)ws.ws_row);
            log_kv_hex("[x86_64][selftest] M4.4a winsize cols=", (uint64_t)ws.ws_col);
            if (rc != 0 || ws.ws_row != 25 || ws.ws_col != 80) {
                early_console64_write("[x86_64][selftest] FAIL M4.4a ioctl winsize\n");
                return 76;
            }
            /* ioctl on a regular (non-tty) fd must fail (ENOTTY). */
            rc = (int64_t)do_syscall(SYS_IOCTL, 999u, 0x5401u, (uint64_t)&tio);
            if (rc != -1) {
                early_console64_write("[x86_64][selftest] FAIL M4.4a ioctl non-tty\n");
                return 77;
            }
        }

        /* TIOCSPGRP/TIOCGPGRP round-trip on the console tty (job control). */
        {
            uint32_t pg = 42u;
            int64_t rc = (int64_t)do_syscall(SYS_IOCTL, 0u, 0x5410u /*TIOCSPGRP*/, (uint64_t)&pg);
            uint32_t back = 0u;
            rc |= (int64_t)do_syscall(SYS_IOCTL, 0u, 0x540Fu /*TIOCGPGRP*/, (uint64_t)&back);
            log_kv_hex("[x86_64][selftest] M4.4a pgrp readback=", (uint64_t)back);
            if (rc != 0 || back != 42u) {
                early_console64_write("[x86_64][selftest] FAIL M4.4a pgrp round-trip\n");
                return 78;
            }
        }

        early_console64_write("[x86_64][selftest] M4.4a TTY line discipline path OK\n");
    }

    /* -------- M4.4b: job control (process groups / sessions / ^C bridge) -------- */
    {
        /* getpgid(0)/getsid(0) of the current (kernel/init) process. Both
         * should be well-defined (the init proc leads its own group+session). */
        int64_t pg = (int64_t)do_syscall(SYS_GETPGID, 0, 0, 0);
        int64_t sd = (int64_t)do_syscall(SYS_GETSID, 0, 0, 0);
        log_kv_hex("[x86_64][selftest] M4.4b getpgid(self)=", (uint64_t)pg);
        log_kv_hex("[x86_64][selftest] M4.4b getsid(self)=", (uint64_t)sd);
        if (pg < 0 || sd < 0) {
            early_console64_write("[x86_64][selftest] FAIL M4.4b self pgid/sid\n");
            return 80;
        }

        /* getpgid of a bogus pid must fail. */
        int64_t bad = (int64_t)do_syscall(SYS_GETPGID, 0xC0FFEEu, 0, 0);
        if (bad != -1) {
            early_console64_write("[x86_64][selftest] FAIL M4.4b getpgid bad pid\n");
            return 81;
        }

        /* setsid() on a process that is already a group leader (the init
         * proc, pgid==pid) must fail (POSIX EPERM). */
        int64_t ss = (int64_t)do_syscall(SYS_SETSID, 0, 0, 0);
        log_kv_hex("[x86_64][selftest] M4.4b setsid(leader)=", (uint64_t)ss);
        if (ss != -1) {
            early_console64_write("[x86_64][selftest] FAIL M4.4b setsid on leader\n");
            return 82;
        }

        /* kill(-pgid, 0) existence probe against the current group: since the
         * caller is a member, at least one process should match -> rc 0. */
        int64_t kg = (int64_t)do_syscall(SYS_KILL, (uint64_t)(-(int64_t)pg), 0, 0);
        log_kv_hex("[x86_64][selftest] M4.4b kill(-pgrp,0) rc=", (uint64_t)kg);
        if (kg != 0) {
            early_console64_write("[x86_64][selftest] FAIL M4.4b group probe\n");
            return 83;
        }

        /* kill(0, 0): signal the caller's own group, existence probe -> 0. */
        int64_t k0 = (int64_t)do_syscall(SYS_KILL, 0, 0, 0);
        if (k0 != 0) {
            early_console64_write("[x86_64][selftest] FAIL M4.4b kill(0,0)\n");
            return 84;
        }

        /* kill against a non-existent group must fail. */
        int64_t kbad = (int64_t)do_syscall(SYS_KILL, (uint64_t)(-999999), 0, 0);
        if (kbad != -1) {
            early_console64_write("[x86_64][selftest] FAIL M4.4b kill bad group\n");
            return 85;
        }

        /* TTY ^C -> foreground pgrp bridge. Point the console tty's foreground
         * group at the caller's group, inject ^C, then pump: the signal must
         * be routed to the group (>=1 process hit). */
        {
            uint32_t fg = (uint32_t)pg;
            (void)do_syscall(SYS_IOCTL, 1u, 0x5410u /*TIOCSPGRP*/, (uint64_t)&fg);
            /* inject ^C on the console tty via the engine */
            int ctid = -1;
            /* the console tty id is whatever fd 0/1/2 map to; reuse ioctl to
             * confirm it exists, then feed the byte through the engine by
             * toggling: we rely on arch_x86_64_tty_pump_signals to consume it */
            extern int arch_x86_64_tty_pump_signals(void);
            /* find console tty id: create is idempotent-ish; instead just feed
             * the byte to tty id 0 (console is the first-created tty). */
            for (int i = 0; i < TTY64_MAX; ++i) {
                if (tty64_valid(i)) { ctid = i; break; }
            }
            if (ctid < 0) {
                early_console64_write("[x86_64][selftest] FAIL M4.4b no console tty\n");
                return 86;
            }
            /* Ensure canonical + ISIG so ^C latches a signal. */
            tty64_termios_t tio;
            tty64_tcgets(ctid, &tio);
            tio.c_lflag |= (TTY64_ICANON | TTY64_ISIG);
            tty64_tcsets(ctid, &tio);
            tty64_set_pgrp(ctid, (int)pg);
            tty64_input_byte(ctid, 0x03); /* ^C */
            int hits = arch_x86_64_tty_pump_signals();
            log_kv_hex("[x86_64][selftest] M4.4b ^C bridge hits=", (uint64_t)hits);
            if (hits < 1) {
                early_console64_write("[x86_64][selftest] FAIL M4.4b ^C bridge\n");
                return 87;
            }
            /* No foreground group -> signal must be dropped (0 hits). */
            tty64_set_pgrp(ctid, 0);
            tty64_input_byte(ctid, 0x03);
            int hits2 = arch_x86_64_tty_pump_signals();
            if (hits2 != 0) {
                early_console64_write("[x86_64][selftest] FAIL M4.4b bg drop\n");
                return 88;
            }
        }

        early_console64_write("[x86_64][selftest] M4.4b job control path OK\n");
    }

    /* -------- M4.1b: memory (mmap/munmap/mprotect/brk/sbrk) -------- */
    {
        /* mmap anonymous RW region (prot = PROT_READ|PROT_WRITE = 0x3) */
        uint64_t page = 0x1000u;
        uint64_t base = do_syscall(SYS_MMAP, 0u, page, 0x3u);
        if (base == (uint64_t)-1) {
            early_console64_write("[x86_64][selftest] FAIL M4.1b mmap returned MAP_FAILED\n");
            return 40;
        }
        log_kv_hex("[x86_64][selftest] M4.1b mmap base=", base);

        /* the region must be writable + readable back */
        volatile unsigned char *p = (volatile unsigned char *)base;
        p[0] = 0xA5u; p[page - 1u] = 0x5Au;
        if (p[0] != 0xA5u || p[page - 1u] != 0x5Au) {
            early_console64_write("[x86_64][selftest] FAIL M4.1b mmap region not writable\n");
            return 41;
        }

        /* mprotect the region to read-only (prot = PROT_READ = 0x1) */
        int64_t mp = (int64_t)do_syscall(SYS_MPROTECT, base, page, 0x1u);
        if (mp != 0) {
            early_console64_write("[x86_64][selftest] FAIL M4.1b mprotect rv!=0\n");
            return 42;
        }

        /* munmap must release cleanly */
        int64_t mu = (int64_t)do_syscall(SYS_MUNMAP, base, page, 0u);
        if (mu != 0) {
            early_console64_write("[x86_64][selftest] FAIL M4.1b munmap rv!=0\n");
            return 43;
        }

        /* brk query (addr=0) returns current break; must be non-zero */
        uint64_t cur_brk = do_syscall(SYS_BRK, 0u, 0u, 0u);
        if (cur_brk == 0u || cur_brk == (uint64_t)-1) {
            early_console64_write("[x86_64][selftest] FAIL M4.1b brk query bad\n");
            return 44;
        }
        log_kv_hex("[x86_64][selftest] M4.1b brk cur=", cur_brk);

        /* sbrk(+page) returns previous break == cur_brk, then grows.
         *
         * M5.3e note: SYS_SBRK was re-routed to the per-process user-space
         * heap (U=1 pages in the caller's own address space). This kernel-side
         * selftest runs before any ring3 process exists, so there is no current
         * process / address space to grow -- the call legitimately returns -1.
         * The real sbrk/malloc validation now lives in the ring3 libc/opk demo.
         * Only treat -1 as a hard failure when a user process context exists. */
        uint64_t prev = do_syscall(SYS_SBRK, page, 0u, 0u);
        if (prev == (uint64_t)-1) {
            if (arch_x86_64_proc_current() == (void *)0) {
                early_console64_write("[x86_64][selftest] SKIP M4.1b sbrk "
                                      "(no ring3 ctx; user-heap tested in libc demo)\n");
            } else {
                early_console64_write("[x86_64][selftest] FAIL M4.1b sbrk grow failed\n");
                return 45;
            }
        } else {
            uint64_t after = do_syscall(SYS_BRK, 0u, 0u, 0u);
            if (after != prev + page) {
                early_console64_write("[x86_64][selftest] FAIL M4.1b sbrk break not advanced\n");
                return 46;
            }
            /* give it back */
            (void)do_syscall(SYS_SBRK, (uint64_t)(-(int64_t)page), 0u, 0u);
        }

        early_console64_write("[x86_64][selftest] M4.1b mmap/munmap/mprotect/brk/sbrk path OK\n");
    }

    /* -------- M4.2: signals (sigaction/sigprocmask/kill dispositions) -------- */
    {
        /* struct openos_sigaction mirror { handler; mask; flags } */
        struct { uint64_t handler; uint64_t mask; uint64_t flags; } act, old;

        /* (1) sigaction register + read-back on SIGUSR1 (10).
         *     Register SIG_IGN, then query it back through `old`. */
        act.handler = 1u /*OPENOS_SIG_IGN*/; act.mask = 0u; act.flags = 0u;
        int64_t sa = (int64_t)do_syscall(SYS_RT_SIGACTION, 10u,
                                         (uint64_t)(uintptr_t)&act, 0u);
        if (sa != 0) {
            early_console64_write("[x86_64][selftest] FAIL M4.2 sigaction set\n");
            return 50;
        }
        old.handler = 0xEEu; old.mask = 0u; old.flags = 0u;
        int64_t saq = (int64_t)do_syscall(SYS_RT_SIGACTION, 10u,
                                          0u, (uint64_t)(uintptr_t)&old);
        log_kv_hex("[x86_64][selftest] M4.2 sigaction readback handler=", old.handler);
        if (saq != 0 || old.handler != 1u) {
            early_console64_write("[x86_64][selftest] FAIL M4.2 sigaction readback\n");
            return 51;
        }

        /* (2) SIGKILL (9) disposition is immutable: sigaction must fail. */
        act.handler = 1u; act.mask = 0u; act.flags = 0u;
        int64_t sak = (int64_t)do_syscall(SYS_RT_SIGACTION, 9u,
                                          (uint64_t)(uintptr_t)&act, 0u);
        if (sak == 0) {
            early_console64_write("[x86_64][selftest] FAIL M4.2 SIGKILL disposition mutable\n");
            return 52;
        }

        /* (3) sigprocmask: block SIGUSR2 (12), read back the old (empty)
         *     mask, then verify the new mask contains the SIGUSR2 bit. */
        uint64_t set = (uint64_t)1 << 12; /* bit 12 = SIGUSR2 */
        uint64_t oldset = 0xFFFFu;
        int64_t pm = (int64_t)do_syscall(SYS_RT_SIGPROCMASK, 0u /*BLOCK*/,
                                         (uint64_t)(uintptr_t)&set,
                                         (uint64_t)(uintptr_t)&oldset);
        log_kv_hex("[x86_64][selftest] M4.2 sigprocmask oldset=", oldset);
        if (pm != 0 || oldset != 0u) {
            early_console64_write("[x86_64][selftest] FAIL M4.2 sigprocmask block\n");
            return 53;
        }
        /* query the now-current mask (set ptr = NULL) */
        uint64_t curset = 0u;
        int64_t pmq = (int64_t)do_syscall(SYS_RT_SIGPROCMASK, 0u,
                                          0u, (uint64_t)(uintptr_t)&curset);
        log_kv_hex("[x86_64][selftest] M4.2 sigprocmask curmask=", curset);
        if (pmq != 0 || (curset & ((uint64_t)1 << 12)) == 0u) {
            early_console64_write("[x86_64][selftest] FAIL M4.2 sigprocmask readback\n");
            return 54;
        }
        /* SIGKILL bit must never survive in the blocked mask */
        uint64_t badset = ((uint64_t)1 << 9) | ((uint64_t)1 << 19); /*KILL|STOP*/
        (void)do_syscall(SYS_RT_SIGPROCMASK, 2u /*SETMASK*/,
                         (uint64_t)(uintptr_t)&badset, 0u);
        uint64_t chk = 0u;
        (void)do_syscall(SYS_RT_SIGPROCMASK, 0u, 0u,
                         (uint64_t)(uintptr_t)&chk);
        log_kv_hex("[x86_64][selftest] M4.2 sigprocmask kill-stripped=", chk);
        if ((chk & (((uint64_t)1 << 9) | ((uint64_t)1 << 19))) != 0u) {
            early_console64_write("[x86_64][selftest] FAIL M4.2 KILL/STOP not stripped\n");
            return 55;
        }
        /* restore an empty mask */
        uint64_t empty = 0u;
        (void)do_syscall(SYS_RT_SIGPROCMASK, 2u,
                         (uint64_t)(uintptr_t)&empty, 0u);

        /* (4) kill: existence probe on self, and ESRCH on a bogus pid. */
        uint64_t self_pid2 = do_syscall(SYS_GETPID, 0u, 0u, 0u);
        int64_t k0 = (int64_t)do_syscall(SYS_KILL, self_pid2, 0u, 0u);
        int64_t kbad = (int64_t)do_syscall(SYS_KILL, 0xC0DEu, 0u, 0u);
        log_kv_hex("[x86_64][selftest] M4.2 kill(self,0)=", (uint64_t)k0);
        log_kv_hex("[x86_64][selftest] M4.2 kill(bad,0)=", (uint64_t)kbad);
        if (k0 != 0 || kbad != -1) {
            early_console64_write("[x86_64][selftest] FAIL M4.2 kill existence probe\n");
            return 56;
        }
        /* (5) kill with an out-of-range signal number must be rejected. */
        int64_t kinv = (int64_t)do_syscall(SYS_KILL, self_pid2, 99u, 0u);
        if (kinv != -1) {
            early_console64_write("[x86_64][selftest] FAIL M4.2 kill bad signo accepted\n");
            return 57;
        }

        early_console64_write("[x86_64][selftest] M4.2 sigaction/sigprocmask/kill path OK\n");
    }

    /* ---- M4.2b: user-mode signal handler trampoline round-trip ---------- *
     *
     * The trampoline glue (proc64 deliver_user / sigreturn) is normally
     * driven from the syscall return path against a live ring3 frame. Here
     * we exercise it head-on in kernel context: register a user handler,
     * raise the signal, ask deliver_user() to build a frame on a scratch
     * "user" stack, verify the outgoing regs point into the handler, then
     * hand the same regs to sigreturn() and verify the pre-signal context is
     * restored byte-for-byte. The scratch stack stands in for identity-
     * mapped user memory, so the uwrite/uread callbacks are plain memcpy. */
    {
        /* 4 KiB scratch "user stack", 16-byte aligned top. */
        static uint8_t sig_stack[4096] __attribute__((aligned(16)));
        uint64_t stack_top = (uint64_t)(uintptr_t)(sig_stack + sizeof(sig_stack));

        const uint64_t HANDLER  = 0x0000000000401234ull; /* fake handler VA */
        const uint64_t RESTORER = 0x0000000000401300ull; /* fake restorer VA */
        const int      SIG      = 11;                     /* SIGSEGV-ish slot */

        /* Register a user handler on SIG via sigaction. The restorer VA
         * rides in `flags` per our SA_RESTORER convention. */
        struct { uint64_t handler; uint64_t mask; uint64_t flags; } b_act;
        b_act.handler = HANDLER; b_act.mask = 0u; b_act.flags = RESTORER;
        int64_t b_sa = (int64_t)do_syscall(SYS_RT_SIGACTION, (uint64_t)SIG,
                                           (uint64_t)(uintptr_t)&b_act, 0u);
        if (b_sa != 0) {
            early_console64_write("[x86_64][selftest] FAIL M4.2b sigaction set\n");
            return 60;
        }

        /* Make SIG pending on the current process. */
        uint64_t b_pid = do_syscall(SYS_GETPID, 0u, 0u, 0u);
        int64_t b_kill = (int64_t)do_syscall(SYS_KILL, b_pid, (uint64_t)SIG, 0u);
        if (b_kill != 0) {
            early_console64_write("[x86_64][selftest] FAIL M4.2b kill(self,SIG)\n");
            return 61;
        }

        /* Craft a pre-signal register context standing on the scratch stack. */
        x86_64_sigregs_t pre;
        __builtin_memset(&pre, 0, sizeof(pre));
        pre.rip    = 0x00000000004055AAull;  /* interrupted instruction   */
        pre.rsp    = stack_top;              /* interrupted user rsp      */
        pre.rflags = 0x202u;                 /* IF set, reserved bit      */
        pre.rax    = 0x1111111111111111ull;
        pre.rbx    = 0x2222222222222222ull;
        pre.r12    = 0x3333333333333333ull;
        pre.r15    = 0x4444444444444444ull;

        /* deliver_user: build the frame + reroute regs into the handler. */
        int b_dv = arch_x86_64_proc_signal_deliver_user(&pre,
                                                         syscall64_uwrite_stub,
                                                         NULL);
        log_kv_hex("[x86_64][selftest] M4.2b deliver_user=", (uint64_t)b_dv);
        if (b_dv != SIG) {
            early_console64_write("[x86_64][selftest] FAIL M4.2b deliver_user\n");
            return 62;
        }
        /* After delivery: rip=handler, rdi=signo, rsp below original top. */
        log_kv_hex("[x86_64][selftest] M4.2b handler rip=", pre.rip);
        log_kv_hex("[x86_64][selftest] M4.2b handler rdi=", pre.rdi);
        if (pre.rip != HANDLER || pre.rdi != (uint64_t)SIG) {
            early_console64_write("[x86_64][selftest] FAIL M4.2b reroute\n");
            return 63;
        }
        if (pre.rsp >= stack_top) {
            early_console64_write("[x86_64][selftest] FAIL M4.2b stack not grown\n");
            return 64;
        }
        /* Top of the new stack must hold the restorer return address. */
        uint64_t on_stack_ret = *(uint64_t *)(uintptr_t)pre.rsp;
        log_kv_hex("[x86_64][selftest] M4.2b restorer-on-stack=", on_stack_ret);
        if (on_stack_ret != RESTORER) {
            early_console64_write("[x86_64][selftest] FAIL M4.2b restorer addr\n");
            return 65;
        }

        /* Simulate the handler's `ret`: pop the restorer address, so rsp now
         * points at the sigcontext -- exactly what sigreturn expects. */
        pre.rsp += 8u;

        /* sigreturn: restore the pre-signal context byte-for-byte. */
        int b_sr = arch_x86_64_proc_sigreturn(&pre,
                                              syscall64_uread_stub, NULL);
        log_kv_hex("[x86_64][selftest] M4.2b sigreturn=", (uint64_t)b_sr);
        if (b_sr != 0) {
            early_console64_write("[x86_64][selftest] FAIL M4.2b sigreturn\n");
            return 66;
        }
        log_kv_hex("[x86_64][selftest] M4.2b restored rip=", pre.rip);
        log_kv_hex("[x86_64][selftest] M4.2b restored rsp=", pre.rsp);
        if (pre.rip != 0x00000000004055AAull ||
            pre.rsp != stack_top ||
            pre.rax != 0x1111111111111111ull ||
            pre.rbx != 0x2222222222222222ull ||
            pre.r12 != 0x3333333333333333ull ||
            pre.r15 != 0x4444444444444444ull ||
            pre.rflags != 0x202u) {
            early_console64_write("[x86_64][selftest] FAIL M4.2b context not restored\n");
            return 67;
        }

        /* Restore SIG's disposition to default to leave no residue. */
        b_act.handler = 0u /*SIG_DFL*/; b_act.mask = 0u; b_act.flags = 0u;
        (void)do_syscall(SYS_RT_SIGACTION, (uint64_t)SIG,
                         (uint64_t)(uintptr_t)&b_act, 0u);

        early_console64_write("[x86_64][selftest] M4.2b user-signal trampoline round-trip OK\n");
    }

    log_kv_hex("[x86_64][selftest] dispatch_total=", arch_x86_64_syscall_dispatch_total());
    log_kv_hex("[x86_64][selftest] dispatch_enosys=", arch_x86_64_syscall_dispatch_enosys());
    return 0;
}
