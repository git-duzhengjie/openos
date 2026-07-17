/*
 * klog64.c -- M6.12 kernel ring buffer logger.
 *
 * Ring buffer layout (byte-level):
 *   [entry_0 hdr][msg_0][NUL][pad->align8][entry_1 hdr][msg_1][NUL]...
 *
 * We store entries contiguously with 8-byte alignment. When the buffer
 * wraps we drop the oldest entries in FIFO order (advancing head), so
 * a slow reader may see 'holes' in the seq sequence but each returned
 * entry is always complete and valid.
 *
 * Concurrency: irq-off + spinlock (single-CPU-safe). SMP-safe because
 * the spinlock uses xchg (see klog_lock() below).
 */
#include "klog64.h"
#include "early_console64.h"

/* ---------------------- helpers ---------------------- */
static inline size_t klog_align8(size_t x) { return (x + 7u) & ~((size_t)7u); }
static inline uint64_t klog_rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/* ---------------------- state ---------------------- */
static uint8_t   g_klog_buf[KLOG_BUFFER_SIZE] __attribute__((aligned(8)));
static uint32_t  g_klog_head;      /* byte offset of oldest entry */
static uint32_t  g_klog_tail;      /* byte offset of next free slot */
static uint32_t  g_klog_used;      /* bytes currently in use */
static uint32_t  g_klog_seq_next;  /* next sequence number */
static uint32_t  g_klog_seq_oldest;/* seq of entry at g_klog_head (== seq_next if empty) */
static uint32_t  g_klog_emitted;
static uint32_t  g_klog_dropped;
static volatile uint32_t g_klog_lock;
static int       g_klog_ready;
static int       g_klog_reentry;   /* guard against klog_emit recursing through early_console */

/* ---------------------- irq-off spinlock ---------------------- */
static inline uint64_t klog_lock(void) {
    uint64_t rflags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(rflags) :: "memory");
    while (__sync_lock_test_and_set(&g_klog_lock, 1)) {
        __asm__ volatile("pause");
    }
    return rflags;
}
static inline void klog_unlock(uint64_t rflags) {
    __sync_lock_release(&g_klog_lock);
    if (rflags & (1ull << 9)) __asm__ volatile("sti");
}

/* ---------------------- ring accessors ---------------------- */
static void klog_pop_oldest_unlocked(void) {
    /* Precondition: g_klog_used >= sizeof(klog_entry_t)+1 */
    klog_entry_t *e = (klog_entry_t *)(g_klog_buf + g_klog_head);
    size_t total = klog_align8(sizeof(klog_entry_t) + e->len + 1);
    g_klog_head = (uint32_t)((g_klog_head + total) % KLOG_BUFFER_SIZE);
    g_klog_used -= (uint32_t)total;
    g_klog_seq_oldest++;
    g_klog_dropped++;
    /* invalidate magic to be safe */
    e->magic = 0;
}

static void klog_write_bytes_unlocked(uint32_t off, const void *src, size_t n) {
    const uint8_t *p = (const uint8_t *)src;
    while (n) {
        size_t chunk = KLOG_BUFFER_SIZE - off;
        if (chunk > n) chunk = n;
        __builtin_memcpy(g_klog_buf + off, p, chunk);
        off = (uint32_t)((off + chunk) % KLOG_BUFFER_SIZE);
        p += chunk;
        n -= chunk;
    }
}

static void klog_read_bytes(uint32_t off, void *dst, size_t n) {
    uint8_t *p = (uint8_t *)dst;
    while (n) {
        size_t chunk = KLOG_BUFFER_SIZE - off;
        if (chunk > n) chunk = n;
        __builtin_memcpy(p, g_klog_buf + off, chunk);
        off = (uint32_t)((off + chunk) % KLOG_BUFFER_SIZE);
        p += chunk;
        n -= chunk;
    }
}

