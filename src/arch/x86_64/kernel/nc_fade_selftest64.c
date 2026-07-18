/*
 * M10.8 selftest: NC TTL / fade-out state machine.
 *
 * Runs entirely off host clock; nc_tick(now_ms) is driven manually so we
 * can prove:
 *   - never-expire path is untouched
 *   - TTL boundary latches fade_start
 *   - alpha ramp is monotonically decreasing during fade
 *   - eviction happens exactly at fade_start + duration
 *   - statistics counters (expired / evicted) advance exactly once each
 */
#include "../include/nc_fade_selftest64.h"
#include "../include/early_console64.h"

#include "../../../kernel/include/notif_center.h"
#include "../../../kernel/include/types.h"

#include <stdint.h>

static void nc_fade_log(const char *s) { early_console64_write(s); }
#define log nc_fade_log

static void nc_fade_memset(void *dst, int val, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    for (uint32_t i = 0; i < n; i++) d[i] = (uint8_t)val;
}
#define memset nc_fade_memset

/* Whitebox: mutable state access for selftest. Read-only public API returns const. */
#define g_nc (*nc_get_state())

/* Access the singleton for white-box validation */
/* g_nc 通过宏射射到 nc_get_state()（白盒 selftest） */

static void clear_all_notifs(void) {
    for (uint32_t i = 0; i < NC_MAX_NOTIFICATIONS; i++) {
        memset(&g_nc.notifs[i], 0, sizeof(g_nc.notifs[i]));
    }
    g_nc.notif_count = 0;
    g_nc.stat_notif_shown   = 0;
    g_nc.stat_notif_hidden  = 0;
    g_nc.stat_notif_expired = 0;
    g_nc.stat_notif_evicted = 0;
    g_nc.now_ms = 0;
}

/* Find the first active notification with given app name; returns index or -1 */
static int find_by_app(const char *app) {
    for (uint32_t i = 0; i < NC_MAX_NOTIFICATIONS; i++) {
        if (!g_nc.notifs[i].active) continue;
        int match = 1;
        for (int k = 0; k < NC_LABEL_MAX; k++) {
            if (g_nc.notifs[i].app_name[k] != app[k]) { match = 0; break; }
            if (app[k] == '\0') break;
        }
        if (match) return (int)i;
    }
    return -1;
}

