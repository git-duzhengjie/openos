/*
 * tty64.c — M4.4a: pseudo-terminal line discipline engine.
 *
 * See tty64.h for the full design rationale. This file owns:
 *   - the device table (fixed TTY64_MAX slots),
 *   - three byte rings per device: raw-edit line, cooked (line-committed),
 *     and output/echo,
 *   - the canonical vs raw line discipline in tty64_input_byte(),
 *   - termios get/set and winsize,
 *   - a one-slot pending-signal latch for INTR/EOF handling.
 *
 * No dependency on proc/syscall — pure byte machine, self-test friendly.
 */
#include "../include/tty64.h"

/* ------------------------------------------------------------------ */
/* Signal numbers we can raise from control chars. Kept local to avoid a
 * hard include dependency; must match the kernel's signal numbering. */
#define TTY64_SIGINT   2
#define TTY64_SIGQUIT  3

/* A cooked-ring byte carries a "line boundary" marker so tty64_read can
 * honour whole-line semantics in canonical mode without rescanning. We store
 * the payload in cbuf[] and a parallel "is end-of-line" bit is unnecessary:
 * instead we track committed-line count so readable() is O(1). */

typedef struct {
    int              used;

    /* pending edit line (canonical mode only) */
    uint8_t          line[TTY64_LINE_MAX];
    int              line_len;

    /* cooked ring: bytes ready for read() */
    uint8_t          cbuf[TTY64_CBUF];
    int              chead, ctail, ccount;
    int              lines_ready;   /* # of complete lines in cbuf (canon) */
    int              eof_pending;   /* ^D on empty line -> next read = 0    */

    /* output / echo ring */
    uint8_t          obuf[TTY64_OBUF];
    int              ohead, otail, ocount;

    tty64_termios_t  tio;
    tty64_winsize_t  win;

    int              sig_latch;     /* queued signal (>0) or 0             */
    int              pgrp;          /* foreground process group (M4.4b)    */
} tty64_dev_t;

static tty64_dev_t g_ttys[TTY64_MAX];
static int         g_ready = 0;

/* ------------------------------------------------------------------ */

static void tty64_lazy_init(void)
{
    if (g_ready) return;
    for (int i = 0; i < TTY64_MAX; ++i) g_ttys[i].used = 0;
    g_ready = 1;
}

void tty64_reset(void)
{
    g_ready = 0;
    tty64_lazy_init();
}

static tty64_dev_t *dev_of(int id)
{
    tty64_lazy_init();
    if (id < 0 || id >= TTY64_MAX) return 0;
    if (!g_ttys[id].used) return 0;
    return &g_ttys[id];
}

int tty64_valid(int id) { return dev_of(id) != 0; }

int tty64_create(void)
{
    tty64_lazy_init();
    for (int i = 0; i < TTY64_MAX; ++i) {
        if (g_ttys[i].used) continue;
        tty64_dev_t *d = &g_ttys[i];
        d->used = 1;
        d->line_len = 0;
        d->chead = d->ctail = d->ccount = 0;
        d->lines_ready = 0;
        d->eof_pending = 0;
        d->ohead = d->otail = d->ocount = 0;
        d->sig_latch = 0;
        d->pgrp = 0;
        /* default cooked termios */
        d->tio.c_lflag = TTY64_ICANON | TTY64_ECHO | TTY64_ISIG;
        d->tio.c_cc[TTY64_VINTR]  = 0x03; /* ^C */
        d->tio.c_cc[TTY64_VEOF]   = 0x04; /* ^D */
        d->tio.c_cc[TTY64_VERASE] = 0x7F; /* DEL */
        d->tio.c_cc[TTY64_VKILL]  = 0x15; /* ^U */
        d->win.ws_row = 25;
        d->win.ws_col = 80;
        d->win.ws_xpixel = 0;
        d->win.ws_ypixel = 0;
        return i;
    }
    return -1;
}

int tty64_active_count(void)
{
    tty64_lazy_init();
    int n = 0;
    for (int i = 0; i < TTY64_MAX; ++i) if (g_ttys[i].used) ++n;
    return n;
}

/* ------------------------------------------------------------------ */
/* Ring helpers (cooked + output share the same simple pattern).       */

static int cpush(tty64_dev_t *d, uint8_t b)
{
    if (d->ccount >= TTY64_CBUF) return 0;
    d->cbuf[d->ctail] = b;
    d->ctail = (d->ctail + 1) % TTY64_CBUF;
    ++d->ccount;
    return 1;
}

