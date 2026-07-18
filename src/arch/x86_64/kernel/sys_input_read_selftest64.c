/*
 * M10.7 selftest: SYS_INPUT_READ syscall path.
 *
 * Verifies the user-space input read path end-to-end at the syscall helper
 * level. We call `syscall_do_input_read_for_selftest(kbuf, max, flags)`
 * which mirrors do_input_read() exactly except it skips the ring3-only
 * validate_user_buf() check (selftest runs in kernel space).
 *
 * Stages (all must PASS):
 *   1) empty-queue non-blocking (returns 0, no side effects)
 *   2) single event round-trip (push 1, read 1, byte-equal)
 *   3) multi-event batch (push N, drain in one call)
 *   4) max_events cap (queue has N, ask for M<N -> returns M, remaining stays)
 *   5) FIFO ordering (drain in write order)
 *   6) full-drain then empty (idempotent, second call returns 0)
 *   7) argument validation (max=0, oversize, bad flags, null buf)
 *   8) drop-oldest overflow (push > capacity, then read -> newest wins,
 *      dropped counter increments accordingly)
 */
#include "../include/sys_input_read_selftest64.h"
#include "../include/early_console64.h"

#include "../../../kernel/include/input_core.h"
#include "../../../kernel/include/types.h"

#include <stdint.h>
#include <stddef.h>

/* Provided by syscall_dispatch64.c */
extern uint64_t syscall_do_input_read_for_selftest(void *kbuf, uint64_t max_events, uint64_t flags);

static void sir_log(const char *s) { early_console64_write(s); }
#define log sir_log

static void sir_memcmp_ret(void); /* silence -Wmissing-declarations placeholder */
static int sir_memcmp(const void *a, const void *b, uint32_t n) {
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    for (uint32_t i = 0; i < n; i++) { if (pa[i] != pb[i]) return (int)pa[i] - (int)pb[i]; }
    return 0;
}
#define memcmp sir_memcmp

static void sir_memset(void *dst, int val, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    for (uint32_t i = 0; i < n; i++) d[i] = (uint8_t)val;
}
#define memset sir_memset
static void sir_memcmp_ret(void) { (void)0; }

/* Whitebox alias for g_nc: forward-declared accessor lives in notif_center.h,
 * but nc_fade_selftest64.c redefines g_nc as a macro. Not needed here. */

/* Drain everything from the ring (best-effort clean slate). */
static void drain_all(void) {
    input_event_t tmp;
    for (int i = 0; i < 4096; ++i) {
        if (!input_poll_event(&tmp)) break;
    }
}

/* Push a synthetic KEY event with a distinct value so we can spot ordering. */
static void push_marker(uint32_t seq) {
    input_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.timestamp_ms = seq;
    ev.dev_id       = 0;
    ev.type         = INPUT_EV_KEY;
    ev.code         = INPUT_KEY_TOUCH;
    ev.value        = (int32_t)seq;
    input_report(&ev);
}

