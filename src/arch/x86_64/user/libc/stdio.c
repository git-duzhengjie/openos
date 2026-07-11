/*
 * M5.3c libc/stdio.c — standard C stdio subset for OpenOS ring3 userland.
 *
 * The formatting engine (openos_vfmt) is syscall-agnostic: it emits bytes to a
 * caller-supplied sink callback, so the same code powers printf (sink = write
 * to fd) and snprintf (sink = fill a buffer). Actual output goes through the
 * weak __libc_write backend (bound to SYS_WRITE=64 in libc_write.c on target,
 * or replaced by the host libc during unit tests).
 */
#include "stdio.h"

/* Backend: emit len bytes to fd. Defined in libc_write.c on target. */
extern long __libc_write(int fd, const void *buf, unsigned long len);

/* ------------------------------------------------------------------ */
/* Standard streams                                                    */
/* ------------------------------------------------------------------ */
static FILE _stdin_file  = { 0, 0, 0 };
static FILE _stdout_file = { 1, 0, 0 };
static FILE _stderr_file = { 2, 0, 0 };
FILE *stdin  = &_stdin_file;
FILE *stdout = &_stdout_file;
FILE *stderr = &_stderr_file;

/* ------------------------------------------------------------------ */
/* Output sink abstraction                                             */
/* ------------------------------------------------------------------ */
struct sink {
    /* For buffer sinks: destination, capacity, running length. */
    char  *buf;
    size_t cap;      /* usable capacity excluding NUL, SIZE_MAX for fd sinks */
    size_t len;      /* number of chars that *would* be written (printf return) */
    int    fd;       /* for fd sinks; -1 for buffer sinks */
};

#define SINK_FD_NONE (-1)
#define SINK_CAP_INF ((size_t)-1)

static void sink_emit(struct sink *s, const char *data, size_t n)
{
    if (s->fd != SINK_FD_NONE) {
        __libc_write(s->fd, data, n);
        s->len += n;
        return;
    }
    /* buffer sink: copy what still fits, but always advance len */
    if (s->cap != 0 && s->len < s->cap) {
        size_t room = s->cap - s->len;
        size_t take = (n < room) ? n : room;
        for (size_t i = 0; i < take; i++)
            s->buf[s->len + i] = data[i];
    }
    s->len += n;
}

static void sink_putc(struct sink *s, char c)
{
    sink_emit(s, &c, 1);
}

/* ------------------------------------------------------------------ */
/* Number formatting helpers                                           */
/* ------------------------------------------------------------------ */

/* Convert an unsigned value to text in the given base into buf (reversed-
 * safe): writes into buf and returns the number of digits. base 2..16. */
static int u_to_str(unsigned long long val, unsigned base, int upper, char *buf)
{
    static const char lower[] = "0123456789abcdef";
    static const char upperd[] = "0123456789ABCDEF";
    const char *digits = upper ? upperd : lower;
    char tmp[64];
    int n = 0;
    if (val == 0) {
        tmp[n++] = '0';
    } else {
        while (val != 0) {
            tmp[n++] = digits[val % base];
            val /= base;
        }
    }
    /* reverse into buf */
    for (int i = 0; i < n; i++)
        buf[i] = tmp[n - 1 - i];
    buf[n] = '\0';
    return n;
}

/* Parsed conversion flags. */
struct fmt_spec {
    int  left;      /* '-' : left-justify */
    int  zero;      /* '0' : zero-pad */
    int  plus;      /* '+' : force sign */
    int  space;     /* ' ' : space before positive */
    int  alt;       /* '#' : alternate form */
    int  width;     /* minimum field width */
    int  prec;      /* precision, -1 if unset */
    int  lenmod;    /* 0=none 1='l' 2='ll' 3='h' 4='hh' 5='z' */
};

/* Emit padding character c, count times. */
static void emit_pad(struct sink *s, char c, int count)
{
    for (int i = 0; i < count; i++)
        sink_putc(s, c);
}

