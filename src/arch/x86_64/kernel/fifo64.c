/*
 * fifo64.c — M4.3b: named pipes (FIFO) registry.
 *
 * See fifo64.h for the design rationale. In short: a small fixed table maps a
 * path string to a lazily-allocated pipe64 ring. All the ring/blocking/poll
 * machinery is reused from pipe64; this file only owns the name registry and
 * per-end open reference counting.
 */
#include "../include/fifo64.h"
#include "../include/pipe64.h"

typedef struct {
    char path[FIFO64_PATH_MAX];
    int  used;       /* name registration is live                */
    int  mode;       /* mkfifo mode bits (informational)         */
    int  pipe_id;    /* backing pipe64 ring, or -1 if not yet opened */
    int  unlinked;   /* name removed but ring still open         */
} fifo64_t;

static fifo64_t g_fifos[FIFO64_MAX];
static int      g_fifos_ready = 0;

/* ------------------------------------------------------------------ */

static int str_eq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b) return 0;
        ++a; ++b;
    }
    return *a == *b;
}

static int str_copy(char *dst, const char *src, int cap)
{
    int i = 0;
    while (src[i] && i < cap - 1) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
    /* Return -1 if truncated (source longer than cap-1). */
    return src[i] == '\0' ? i : -1;
}

static void fifo64_lazy_init(void)
{
    if (g_fifos_ready) return;
    for (int i = 0; i < FIFO64_MAX; ++i) {
        g_fifos[i].used = 0;
        g_fifos[i].pipe_id = -1;
        g_fifos[i].unlinked = 0;
        g_fifos[i].path[0] = '\0';
    }
    g_fifos_ready = 1;
}

void fifo64_reset(void)
{
    g_fifos_ready = 0;
    fifo64_lazy_init();
}

/* Find a live (non-unlinked) registration by path. */
static int find_by_path(const char *path)
{
    fifo64_lazy_init();
    for (int i = 0; i < FIFO64_MAX; ++i) {
        if (g_fifos[i].used && !g_fifos[i].unlinked &&
            str_eq(g_fifos[i].path, path)) {
            return i;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */

int fifo64_mkfifo(const char *path, int mode)
{
    fifo64_lazy_init();
    if (!path || path[0] == '\0') {
        return -1;
    }
    if (find_by_path(path) >= 0) {
        return -3; /* EEXIST */
    }
    for (int i = 0; i < FIFO64_MAX; ++i) {
        if (!g_fifos[i].used) {
            if (str_copy(g_fifos[i].path, path, FIFO64_PATH_MAX) < 0) {
                g_fifos[i].path[0] = '\0';
                return -1; /* path too long */
            }
            g_fifos[i].used = 1;
            g_fifos[i].mode = mode;
            g_fifos[i].pipe_id = -1;
            g_fifos[i].unlinked = 0;
            return 0;
        }
    }
    return -2; /* table full */
}

int fifo64_lookup(const char *path)
{
    return find_by_path(path);
}

int fifo64_open(const char *path, int dir)
{
    int idx = find_by_path(path);
    if (idx < 0) {
        return -1; /* no such FIFO */
    }
    if (dir != FIFO64_O_READ && dir != FIFO64_O_WRITE) {
        return -1;
    }
    fifo64_t *f = &g_fifos[idx];
    /* Lazily allocate the backing ring on first open. */
    if (f->pipe_id < 0) {
        int pid = pipe64_create_bare();
        if (pid < 0) {
            return -1; /* rings exhausted */
        }
        f->pipe_id = pid;
    }
    /* Bump the matching end's refcount. */
    int is_write = (dir == FIFO64_O_WRITE) ? 1 : 0;
    if (pipe64_ref_end(f->pipe_id, is_write) < 0) {
        return -1;
    }
    return f->pipe_id;
}

int fifo64_close(const char *path, int dir)
{
    /* Search across both live and unlinked entries: an unlinked FIFO may
     * still have open ends being closed. */
    fifo64_lazy_init();
    int idx = -1;
    for (int i = 0; i < FIFO64_MAX; ++i) {
        if (g_fifos[i].used && str_eq(g_fifos[i].path, path)) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        return -1;
    }
    fifo64_t *f = &g_fifos[idx];
    if (f->pipe_id < 0) {
        return -1; /* never opened */
    }
    int is_write = (dir == FIFO64_O_WRITE) ? 1 : 0;
    pipe64_close_end(f->pipe_id, is_write);
    /* When both ends are fully closed the pipe64 layer has already recycled
     * the ring; forget our reference to it. */
    if (pipe64_has_reader(f->pipe_id) == 0 &&
        pipe64_has_writer(f->pipe_id) == 0) {
        f->pipe_id = -1;
        /* If the name was already unlinked, drop the slot entirely now. */
        if (f->unlinked) {
            f->used = 0;
            f->path[0] = '\0';
            f->unlinked = 0;
        }
    }
    return 0;
}

int fifo64_unlink(const char *path)
{
    int idx = find_by_path(path);
    if (idx < 0) {
        return -1;
    }
    fifo64_t *f = &g_fifos[idx];
    if (f->pipe_id < 0) {
        /* No open ends: remove the registration outright. */
        f->used = 0;
        f->path[0] = '\0';
        f->unlinked = 0;
    } else {
        /* Ring still open: unlink the name, keep the ring until last close. */
        f->unlinked = 1;
    }
    return 0;
}

int fifo64_active_count(void)
{
    fifo64_lazy_init();
    int n = 0;
    for (int i = 0; i < FIFO64_MAX; ++i) {
        if (g_fifos[i].used && !g_fifos[i].unlinked) {
            ++n;
        }
    }
    return n;
}

int fifo64_pipe_id(const char *path)
{
    int idx = find_by_path(path);
    if (idx < 0) {
        return -1;
    }
    return g_fifos[idx].pipe_id;
}
