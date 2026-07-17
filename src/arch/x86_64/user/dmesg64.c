/*
 * dmesg(1) -- M6.12 userspace kernel log viewer.
 *
 * Usage:
 *   dmesg               dump full ring buffer
 *   dmesg -n N          show last N entries
 *   dmesg -s            show statistics
 *   dmesg -c            clear ring buffer (requires uid=0)
 *   dmesg -h            help
 *
 * Uses SYS_KLOG (487) to fetch pre-formatted log lines from the kernel.
 */
#include "openos64.h"

static openos64_size_t d_strlen(const char *s) {
    openos64_size_t n = 0; while (s && s[n]) n++; return n;
}
static void d_puts(const char *s) {
    openos64_write(OPENOS64_STDOUT_FILENO, s, d_strlen(s));
}
static int d_streq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (*a == 0 && *b == 0);
}

/* Simple positive integer parser (returns 0 on bad input). */
static unsigned d_atou(const char *s) {
    unsigned v = 0;
    while (s && *s >= '0' && *s <= '9') { v = v * 10 + (unsigned)(*s - '0'); s++; }
    return v;
}
static void d_utoa(unsigned v, char *out) {
    char tmp[12]; unsigned n = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    for (unsigned i = 0; i < n; ++i) out[i] = tmp[n - 1 - i];
    out[n] = 0;
}
static void d_print_kv(const char *k, unsigned v) {
    char nbuf[16];
    d_utoa(v, nbuf);
    d_puts("  "); d_puts(k); d_puts(" = "); d_puts(nbuf); d_puts("\n");
}

static int cmd_help(void) {
    d_puts(
        "dmesg - print or control the kernel ring buffer\n"
        "usage: dmesg [-n N | -s | -c | -h]\n"
        "  (no args)   dump full log\n"
        "  -n N        show last N entries\n"
        "  -s          show ring buffer statistics\n"
        "  -c          clear ring buffer (uid=0 only)\n"
        "  -h          this help\n");
    return 0;
}

static int cmd_stats(void) {
    openos64_klog_stats_t st;
    long r = openos64_klog(OPENOS64_KLOG_STATS, 0, &st, (unsigned)sizeof(st));
    if (r < 0) { d_puts("dmesg: SYS_KLOG stats failed\n"); return 1; }
    d_puts("klog statistics:\n");
    d_print_kv("seq_next     ", st.seq_next);
    d_print_kv("seq_oldest   ", st.seq_oldest);
    d_print_kv("total_emitted", st.total_emitted);
    d_print_kv("total_dropped", st.total_dropped);
    d_print_kv("buffer_used  ", st.buffer_used);
    d_print_kv("buffer_size  ", st.buffer_size);
    return 0;
}

static int cmd_clear(void) {
    long r = openos64_klog(OPENOS64_KLOG_CLEAR, 0, 0, 0);
    if (r < 0) { d_puts("dmesg: clear failed (need uid=0?)\n"); return 1; }
    d_puts("dmesg: ring cleared\n");
    return 0;
}

/* Dump helper: paginate reads to fit our 8KB user-side buffer. */
static int dump_from_seq(unsigned start_seq) {
    static char buf[8192];
    unsigned seq = start_seq;
    for (int rounds = 0; rounds < 64; ++rounds) {
        long r = openos64_klog(OPENOS64_KLOG_READ_FROM, seq, buf, (unsigned)sizeof(buf) - 1);
        if (r <= 0) break;
        buf[r] = 0;
        openos64_write(OPENOS64_STDOUT_FILENO, buf, (openos64_size_t)r);
        /* Find the highest seq we just printed to advance. Format is:
         *   <prio>[SEQ] MSG\n   -- parse each line's SEQ. */
        unsigned last_seq = seq;
        for (long i = 0; i < r; ++i) {
            if (buf[i] == '[') {
                long j = i + 1; unsigned v = 0;
                while (j < r && buf[j] >= '0' && buf[j] <= '9') {
                    v = v * 10 + (unsigned)(buf[j] - '0'); j++;
                }
                if (j < r && buf[j] == ']') last_seq = v;
                i = j;
            }
        }
        if (last_seq + 1 == seq) break; /* no progress */
        seq = last_seq + 1;
    }
    return 0;
}

static int cmd_all(void) { return dump_from_seq(0); }

static int cmd_tail(unsigned n) {
    static char buf[8192];
    long r = openos64_klog(OPENOS64_KLOG_READ_TAIL, n, buf, (unsigned)sizeof(buf) - 1);
    if (r < 0) { d_puts("dmesg: read_tail failed\n"); return 1; }
    buf[r] = 0;
    openos64_write(OPENOS64_STDOUT_FILENO, buf, (openos64_size_t)r);
    return 0;
}

int openos64_main(int argc, char **argv, char **envp);
int openos64_main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc <= 1) return cmd_all();
    if (d_streq(argv[1], "-h") || d_streq(argv[1], "--help")) return cmd_help();
    if (d_streq(argv[1], "-s")) return cmd_stats();
    if (d_streq(argv[1], "-c")) return cmd_clear();
    if (d_streq(argv[1], "-n")) {
        if (argc < 3) { d_puts("dmesg: -n needs a count\n"); return 1; }
        unsigned n = d_atou(argv[2]);
        if (n == 0) n = 20;
        return cmd_tail(n);
    }
    d_puts("dmesg: unknown option (try -h)\n");
    return 1;
}