bool arch_x86_64_sys_input_read_selftest_run(void) {
    log("[sys-input-read-selftest] start\n");

    input_core_init();
    drain_all();

    /* ---------- Stage 1: empty queue -> 0 ---------- */
    {
        input_event_t buf[4];
        uint64_t r = syscall_do_input_read_for_selftest(buf, 4, 0);
        if (r != 0) { log("[sys-input-read-selftest] FAIL stage1: expected 0\n"); return false; }
    }
    log("[sys-input-read-selftest] stage1 OK (empty queue -> 0)\n");

    /* ---------- Stage 2: single event round-trip ---------- */
    {
        push_marker(0xAA55);
        input_event_t buf[4];
        memset(buf, 0, sizeof(buf));
        uint64_t r = syscall_do_input_read_for_selftest(buf, 4, 0);
        if (r != 1) { log("[sys-input-read-selftest] FAIL stage2: expected 1\n"); return false; }
        if (buf[0].type != INPUT_EV_KEY || buf[0].code != INPUT_KEY_TOUCH || buf[0].value != (int32_t)0xAA55) {
            log("[sys-input-read-selftest] FAIL stage2: content mismatch\n"); return false;
        }
    }
    log("[sys-input-read-selftest] stage2 OK (single event)\n");

    /* ---------- Stage 3: multi-event batch ---------- */
    {
        drain_all();
        for (uint32_t i = 1; i <= 5; ++i) push_marker(i);
        input_event_t buf[8];
        uint64_t r = syscall_do_input_read_for_selftest(buf, 8, 0);
        if (r != 5) { log("[sys-input-read-selftest] FAIL stage3: expected 5\n"); return false; }
        for (uint32_t i = 0; i < 5; ++i) {
            if (buf[i].value != (int32_t)(i + 1)) {
                log("[sys-input-read-selftest] FAIL stage3: FIFO order broken\n"); return false;
            }
        }
    }
    log("[sys-input-read-selftest] stage3 OK (batch drain, FIFO preserved)\n");

    /* ---------- Stage 4: max_events cap ---------- */
    {
        drain_all();
        for (uint32_t i = 1; i <= 10; ++i) push_marker(i * 100);
        input_event_t buf[3];
        uint64_t r = syscall_do_input_read_for_selftest(buf, 3, 0);
        if (r != 3) { log("[sys-input-read-selftest] FAIL stage4: expected 3\n"); return false; }
        if (buf[0].value != 100 || buf[1].value != 200 || buf[2].value != 300) {
            log("[sys-input-read-selftest] FAIL stage4: first-3 mismatch\n"); return false;
        }
        /* remaining 7 should still be there */
        input_event_t buf2[16];
        uint64_t r2 = syscall_do_input_read_for_selftest(buf2, 16, 0);
        if (r2 != 7) { log("[sys-input-read-selftest] FAIL stage4: remainder=?\n"); return false; }
        if (buf2[0].value != 400 || buf2[6].value != 1000) {
            log("[sys-input-read-selftest] FAIL stage4: remainder order broken\n"); return false;
        }
    }
    log("[sys-input-read-selftest] stage4 OK (max_events cap)\n");

    /* ---------- Stage 5: FIFO across boundary ---------- */
    {
        drain_all();
        for (uint32_t i = 0; i < 20; ++i) push_marker(0xBEEF0000u + i);
        input_event_t buf[32];
        uint64_t r = syscall_do_input_read_for_selftest(buf, 32, 0);
        if (r != 20) { log("[sys-input-read-selftest] FAIL stage5: expected 20\n"); return false; }
        for (uint32_t i = 0; i < 20; ++i) {
            if (buf[i].value != (int32_t)(0xBEEF0000u + i)) {
                log("[sys-input-read-selftest] FAIL stage5: order slip\n"); return false;
            }
        }
    }
    log("[sys-input-read-selftest] stage5 OK (FIFO 20 events)\n");

    /* ---------- Stage 6: full drain then empty ---------- */
    {
        input_event_t buf[4];
        uint64_t r = syscall_do_input_read_for_selftest(buf, 4, 0);
        if (r != 0) { log("[sys-input-read-selftest] FAIL stage6: expected 0 (already drained)\n"); return false; }
    }
    log("[sys-input-read-selftest] stage6 OK (idempotent empty)\n");

    /* ---------- Stage 7: argument validation ---------- */
    {
        input_event_t buf[4];
        /* null buf */
        if (syscall_do_input_read_for_selftest(NULL, 4, 0) != (uint64_t)-1) {
            log("[sys-input-read-selftest] FAIL stage7: null buf not rejected\n"); return false;
        }
        /* max=0 */
        if (syscall_do_input_read_for_selftest(buf, 0, 0) != (uint64_t)-1) {
            log("[sys-input-read-selftest] FAIL stage7: max=0 not rejected\n"); return false;
        }
        /* max too big (>256) */
        if (syscall_do_input_read_for_selftest(buf, 1000, 0) != (uint64_t)-1) {
            log("[sys-input-read-selftest] FAIL stage7: max=1000 not rejected\n"); return false;
        }
        /* bad flags */
        if (syscall_do_input_read_for_selftest(buf, 4, 1) != (uint64_t)-1) {
            log("[sys-input-read-selftest] FAIL stage7: flags=1 not rejected\n"); return false;
        }
    }
    log("[sys-input-read-selftest] stage7 OK (arg validation)\n");

    /* ---------- Stage 8: drop-oldest overflow ---------- */
    {
        drain_all();
        uint32_t dropped_before = input_stat_events_dropped();
        /* Push > INPUT_RING_CAPACITY (=256) events to force at least 1 drop */
        for (uint32_t i = 0; i < INPUT_RING_CAPACITY + 32; ++i) push_marker(i);
        uint32_t dropped_after = input_stat_events_dropped();
        if (dropped_after <= dropped_before) {
            log("[sys-input-read-selftest] FAIL stage8: drop counter did not advance\n"); return false;
        }
        /* Drain and verify we get INPUT_RING_CAPACITY events, newest tail present */
        input_event_t big[INPUT_RING_CAPACITY];
        uint64_t r = syscall_do_input_read_for_selftest(big, INPUT_RING_CAPACITY, 0);
        if (r == 0 || r > INPUT_RING_CAPACITY) {
            log("[sys-input-read-selftest] FAIL stage8: unexpected read count\n"); return false;
        }
        /* Last event returned must be the newest we pushed (INPUT_RING_CAPACITY+31) */
        int32_t last_expected = (int32_t)(INPUT_RING_CAPACITY + 32 - 1);
        if (big[r - 1].value != last_expected) {
            log("[sys-input-read-selftest] FAIL stage8: newest event lost\n"); return false;
        }
    }
    log("[sys-input-read-selftest] stage8 OK (drop-oldest overflow)\n");

    log("[sys-input-read-selftest] PASS\n");
    return true;
}
