/*
 * klog_selftest64.c -- M6.12 klog selftest.
 *
 * Verifies the ring buffer, sequence monotonicity, stats, tail read,
 * seq-based resume read, and wrap eviction. The test emits its own
 * marker entries against the live ring; it does not attempt to restore
 * the pre-test state because klog is monotonic by design.
 */
#include "../include/klog_selftest64.h"
#include "../include/klog64.h"
#include "../include/early_console64.h"

#include <stdint.h>
#include <stddef.h>

static void kt_log(const char *s) { early_console64_write(s); }

/* Local strlen / strstr / itoa to keep this TU self-contained. */
static size_t kt_strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
static const char *kt_strstr(const char *hay, size_t haylen, const char *needle) {
    size_t nl = kt_strlen(needle);
    if (nl == 0 || nl > haylen) return 0;
    for (size_t i = 0; i + nl <= haylen; ++i) {
        size_t j = 0;
        while (j < nl && hay[i + j] == needle[j]) j++;
        if (j == nl) return hay + i;
    }
    return 0;
}

bool arch_x86_64_klog_selftest_run(void)
{
    bool ok = true;
    #define KT_CHECK(cond, msg)                                        \
        do {                                                           \
            if (!(cond)) {                                             \
                kt_log("[x86_64][klog-selftest] FAIL: " msg "\n");     \
                ok = false;                                            \
            }                                                          \
        } while (0)

    /* --- Stage 1: monotonic seq. Emit 3 markers and read back. --- */
    klog_stats_t st0; klog_get_stats(&st0);
    klog_emit(KLOG_INFO, KLOG_FAC_KERNEL, "KT-MARK-A");
    klog_emit(KLOG_INFO, KLOG_FAC_KERNEL, "KT-MARK-B");
    klog_emit(KLOG_INFO, KLOG_FAC_KERNEL, "KT-MARK-C");
    klog_stats_t st1; klog_get_stats(&st1);
    KT_CHECK(st1.seq_next == st0.seq_next + 3, "stage1 seq_next should +3");
    KT_CHECK(st1.total_emitted >= st0.total_emitted + 3, "stage1 emitted counter");

    /* --- Stage 2: tail read returns markers. --- */
    {
        static char buf[2048];
        size_t n = 0;
        bool r = klog_read_tail(8, buf, sizeof(buf), &n);
        KT_CHECK(r, "stage2 read_tail should succeed");
        KT_CHECK(n > 0, "stage2 read_tail bytes>0");
        KT_CHECK(kt_strstr(buf, n, "KT-MARK-A") != 0, "stage2 tail contains marker A");
        KT_CHECK(kt_strstr(buf, n, "KT-MARK-B") != 0, "stage2 tail contains marker B");
        KT_CHECK(kt_strstr(buf, n, "KT-MARK-C") != 0, "stage2 tail contains marker C");
    }

    /* --- Stage 3: read_from(seq) resumes correctly. --- */
    {
        /* seq of KT-MARK-B = st0.seq_next + 1. */
        uint32_t seq_b = st0.seq_next + 1;
        static char buf[2048];
        size_t n = 0;
        bool r = klog_read_from(seq_b, buf, sizeof(buf), &n);
        KT_CHECK(r, "stage3 read_from should succeed");
        KT_CHECK(kt_strstr(buf, n, "KT-MARK-B") != 0, "stage3 contains B");
        KT_CHECK(kt_strstr(buf, n, "KT-MARK-C") != 0, "stage3 contains C");
        KT_CHECK(kt_strstr(buf, n, "KT-MARK-A") == 0, "stage3 does NOT contain A");
    }

    /* --- Stage 4: stats sanity. --- */
    {
        klog_stats_t st; klog_get_stats(&st);
        KT_CHECK(st.buffer_size == KLOG_BUFFER_SIZE, "stage4 buffer_size");
        KT_CHECK(st.seq_oldest <= st.seq_next, "stage4 seq ordering");
        KT_CHECK(st.buffer_used <= st.buffer_size, "stage4 buffer_used bound");
    }

    /* --- Stage 5: wrap eviction. Emit enough to force wrap and check that
     *  drop counter increased and seq_oldest advanced. Each entry is
     *  ~64 bytes with a 32-char payload; emit 2000 to guarantee wrap. */
    {
        klog_stats_t before; klog_get_stats(&before);
        for (int i = 0; i < 2000; ++i) {
            klog_emit(KLOG_INFO, KLOG_FAC_KERNEL,
                      "KT-FILL-XXXXXXXXXXXXXXXXXXXXXXXXXXXX");
        }
        klog_stats_t after; klog_get_stats(&after);
        KT_CHECK(after.total_dropped > before.total_dropped,
                 "stage5 drops should increase after wrap");
        KT_CHECK(after.seq_oldest > before.seq_oldest,
                 "stage5 seq_oldest should advance");
        KT_CHECK(after.seq_next  == before.seq_next + 2000,
                 "stage5 seq_next should advance by exactly 2000");
    }

    /* --- Stage 6: clear resets ring but preserves seq_next. --- */
    {
        klog_stats_t before; klog_get_stats(&before);
        klog_clear();
        klog_stats_t after; klog_get_stats(&after);
        KT_CHECK(after.buffer_used == 0, "stage6 buffer_used==0 after clear");
        KT_CHECK(after.seq_oldest == before.seq_next,
                 "stage6 seq_oldest==seq_next after clear");
        KT_CHECK(after.seq_next == before.seq_next,
                 "stage6 seq_next preserved across clear");
    }

    if (ok) kt_log("[x86_64][klog-selftest] PASS\n");
    #undef KT_CHECK
    return ok;
}