static int cpop(tty64_dev_t *d, uint8_t *out)
{
    if (d->ccount <= 0) return 0;
    *out = d->cbuf[d->chead];
    d->chead = (d->chead + 1) % TTY64_CBUF;
    --d->ccount;
    return 1;
}

static int opush(tty64_dev_t *d, uint8_t b)
{
    if (d->ocount >= TTY64_OBUF) return 0;
    d->obuf[d->otail] = b;
    d->otail = (d->otail + 1) % TTY64_OBUF;
    ++d->ocount;
    return 1;
}

static int opop(tty64_dev_t *d, uint8_t *out)
{
    if (d->ocount <= 0) return 0;
    *out = d->obuf[d->ohead];
    d->ohead = (d->ohead + 1) % TTY64_OBUF;
    --d->ocount;
    return 1;
}

/* Echo one byte to the output ring, expanding control chars the way a real
 * terminal does (^C shows as "^C"). Printable + \n + \t pass through. */
static void echo_byte(tty64_dev_t *d, uint8_t b)
{
    if (!(d->tio.c_lflag & TTY64_ECHO)) return;
    if (b == '\n' || b == '\t' || (b >= 0x20 && b < 0x7F)) {
        opush(d, b);
    } else if (b < 0x20) {
        opush(d, '^');
        opush(d, (uint8_t)(b + '@'));
    } else {
        /* 0x7F and >=0x80: show as '?' to stay printable */
        opush(d, '?');
    }
}

/* Commit the pending edit line to the cooked ring, appending '\n'. */
static void commit_line(tty64_dev_t *d, int with_newline)
{
    for (int i = 0; i < d->line_len; ++i) cpush(d, d->line[i]);
    if (with_newline) cpush(d, (uint8_t)'\n');
    d->line_len = 0;
    ++d->lines_ready;
}

/* ------------------------------------------------------------------ */
/* Input path: run one raw byte through the line discipline.           */

int tty64_input_byte(int id, uint8_t ch)
{
    tty64_dev_t *d = dev_of(id);
    if (!d) return -1;

    /* RAW mode: byte is immediately available, echo honoured, no editing. */
    if (!(d->tio.c_lflag & TTY64_ICANON)) {
        echo_byte(d, ch);
        cpush(d, ch);
        return 0;
    }

    /* CANONICAL mode. */
    const uint8_t INTR  = d->tio.c_cc[TTY64_VINTR];
    const uint8_t VEOF  = d->tio.c_cc[TTY64_VEOF];
    const uint8_t ERASE = d->tio.c_cc[TTY64_VERASE];
    const uint8_t KILL  = d->tio.c_cc[TTY64_VKILL];

    /* Signal-generating chars (only if ISIG). */
    if ((d->tio.c_lflag & TTY64_ISIG) && ch == INTR) {
        d->line_len = 0;             /* discard partial line */
        d->sig_latch = TTY64_SIGINT;
        echo_byte(d, ch);            /* show ^C */
        return 0;
    }

    /* EOF (^D): terminate line. On non-empty line commit it *without* an
     * extra newline sentinel beyond what's typed; on empty line raise EOF. */
    if (ch == VEOF) {
        if (d->line_len > 0) {
            commit_line(d, 0);       /* deliver typed chars, no '\n' added */
        } else {
            d->eof_pending = 1;      /* empty line -> read() returns 0 */
        }
        return 0;
    }

    /* ERASE: rub out last char of pending line. */
    if (ch == ERASE) {
        if (d->line_len > 0) {
            --d->line_len;
            if (d->tio.c_lflag & TTY64_ECHO) {
                opush(d, '\b'); opush(d, ' '); opush(d, '\b');
            }
        }
        return 0;
    }

    /* KILL: wipe whole pending line. */
    if (ch == KILL) {
        while (d->line_len > 0) {
            --d->line_len;
            if (d->tio.c_lflag & TTY64_ECHO) {
                opush(d, '\b'); opush(d, ' '); opush(d, '\b');
            }
        }
        return 0;
    }

    /* Line terminator: accept and commit. */
    if (ch == '\n') {
        echo_byte(d, ch);
        commit_line(d, 1);
        return 0;
    }

    /* Ordinary char: append to edit line if room, echo it. */
    if (d->line_len < TTY64_LINE_MAX) {
        d->line[d->line_len++] = ch;
        echo_byte(d, ch);
    }
    return 0;
}

