/*
 * pipe64.c — Anonymous pipe pool implementation (M4.1c)
 *
 * See pipe64.h for the contract. Each pipe is a fixed-capacity byte ring
 * buffer with head/tail indices and a byte count, plus separate open-end
 * reference counts for the read and write ends.
 */
#include "../include/pipe64.h"

typedef struct {
    uint8_t  buf[PIPE64_CAPACITY];
    uint32_t head;      /* next read position  */
    uint32_t tail;      /* next write position */
    uint32_t count;     /* bytes currently buffered */
    int      read_refs; /* open read ends  */
    int      write_refs;/* open write ends */
    int      used;      /* slot allocated */
    /* M4.3a: per-end waiter queues holding parked scheduler slot ids.
     * rwaiters = readers blocked on an empty pipe waiting for data;
     * wwaiters = writers blocked on a full pipe waiting for space. */
    uint32_t rwaiters[PIPE64_WAITERS_MAX];
    int      rwait_n;
    uint32_t wwaiters[PIPE64_WAITERS_MAX];
    int      wwait_n;
} pipe64_t;

static pipe64_t g_pipes[PIPE64_MAX];
static int      g_pipes_ready = 0;

static void pipe64_lazy_init(void)
{
    if (g_pipes_ready) {
        return;
    }
    for (int i = 0; i < PIPE64_MAX; ++i) {
        g_pipes[i].head = 0;
        g_pipes[i].tail = 0;
        g_pipes[i].count = 0;
        g_pipes[i].read_refs = 0;
        g_pipes[i].write_refs = 0;
        g_pipes[i].used = 0;
        g_pipes[i].rwait_n = 0;
        g_pipes[i].wwait_n = 0;
    }
    g_pipes_ready = 1;
}

void pipe64_reset(void)
{
    g_pipes_ready = 0;
    pipe64_lazy_init();
}

static pipe64_t *pipe64_get(int pipe_id)
{
    pipe64_lazy_init();
    if (pipe_id < 0 || pipe_id >= PIPE64_MAX) {
        return 0;
    }
    if (!g_pipes[pipe_id].used) {
        return 0;
    }
    return &g_pipes[pipe_id];
}

int pipe64_create(void)
{
    pipe64_lazy_init();
    for (int i = 0; i < PIPE64_MAX; ++i) {
        if (!g_pipes[i].used) {
            g_pipes[i].head = 0;
            g_pipes[i].tail = 0;
            g_pipes[i].count = 0;
            g_pipes[i].read_refs = 1;
            g_pipes[i].write_refs = 1;
            g_pipes[i].used = 1;
            g_pipes[i].rwait_n = 0;
            g_pipes[i].wwait_n = 0;
            return i;
        }
    }
    return -1; /* pool exhausted */
}

int pipe64_create_bare(void)
{
    pipe64_lazy_init();
    for (int i = 0; i < PIPE64_MAX; ++i) {
        if (!g_pipes[i].used) {
            g_pipes[i].head = 0;
            g_pipes[i].tail = 0;
            g_pipes[i].count = 0;
            g_pipes[i].read_refs = 0;   /* FIFO: ends opened on demand */
            g_pipes[i].write_refs = 0;
            g_pipes[i].used = 1;
            g_pipes[i].rwait_n = 0;
            g_pipes[i].wwait_n = 0;
            return i;
        }
    }
    return -1; /* pool exhausted */
}

int pipe64_ref_end(int pipe_id, int is_write)
{
    pipe64_t *p = pipe64_get(pipe_id);
    if (!p) {
        return -1;
    }
    if (is_write) {
        p->write_refs++;
    } else {
        p->read_refs++;
    }
    return 0;
}

int pipe64_close_end(int pipe_id, int is_write)
{
    pipe64_t *p = pipe64_get(pipe_id);
    if (!p) {
        return -1;
    }
    if (is_write) {
        if (p->write_refs > 0) {
            p->write_refs--;
        }
    } else {
        if (p->read_refs > 0) {
            p->read_refs--;
        }
    }
    /* Recycle the slot only when both ends are fully closed. */
    if (p->read_refs == 0 && p->write_refs == 0) {
        p->used = 0;
        p->head = p->tail = p->count = 0;
    }
    return 0;
}

int pipe64_read(int pipe_id, void *buf, uint32_t len)
{
    pipe64_t *p = pipe64_get(pipe_id);
    if (!p) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }
    uint8_t *out = (uint8_t *)buf;
    uint32_t n = 0;
    while (n < len && p->count > 0) {
        out[n++] = p->buf[p->head];
        p->head = (p->head + 1) % PIPE64_CAPACITY;
        p->count--;
    }
    /* n may be 0 if the ring is empty. With no open write end that means
     * EOF; with a write end still open it is a would-block (returned as 0
     * for the current non-blocking model). Callers treat 0 accordingly. */
    return (int)n;
}