/* ---------------------- init ---------------------- */
void klog_init(void) {
    for (size_t i = 0; i < sizeof(g_klog_buf); ++i) g_klog_buf[i] = 0;
    g_klog_head = g_klog_tail = g_klog_used = 0;
    g_klog_seq_next = g_klog_seq_oldest = 0;
    g_klog_emitted = g_klog_dropped = 0;
    g_klog_lock = 0;
    g_klog_reentry = 0;
    g_klog_ready = 1;
}

/* ---------------------- emit ---------------------- */
void klog_emit(uint8_t level, uint8_t facility, const char *msg) {
    if (!g_klog_ready || !msg) return;

    /* Determine message length (bounded to avoid runaway). */
    size_t msg_len = 0;
    while (msg[msg_len] && msg_len < 1024) msg_len++;
    if (msg_len == 0) return;
    /* Trim trailing newline for cleaner readback (early_console adds them). */
    while (msg_len > 0 && (msg[msg_len - 1] == '\n' || msg[msg_len - 1] == '\r')) msg_len--;
    if (msg_len == 0) return;

    /* One entry cannot exceed half the buffer (sanity clamp). */
    if (msg_len > (KLOG_BUFFER_SIZE / 2)) msg_len = KLOG_BUFFER_SIZE / 2;

    size_t total = klog_align8(sizeof(klog_entry_t) + msg_len + 1);

    uint64_t rf = klog_lock();
    if (g_klog_reentry) { klog_unlock(rf); return; }
    g_klog_reentry = 1;

    /* Evict oldest entries until we have room. */
    while (g_klog_used + total > KLOG_BUFFER_SIZE) {
        if (g_klog_used == 0) break; /* shouldn't happen: entry > buffer */
        klog_pop_oldest_unlocked();
    }

    if (g_klog_used + total <= KLOG_BUFFER_SIZE) {
        klog_entry_t hdr;
        hdr.magic     = KLOG_ENTRY_MAGIC;
        hdr.seq       = g_klog_seq_next;
        hdr.timestamp = klog_rdtsc();
        hdr.prio      = (uint16_t)KLOG_MAKE_PRIO(level, facility);
        hdr.len       = (uint16_t)msg_len;

        klog_write_bytes_unlocked(g_klog_tail, &hdr, sizeof(hdr));
        uint32_t msg_off = (uint32_t)((g_klog_tail + sizeof(hdr)) % KLOG_BUFFER_SIZE);
        klog_write_bytes_unlocked(msg_off, msg, msg_len);
        uint32_t nul_off = (uint32_t)((msg_off + msg_len) % KLOG_BUFFER_SIZE);
        uint8_t nul = 0;
        klog_write_bytes_unlocked(nul_off, &nul, 1);

        g_klog_tail = (uint32_t)((g_klog_tail + total) % KLOG_BUFFER_SIZE);
        g_klog_used += (uint32_t)total;
        g_klog_seq_next++;
        g_klog_emitted++;
    }

    g_klog_reentry = 0;
    klog_unlock(rf);
}

/* ---------------------- read ---------------------- */
/* Helpers for user-facing text formatting. Output format per entry:
 *   "[NNNN.NNN] <PRIO> MSG\n"
 * We keep it minimal and printable so dmesg(1) just spools bytes.
 */
static size_t klog_u32_to_dec(uint32_t v, char *out) {
    char tmp[12]; size_t n = 0;
    if (v == 0) { out[0] = '0'; return 1; }
    while (v) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    for (size_t i = 0; i < n; ++i) out[i] = tmp[n - 1 - i];
    return n;
}

static size_t klog_format_entry(const klog_entry_t *hdr, const char *msg,
                                char *out, size_t outsz) {
    if (outsz < 32) return 0;
    size_t i = 0;
    out[i++] = '<';
    /* prio in decimal (0..63): */
    uint32_t prio = hdr->prio;
    i += klog_u32_to_dec(prio, out + i);
    out[i++] = '>';
    out[i++] = '[';
    i += klog_u32_to_dec(hdr->seq, out + i);
    out[i++] = ']';
    out[i++] = ' ';
    size_t remain = (outsz > i + 2) ? (outsz - i - 2) : 0;
    size_t take = hdr->len;
    if (take > remain) take = remain;
    for (size_t k = 0; k < take; ++k) out[i++] = msg[k];
    out[i++] = '\n';
    return i;
}