int tty64_input_str(int id, const char *s)
{
    if (!s) return -1;
    int n = 0;
    for (; s[n]; ++n) {
        if (tty64_input_byte(id, (uint8_t)s[n]) < 0) return -1;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* Read path.                                                          */

int tty64_readable(int id)
{
    tty64_dev_t *d = dev_of(id);
    if (!d) return -1;
    if (!(d->tio.c_lflag & TTY64_ICANON)) {
        /* raw: everything in cooked ring is readable now */
        return d->ccount;
    }
    /* canonical: only bytes belonging to complete lines are readable.
     * Since commit only ever pushes whole lines into cbuf, the entire
     * cbuf content is line-complete -> ccount is the readable count. */
    if (d->lines_ready > 0) return d->ccount;
    return 0;
}

int tty64_read(int id, void *buf, int len)
{
    tty64_dev_t *d = dev_of(id);
    if (!d) return -1;
    if (len <= 0) return 0;
    uint8_t *p = (uint8_t *)buf;

    /* RAW mode: hand over up to len bytes, whatever is present. */
    if (!(d->tio.c_lflag & TTY64_ICANON)) {
        int n = 0;
        uint8_t b;
        while (n < len && cpop(d, &b)) p[n++] = b;
        return n;
    }

    /* CANONICAL mode. */
    if (d->ccount == 0) {
        /* no complete line; maybe an EOF was signalled on an empty line */
        if (d->eof_pending) { d->eof_pending = 0; return 0; }
        return 0;
    }

    /* Deliver bytes up to len, stopping right after a '\n' so each read()
     * returns at most one line (classic canonical behaviour). */
    int n = 0;
    uint8_t b;
    while (n < len && cpop(d, &b)) {
        p[n++] = b;
        if (b == '\n') {
            if (d->lines_ready > 0) --d->lines_ready;
            break;
        }
    }
    /* If we consumed a line that had no trailing '\n' (EOF-committed line),
     * decrement the line counter too once the ring drains that line. This is
     * approximated by: when ccount hits 0 and lines_ready still >0, clear. */
    if (d->ccount == 0 && d->lines_ready > 0) d->lines_ready = 0;
    return n;
}

/* ------------------------------------------------------------------ */
/* Output path.                                                        */

int tty64_write(int id, const void *buf, int len)
{
    tty64_dev_t *d = dev_of(id);
    if (!d) return -1;
    if (len <= 0) return 0;
    const uint8_t *p = (const uint8_t *)buf;
    int n = 0;
    while (n < len && opush(d, p[n])) ++n;
    return n;
}

int tty64_drain_output(int id, void *buf, int len)
{
    tty64_dev_t *d = dev_of(id);
    if (!d) return -1;
    if (len <= 0) return 0;
    uint8_t *p = (uint8_t *)buf;
    int n = 0;
    uint8_t b;
    while (n < len && opop(d, &b)) p[n++] = b;
    return n;
}

int tty64_output_pending(int id)
{
    tty64_dev_t *d = dev_of(id);
    if (!d) return -1;
    return d->ocount;
}

/* ------------------------------------------------------------------ */
/* termios / winsize backing.                                          */

int tty64_tcgets(int id, tty64_termios_t *out)
{
    tty64_dev_t *d = dev_of(id);
    if (!d || !out) return -1;
    *out = d->tio;
    return 0;
}

int tty64_tcsets(int id, const tty64_termios_t *in)
{
    tty64_dev_t *d = dev_of(id);
    if (!d || !in) return -1;
    int was_canon = (d->tio.c_lflag & TTY64_ICANON) != 0;
    d->tio = *in;
    int now_canon = (d->tio.c_lflag & TTY64_ICANON) != 0;
    /* Leaving canonical mode: flush any pending edit line into cooked ring
     * so no typed bytes are lost across the mode switch. */
    if (was_canon && !now_canon && d->line_len > 0) {
        for (int i = 0; i < d->line_len; ++i) cpush(d, d->line[i]);
        d->line_len = 0;
    }
    return 0;
}

int tty64_get_winsize(int id, tty64_winsize_t *out)
{
    tty64_dev_t *d = dev_of(id);
    if (!d || !out) return -1;
    *out = d->win;
    return 0;
}

int tty64_set_winsize(int id, const tty64_winsize_t *in)
{
    tty64_dev_t *d = dev_of(id);
    if (!d || !in) return -1;
    d->win = *in;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Signal handshake + job control fields.                              */

int tty64_take_signal(int id)
{
    tty64_dev_t *d = dev_of(id);
    if (!d) return 0;
    int s = d->sig_latch;
    d->sig_latch = 0;
    return s;
}

int tty64_get_pgrp(int id)
{
    tty64_dev_t *d = dev_of(id);
    if (!d) return -1;
    return d->pgrp;
}

int tty64_set_pgrp(int id, int pgrp)
{
    tty64_dev_t *d = dev_of(id);
    if (!d) return -1;
    d->pgrp = pgrp;
    return 0;
}