int pipe64_write(int pipe_id, const void *buf, uint32_t len)
{
    pipe64_t *p = pipe64_get(pipe_id);
    if (!p) {
        return -1;
    }
    if (p->read_refs == 0) {
        return -2; /* EPIPE: nobody will ever read */
    }
    if (len == 0) {
        return 0;
    }
    const uint8_t *in = (const uint8_t *)buf;
    uint32_t n = 0;
    while (n < len && p->count < PIPE64_CAPACITY) {
        p->buf[p->tail] = in[n++];
        p->tail = (p->tail + 1) % PIPE64_CAPACITY;
        p->count++;
    }
    return (int)n; /* short write if the ring filled up */
}

int pipe64_active_count(void)
{
    pipe64_lazy_init();
    int n = 0;
    for (int i = 0; i < PIPE64_MAX; ++i) {
        if (g_pipes[i].used) {
            ++n;
        }
    }
    return n;
}

/*
 * ===========================================================================
 * M4.3a: poll + waiter-queue bookkeeping for blocking pipe semantics.
 * ===========================================================================
 */

int pipe64_poll(int pipe_id)
{
    pipe64_t *p = pipe64_get(pipe_id);
    if (!p) {
        return -1;
    }
    int bits = 0;
    /* Readable when data is buffered, OR when no writer remains (EOF: a read
     * would return 0 immediately rather than block). */
    if (p->count > 0 || p->write_refs == 0) {
        bits |= PIPE64_POLL_READABLE;
    }
    /* Writable when there is ring space, OR when no reader remains (a write
     * would fail with EPIPE immediately rather than block). */
    if (p->count < PIPE64_CAPACITY || p->read_refs == 0) {
        bits |= PIPE64_POLL_WRITABLE;
    }
    if (p->read_refs == 0 || p->write_refs == 0) {
        bits |= PIPE64_POLL_HUP;
    }
    return bits;
}

int pipe64_buffered(int pipe_id)
{
    pipe64_t *p = pipe64_get(pipe_id);
    if (!p) {
        return -1;
    }
    return (int)p->count;
}

int pipe64_space(int pipe_id)
{
    pipe64_t *p = pipe64_get(pipe_id);
    if (!p) {
        return -1;
    }
    return (int)(PIPE64_CAPACITY - p->count);
}

int pipe64_has_writer(int pipe_id)
{
    pipe64_t *p = pipe64_get(pipe_id);
    if (!p) {
        return -1;
    }
    return p->write_refs > 0 ? 1 : 0;
}

int pipe64_has_reader(int pipe_id)
{
    pipe64_t *p = pipe64_get(pipe_id);
    if (!p) {
        return -1;
    }
    return p->read_refs > 0 ? 1 : 0;
}

int pipe64_wait_add(int pipe_id, int is_write, uint32_t slot)
{
    pipe64_t *p = pipe64_get(pipe_id);
    if (!p) {
        return -1;
    }
    uint32_t *q = is_write ? p->wwaiters : p->rwaiters;
    int      *n = is_write ? &p->wwait_n : &p->rwait_n;
    /* Coalesce duplicate registrations. */
    for (int i = 0; i < *n; ++i) {
        if (q[i] == slot) {
            return 0;
        }
    }
    if (*n >= PIPE64_WAITERS_MAX) {
        return -2;
    }
    q[(*n)++] = slot;
    return 0;
}

int pipe64_wait_remove(int pipe_id, int is_write, uint32_t slot)
{
    pipe64_t *p = pipe64_get(pipe_id);
    if (!p) {
        return -1;
    }
    uint32_t *q = is_write ? p->wwaiters : p->rwaiters;
    int      *n = is_write ? &p->wwait_n : &p->rwait_n;
    for (int i = 0; i < *n; ++i) {
        if (q[i] == slot) {
            /* Compact tail-fill: move last into the hole. */
            q[i] = q[*n - 1];
            (*n)--;
            return 0;
        }
    }
    return 0; /* not present: no-op success */
}

int pipe64_wait_drain(int pipe_id, int is_write, uint32_t *out_slots)
{
    pipe64_t *p = pipe64_get(pipe_id);
    if (!p) {
        return -1;
    }
    uint32_t *q = is_write ? p->wwaiters : p->rwaiters;
    int      *n = is_write ? &p->wwait_n : &p->rwait_n;
    int cnt = *n;
    if (out_slots) {
        for (int i = 0; i < cnt; ++i) {
            out_slots[i] = q[i];
        }
    }
    *n = 0;
    return cnt;
}

int pipe64_wait_count(int pipe_id, int is_write)
{
    pipe64_t *p = pipe64_get(pipe_id);
    if (!p) {
        return -1;
    }
    return is_write ? p->wwait_n : p->rwait_n;
}