/* Walk entries starting at head_offset, calling emit for each entry
 * whose seq >= start_seq. Stops when buf is full or we've walked
 * everything. Returns total bytes written and, indirectly, count. */
static size_t klog_walk_and_dump(uint32_t start_seq, uint32_t max_count,
                                 char *out, size_t outsz) {
    if (outsz == 0) return 0;
    uint32_t off = g_klog_head;
    uint32_t remaining = g_klog_used;
    size_t written = 0;
    uint32_t emitted = 0;

    while (remaining >= sizeof(klog_entry_t) && written < outsz) {
        klog_entry_t hdr;
        klog_read_bytes(off, &hdr, sizeof(hdr));
        if (hdr.magic != KLOG_ENTRY_MAGIC) break;
        size_t total = klog_align8(sizeof(klog_entry_t) + hdr.len + 1);
        if (total > remaining) break;

        if (hdr.seq >= start_seq) {
            /* Extract message into a stack buffer for formatting. */
            char msgbuf[512];
            size_t take = hdr.len;
            if (take > sizeof(msgbuf) - 1) take = sizeof(msgbuf) - 1;
            uint32_t msg_off = (uint32_t)((off + sizeof(klog_entry_t)) % KLOG_BUFFER_SIZE);
            klog_read_bytes(msg_off, msgbuf, take);
            msgbuf[take] = 0;
            hdr.len = (uint16_t)take;

            char line[600];
            size_t line_len = klog_format_entry(&hdr, msgbuf, line, sizeof(line));
            if (written + line_len > outsz) break;
            for (size_t k = 0; k < line_len; ++k) out[written++] = line[k];
            emitted++;
            if (max_count && emitted >= max_count) break;
        }

        off = (uint32_t)((off + total) % KLOG_BUFFER_SIZE);
        remaining -= (uint32_t)total;
    }
    return written;
}

bool klog_read_from(uint32_t start_seq, char *buf, size_t bufsize, size_t *out_len) {
    if (!buf || bufsize == 0) return false;
    uint64_t rf = klog_lock();
    size_t n = klog_walk_and_dump(start_seq, 0, buf, bufsize);
    klog_unlock(rf);
    if (out_len) *out_len = n;
    return true;
}

bool klog_read_tail(uint32_t count, char *buf, size_t bufsize, size_t *out_len) {
    if (!buf || bufsize == 0) return false;
    uint64_t rf = klog_lock();
    /* Compute start_seq so that we return roughly the last `count` entries. */
    uint32_t total_available = g_klog_seq_next - g_klog_seq_oldest;
    uint32_t start = g_klog_seq_oldest;
    if (count < total_available) start = g_klog_seq_next - count;
    size_t n = klog_walk_and_dump(start, count, buf, bufsize);
    klog_unlock(rf);
    if (out_len) *out_len = n;
    return true;
}

void klog_get_stats(klog_stats_t *out) {
    if (!out) return;
    uint64_t rf = klog_lock();
    out->seq_next      = g_klog_seq_next;
    out->seq_oldest    = g_klog_seq_oldest;
    out->total_emitted = g_klog_emitted;
    out->total_dropped = g_klog_dropped;
    out->buffer_used   = g_klog_used;
    out->buffer_size   = KLOG_BUFFER_SIZE;
    klog_unlock(rf);
}

void klog_clear(void) {
    uint64_t rf = klog_lock();
    g_klog_head = g_klog_tail = g_klog_used = 0;
    g_klog_seq_oldest = g_klog_seq_next;
    klog_unlock(rf);
}
