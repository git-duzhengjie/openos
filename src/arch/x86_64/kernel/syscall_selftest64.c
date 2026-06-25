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

static char selftest_buf[256];
static const char selftest_target_path[] = "/hello.txt";

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

    log_kv_hex("[x86_64][selftest] dispatch_total=", arch_x86_64_syscall_dispatch_total());
    log_kv_hex("[x86_64][selftest] dispatch_enosys=", arch_x86_64_syscall_dispatch_enosys());
    return 0;
}