bool arch_x86_64_nc_fade_selftest_run(void) {
    log("[nc-fade-selftest] start\n");

    nc_init(1920, 1080);
    clear_all_notifs();

    /* ---------- Stage 1: ttl=0 never expires ---------- */
    {
        nc_push_notification_ttl("neverExpire", "t", "c", 0, 0, 0);
        nc_tick(1000000);  /* huge future time */
        int idx = find_by_app("neverExpire");
        if (idx < 0) { log("[nc-fade-selftest] FAIL stage1: notif gone\n"); return false; }
        if (g_nc.notifs[idx].alpha != 255) {
            log("[nc-fade-selftest] FAIL stage1: alpha changed for ttl=0\n"); return false;
        }
        if (g_nc.stat_notif_expired != 0) {
            log("[nc-fade-selftest] FAIL stage1: stat_notif_expired should stay 0\n"); return false;
        }
    }
    log("[nc-fade-selftest] stage1 OK (ttl=0 immortal)\n");

    /* ---------- Stage 2: before ttl: no fade ---------- */
    clear_all_notifs();
    {
        /* start at now=0; ttl=1000ms; duration=200ms */
        nc_push_notification_ttl("preTTL", "t", "c", 0, 1000, 200);
        nc_tick(500);  /* mid-life, not yet expired */
        int idx = find_by_app("preTTL");
        if (idx < 0) { log("[nc-fade-selftest] FAIL stage2: notif gone\n"); return false; }
        if (g_nc.notifs[idx].alpha != 255 || g_nc.notifs[idx].fade_start_ms != 0) {
            log("[nc-fade-selftest] FAIL stage2: fade started too early\n"); return false;
        }
        if (g_nc.stat_notif_expired != 0) {
            log("[nc-fade-selftest] FAIL stage2: expired counter fired early\n"); return false;
        }
    }
    log("[nc-fade-selftest] stage2 OK (pre-TTL untouched)\n");

    /* ---------- Stage 3: at ttl boundary: fade_start latched ---------- */
    {
        /* g_nc.now_ms is 500 (from previous tick). expire_ms was 0 + 1000 = 1000.
         * push at now_ms=0 means expire_ms=1000. Tick to exactly boundary. */
        nc_tick(1000);
        int idx = find_by_app("preTTL");
        if (idx < 0) { log("[nc-fade-selftest] FAIL stage3: notif gone\n"); return false; }
        if (g_nc.notifs[idx].fade_start_ms != 1000) {
            log("[nc-fade-selftest] FAIL stage3: fade_start_ms not latched\n"); return false;
        }
        if (g_nc.stat_notif_expired != 1) {
            log("[nc-fade-selftest] FAIL stage3: stat_notif_expired != 1\n"); return false;
        }
    }
    log("[nc-fade-selftest] stage3 OK (TTL boundary latched)\n");

    /* ---------- Stage 4: mid-fade monotone decrease ---------- */
    {
        int idx = find_by_app("preTTL");
        if (idx < 0) { log("[nc-fade-selftest] FAIL stage4: notif gone\n"); return false; }
        /* duration=200, start=1000: at t=1050 (25%) alpha ~ 191; at t=1150 (75%) alpha ~ 63 */
        nc_tick(1050);
        uint8_t a1 = g_nc.notifs[idx].alpha;
        nc_tick(1150);
        uint8_t a2 = g_nc.notifs[idx].alpha;
        if (!(a1 < 255 && a2 < a1 && a2 > 0)) {
            log("[nc-fade-selftest] FAIL stage4: alpha ramp broken\n"); return false;
        }
    }
    log("[nc-fade-selftest] stage4 OK (alpha ramp monotone)\n");

    /* ---------- Stage 5: end of fade -> evict ---------- */
    {
        /* fade_start=1000, duration=200, so eviction at t=1200 */
        nc_tick(1200);
        int idx = find_by_app("preTTL");
        if (idx >= 0) {
            log("[nc-fade-selftest] FAIL stage5: notif should be evicted\n"); return false;
        }
        if (g_nc.stat_notif_evicted != 1) {
            log("[nc-fade-selftest] FAIL stage5: evicted counter != 1\n"); return false;
        }
    }
    log("[nc-fade-selftest] stage5 OK (auto-eviction)\n");

    /* ---------- Stage 6: mixed batch survivability ---------- */
    clear_all_notifs();
    {
        nc_push_notification_ttl("keep", "t", "c", 0, 0, 0);          /* immortal */
        nc_push_notification_ttl("die",  "t", "c", 0, 100, 100);      /* die at 200 */
        nc_tick(50);
        if (g_nc.notif_count != 2) { log("[nc-fade-selftest] FAIL stage6a: count!=2\n"); return false; }
        nc_tick(300);  /* well past die's fade end */
        if (find_by_app("keep") < 0) {
            log("[nc-fade-selftest] FAIL stage6: immortal disappeared\n"); return false;
        }
        if (find_by_app("die") >= 0) {
            log("[nc-fade-selftest] FAIL stage6: mortal survived\n"); return false;
        }
        if (g_nc.notif_count != 1) {
            log("[nc-fade-selftest] FAIL stage6: count did not decrement\n"); return false;
        }
    }
    log("[nc-fade-selftest] stage6 OK (mixed batch)\n");

    /* ---------- Stage 7: idempotent tick ---------- */
    clear_all_notifs();
    {
        nc_push_notification_ttl("idem", "t", "c", 0, 100, 200);
        nc_tick(150);   /* boundary crossed, fade_start latched */
        uint32_t exp1 = g_nc.stat_notif_expired;
        uint8_t  a1   = g_nc.notifs[find_by_app("idem")].alpha;
        nc_tick(150);   /* same time again */
        uint32_t exp2 = g_nc.stat_notif_expired;
        uint8_t  a2   = g_nc.notifs[find_by_app("idem")].alpha;
        if (exp2 != exp1) {
            log("[nc-fade-selftest] FAIL stage7: expired double-counted\n"); return false;
        }
        if (a1 != a2) {
            log("[nc-fade-selftest] FAIL stage7: alpha wobbled on same-tick\n"); return false;
        }
    }
    log("[nc-fade-selftest] stage7 OK (idempotent tick)\n");

    /* ---------- Stage 8: custom duration override ---------- */
    clear_all_notifs();
    {
        /* Short duration = 50ms; ttl=100ms -> full eviction at 150ms */
        nc_push_notification_ttl("short", "t", "c", 0, 100, 50);
        nc_tick(120);   /* mid-fade at 40% */
        int idx = find_by_app("short");
        if (idx < 0) { log("[nc-fade-selftest] FAIL stage8: notif gone early\n"); return false; }
        if (g_nc.notifs[idx].fade_duration_ms != 50) {
            log("[nc-fade-selftest] FAIL stage8: duration override lost\n"); return false;
        }
        if (g_nc.notifs[idx].alpha == 0 || g_nc.notifs[idx].alpha == 255) {
            log("[nc-fade-selftest] FAIL stage8: alpha not in fade range\n"); return false;
        }
        nc_tick(150);   /* fade end */
        if (find_by_app("short") >= 0) {
            log("[nc-fade-selftest] FAIL stage8: short-duration should evict at 150\n"); return false;
        }
    }
    log("[nc-fade-selftest] stage8 OK (duration override)\n");

    log("[nc-fade-selftest] PASS\n");
    return true;
}