/* Emit a fully-prepared numeric body with prefix/sign and padding applied. */
static void emit_num(struct sink *s, const struct fmt_spec *f,
                     const char *sign, const char *prefix,
                     const char *digits, int ndigits)
{
    int slen = 0; while (sign[slen]) slen++;
    int plen = 0; while (prefix[plen]) plen++;

    /* apply precision (min digits) by zero-extending */
    int zeros = 0;
    if (f->prec >= 0 && ndigits < f->prec)
        zeros = f->prec - ndigits;

    int body = slen + plen + zeros + ndigits;
    int pad = (f->width > body) ? (f->width - body) : 0;

    /* zero-padding only applies when no precision and not left-justified */
    int use_zero = (f->zero && !f->left && f->prec < 0);

    if (!f->left && !use_zero)
        emit_pad(s, ' ', pad);
    sink_emit(s, sign, slen);
    sink_emit(s, prefix, plen);
    if (use_zero)
        emit_pad(s, '0', pad);
    emit_pad(s, '0', zeros);
    sink_emit(s, digits, ndigits);
    if (f->left)
        emit_pad(s, ' ', pad);
}

/* ------------------------------------------------------------------ */
/* Core formatting engine                                              */
/* ------------------------------------------------------------------ */
static int openos_vfmt(struct sink *s, const char *fmt, va_list ap)
{
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            sink_putc(s, *p);
            continue;
        }
        p++;                    /* skip '%' */
        if (*p == '%') { sink_putc(s, '%'); continue; }

        struct fmt_spec f = { 0, 0, 0, 0, 0, 0, -1, 0 };

        /* flags */
        for (;; p++) {
            if (*p == '-') f.left = 1;
            else if (*p == '0') f.zero = 1;
            else if (*p == '+') f.plus = 1;
            else if (*p == ' ') f.space = 1;
            else if (*p == '#') f.alt = 1;
            else break;
        }
        /* width (int or '*') */
        if (*p == '*') { f.width = va_arg(ap, int); if (f.width < 0) { f.left = 1; f.width = -f.width; } p++; }
        else while (*p >= '0' && *p <= '9') { f.width = f.width * 10 + (*p - '0'); p++; }
        /* precision */
        if (*p == '.') {
            p++;
            f.prec = 0;
            if (*p == '*') { f.prec = va_arg(ap, int); if (f.prec < 0) f.prec = -1; p++; }
            else while (*p >= '0' && *p <= '9') { f.prec = f.prec * 10 + (*p - '0'); p++; }
        }
        /* length modifiers */
        if (*p == 'l') { p++; if (*p == 'l') { f.lenmod = 2; p++; } else f.lenmod = 1; }
        else if (*p == 'h') { p++; if (*p == 'h') { f.lenmod = 4; p++; } else f.lenmod = 3; }
        else if (*p == 'z') { f.lenmod = 5; p++; }

        char numbuf[64];
        int nd;
        switch (*p) {
        case 'd':
        case 'i': {
            long long v;
            if (f.lenmod == 2) v = va_arg(ap, long long);
            else if (f.lenmod == 1) v = va_arg(ap, long);
            else if (f.lenmod == 5) v = (long long)va_arg(ap, long);
            else v = va_arg(ap, int);
            unsigned long long mag = (v < 0) ? (unsigned long long)(-(v + 1)) + 1ULL
                                             : (unsigned long long)v;
            nd = u_to_str(mag, 10, 0, numbuf);
            const char *sign = (v < 0) ? "-" : (f.plus ? "+" : (f.space ? " " : ""));
            emit_num(s, &f, sign, "", numbuf, nd);
            break;
        }
        case 'u':
        case 'x':
        case 'X':
        case 'o': {
            unsigned long long v;
            if (f.lenmod == 2) v = va_arg(ap, unsigned long long);
            else if (f.lenmod == 1) v = va_arg(ap, unsigned long);
            else if (f.lenmod == 5) v = va_arg(ap, unsigned long);
            else v = va_arg(ap, unsigned int);
            unsigned base = (*p == 'o') ? 8 : ((*p == 'u') ? 10 : 16);
            int upper = (*p == 'X');
            nd = u_to_str(v, base, upper, numbuf);
            const char *prefix = "";
            if (f.alt && v != 0) {
                if (*p == 'x') prefix = "0x";
                else if (*p == 'X') prefix = "0X";
                else if (*p == 'o') prefix = "0";
            }
            emit_num(s, &f, "", prefix, numbuf, nd);
            break;
        }
        case 'p': {
            unsigned long long v = (unsigned long long)(unsigned long)va_arg(ap, void *);
            nd = u_to_str(v, 16, 0, numbuf);
            emit_num(s, &f, "", "0x", numbuf, nd);
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            int pad = (f.width > 1) ? f.width - 1 : 0;
            if (!f.left) emit_pad(s, ' ', pad);
            sink_putc(s, c);
            if (f.left) emit_pad(s, ' ', pad);
            break;
        }
        case 's': {
            const char *str = va_arg(ap, const char *);
            if (!str) str = "(null)";
            int slen = 0; while (str[slen]) slen++;
            if (f.prec >= 0 && slen > f.prec) slen = f.prec;
            int pad = (f.width > slen) ? f.width - slen : 0;
            if (!f.left) emit_pad(s, ' ', pad);
            sink_emit(s, str, slen);
            if (f.left) emit_pad(s, ' ', pad);
            break;
        }
        case '\0':
            /* trailing '%': emit literally and stop */
            sink_putc(s, '%');
            return (int)s->len;
        default:
            /* unknown conversion: emit verbatim */
            sink_putc(s, '%');
            sink_putc(s, *p);
            break;
        }
    }
    return (int)s->len;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */
int vsnprintf(char *str, size_t size, const char *format, va_list ap)
{
    struct sink s;
    s.buf = str;
    s.cap = (size > 0) ? size - 1 : 0;   /* reserve room for NUL */
    s.len = 0;
    s.fd  = SINK_FD_NONE;
    int r = openos_vfmt(&s, format, ap);
    if (size > 0) {
        size_t term = (s.len < s.cap) ? s.len : s.cap;
        str[term] = '\0';
    }
    return r;
}

int snprintf(char *str, size_t size, const char *format, ...)
{
    va_list ap; va_start(ap, format);
    int r = vsnprintf(str, size, format, ap);
    va_end(ap);
    return r;
}

int vfprintf(FILE *stream, const char *format, va_list ap)
{
    struct sink s;
    s.buf = NULL;
    s.cap = SINK_CAP_INF;
    s.len = 0;
    s.fd  = stream ? stream->fd : 1;
    return openos_vfmt(&s, format, ap);
}

int vprintf(const char *format, va_list ap)
{
    return vfprintf(stdout, format, ap);
}

int fprintf(FILE *stream, const char *format, ...)
{
    va_list ap; va_start(ap, format);
    int r = vfprintf(stream, format, ap);
    va_end(ap);
    return r;
}

int printf(const char *format, ...)
{
    va_list ap; va_start(ap, format);
    int r = vfprintf(stdout, format, ap);
    va_end(ap);
    return r;
}

/* ------------------------------------------------------------------ */
/* Unformatted output                                                  */
/* ------------------------------------------------------------------ */
int fputc(int c, FILE *stream)
{
    unsigned char ch = (unsigned char)c;
    if (__libc_write(stream ? stream->fd : 1, &ch, 1) != 1)
        return EOF;
    return (int)ch;
}

int putc(int c, FILE *stream)
{
    return fputc(c, stream);
}

int putchar(int c)
{
    return fputc(c, stdout);
}

int fputs(const char *s, FILE *stream)
{
    size_t n = 0; while (s[n]) n++;
    if ((size_t)__libc_write(stream ? stream->fd : 1, s, n) != n)
        return EOF;
    return (int)n;
}

int puts(const char *s)
{
    if (fputs(s, stdout) == EOF) return EOF;
    if (fputc('\n', stdout) == EOF) return EOF;
    return 0;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    size_t total = size * nmemb;
    if (size != 0 && total / size != nmemb) return 0;   /* overflow guard */
    if (total == 0) return 0;
    long w = __libc_write(stream ? stream->fd : 1, ptr, total);
    if (w <= 0) return 0;
    return (size_t)w / size;
}
