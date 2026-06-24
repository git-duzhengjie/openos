#ifndef OPENOS_TCC_COMPAT_H
#define OPENOS_TCC_COMPAT_H

#define TCC_TARGET_I386 1
#define TCC_TARGET_OPENOS 1
/* Do not define other TCC_TARGET_* macros here: TinyCC uses #ifdef checks. */
#define CONFIG_TCC_STATIC 1
#define CONFIG_USE_LIBGCC 0
#define CONFIG_TCC_SEMLOCK 0
#define CONFIG_TCCDIR "/usr/lib/tcc"
#define CONFIG_SYSROOT ""
#define CONFIG_TCC_CRTPREFIX "/usr/lib/tcc"
#define CONFIG_TCC_ELFINTERP ""
#define ONE_SOURCE 1

#include "openos.h"

#undef malloc
#undef free
#undef realloc
#undef gettimeofday

static inline void *malloc(unsigned long size) { return openos_malloc((int)size); }
static inline void free(void *ptr) { openos_free(ptr); }
static inline void *realloc(void *ptr, unsigned long size) { return openos_realloc(ptr, (int)size); }

typedef unsigned int size_t;
typedef int ssize_t;
typedef long time_t;
typedef unsigned int mode_t;
typedef int pid_t;
typedef int off_t;
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;
typedef unsigned int uintptr_t;
typedef int intptr_t;
typedef int intmax_t;
typedef unsigned int uintmax_t;

#define NULL ((void*)0)
#define EOF (-1)
#define BUFSIZ 1024
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
/* stdin/stdout/stderr are provided by openos.h */

#define ENOENT OPENOS_ENOENT
#define ENOMEM OPENOS_ENOMEM
#define EINVAL OPENOS_EINVAL
#define EIO OPENOS_EIO
#define EEXIST OPENOS_EEXIST
#define ENOTDIR OPENOS_ENOTDIR
#define EISDIR OPENOS_EISDIR
#define EACCES OPENOS_EACCES
#define errno (*openos_errno_location())

#ifndef O_APPEND
#define O_APPEND 0x400
#endif

#define S_IFMT FS_TYPE_MASK
#define S_IFREG FS_FILE
#define S_IFDIR FS_DIR
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_IREAD  0400
#define S_IWRITE 0200
#define S_IEXEC  0100

struct stat {
    unsigned int mode;
    unsigned int size;
    unsigned int mtime;
};

static inline int openos_tcc_stat(const char *path, struct stat *st)
{
    openos_stat_t os;
    int r = openos_stat(path, &os);
    if (r < 0) return r;
    st->mode = os.mode;
    st->size = os.size;
    st->mtime = (unsigned int)os.mtime_utc;
    return 0;
}

static inline int openos_tcc_fstat(int fd, struct stat *st)
{
    openos_stat_t os;
    int r = openos_fstat(fd, &os);
    if (r < 0) return r;
    st->mode = os.mode;
    st->size = os.size;
    st->mtime = (unsigned int)os.mtime_utc;
    return 0;
}

#define stat(path, st) openos_tcc_stat((path), (st))
#define fstat(fd, st) openos_tcc_fstat((fd), (st))
#define open(path, flags, ...) openos_open((path), (flags), 0644)
#define close(fd) openos_close((fd))
#define read(fd, buf, len) openos_read((fd), (buf), (len))
#define write(fd, buf, len) openos_write((fd), (buf), (len))
#define unlink(path) openos_unlink((path))
#define lseek(fd, off, whence) openos_seek((fd), (off), (whence))
#define getcwd(buf, size) openos_getcwd((buf), (size))
#define chdir(path) openos_chdir((path))
#define access(path, mode) (openos_tcc_stat((path), &(struct stat){0}))
#define O_BINARY 0
#undef time
#define exit(code) openos_exit((code))
#define abort() openos_exit(134)

static inline int remove(const char *path) { return openos_unlink(path); }
static inline int rename(const char *oldpath, const char *newpath) { (void)oldpath; (void)newpath; openos_set_errno(OPENOS_ENOSYS); return -1; }
static inline int chmod(const char *path, mode_t mode) { return openos_chmod(path, mode); }
static inline int isatty(int fd) { return fd == 0 || fd == 1 || fd == 2; }
static inline int fileno(FILE *f) { return f ? f->fd : -1; }
static inline int vprintf(const char *fmt, __builtin_va_list ap) { return openos_vdprintf(1, fmt, ap); }
static inline int vfprintf(FILE *f, const char *fmt, __builtin_va_list ap) { return openos_vfprintf(f, fmt, ap); }
static inline int vsnprintf(char *buf, size_t size, const char *fmt, __builtin_va_list ap) { return openos_vsnprintf(buf, size, fmt, ap); }
static inline int sprintf(char *buf, const char *fmt, ...) { __builtin_va_list ap; int r; __builtin_va_start(ap, fmt); r = openos_vsnprintf(buf, 0x7fffffffU, fmt, ap); __builtin_va_end(ap); return r; }
static inline int fseek(FILE *f, long off, int whence) { return openos_seek(f->fd, (int)off, whence) < 0 ? -1 : 0; }
static inline long ftell(FILE *f) { return openos_seek(f->fd, 0, SEEK_CUR); }
static inline void rewind(FILE *f) { (void)openos_seek(f->fd, 0, SEEK_SET); }
static inline char *getenv(const char *name) { (void)name; return 0; }
static inline int system(const char *cmd) { (void)cmd; openos_set_errno(OPENOS_ENOSYS); return -1; }
static inline int execvp(const char *file, char *const argv[]) { (void)file; (void)argv; openos_set_errno(OPENOS_ENOSYS); return -1; }
static inline FILE *fdopen(int fd, const char *mode) { FILE *f; (void)mode; f = (FILE *)malloc(sizeof(FILE)); if (!f) return 0; f->fd = fd; f->flags = 0; f->error = 0; f->eof = 0; f->builtin = 0; return f; }
static inline pid_t fork(void) { openos_set_errno(OPENOS_ENOSYS); return -1; }
static inline pid_t waitpid(pid_t pid, int *status, int options) { (void)pid; (void)status; (void)options; openos_set_errno(OPENOS_ENOSYS); return -1; }

static inline int openos_tcc_digit_value(int ch) { if (ch >= '0' && ch <= '9') return ch - '0'; if (ch >= 'a' && ch <= 'z') return ch - 'a' + 10; if (ch >= 'A' && ch <= 'Z') return ch - 'A' + 10; return -1; }
static inline unsigned long openos_tcc_strtoul_impl(const char *nptr, char **endptr, int base, int *negp) { const char *s = nptr; unsigned long v = 0; int neg = 0, d; while (openos_isspace(*s)) s++; if (*s == '-' || *s == '+') { neg = (*s == '-'); s++; } if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; } else if (base == 0 && s[0] == '0') { base = 8; s++; } else if (base == 0) base = 10; while ((d = openos_tcc_digit_value(*s)) >= 0 && d < base) { v = v * (unsigned long)base + (unsigned long)d; s++; } if (endptr) *endptr = (char *)s; if (negp) *negp = neg; return v; }
static inline long strtol(const char *nptr, char **endptr, int base) { int neg = 0; unsigned long v = openos_tcc_strtoul_impl(nptr, endptr, base, &neg); return neg ? -(long)v : (long)v; }
static inline unsigned long strtoul(const char *nptr, char **endptr, int base) { int neg = 0; unsigned long v = openos_tcc_strtoul_impl(nptr, endptr, base, &neg); return neg ? (unsigned long)(-(long)v) : v; }
static inline long long strtoll(const char *nptr, char **endptr, int base) { return (long long)strtol(nptr, endptr, base); }
static inline unsigned long long strtoull(const char *nptr, char **endptr, int base) { return (unsigned long long)strtoul(nptr, endptr, base); }

struct tm { int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst; };
#ifndef OPENOS_TCC_TIMEVAL_DEFINED
#define OPENOS_TCC_TIMEVAL_DEFINED 1
struct timeval { long tv_sec; long tv_usec; };
struct timezone { int tz_minuteswest; int tz_dsttime; };
#endif
static inline int gettimeofday(struct timeval *tv, struct timezone *tz) { if (tv) { tv->tv_sec = (long)openos_time(0); tv->tv_usec = 0; } if (tz) { tz->tz_minuteswest = 0; tz->tz_dsttime = 0; } return 0; }
static inline time_t time(time_t *out) { openos_time_t now = openos_time(0); if (out) *out = (time_t)now; return (time_t)now; }
static inline struct tm *localtime(const time_t *timer) { static struct tm tm; unsigned int t = timer ? (unsigned int)*timer : openos_time(0); tm.tm_sec = t % 60; tm.tm_min = (t / 60) % 60; tm.tm_hour = (t / 3600) % 24; tm.tm_mday = 1; tm.tm_mon = 0; tm.tm_year = 70; tm.tm_wday = 4; tm.tm_yday = 0; tm.tm_isdst = 0; return &tm; }
static inline long double ldexpl(long double x, int exp) { while (exp > 0) { x *= 2.0L; exp--; } while (exp < 0) { x *= 0.5L; exp++; } return x; }
static inline double strtod(const char *nptr, char **endptr) { long v = strtol(nptr, endptr, 10); return (double)v; }
static inline float strtof(const char *nptr, char **endptr) { return (float)strtod(nptr, endptr); }
static inline long double strtold(const char *nptr, char **endptr) { return (long double)strtod(nptr, endptr); }
static unsigned long long __udivdi3(unsigned long long a, unsigned long long b) { unsigned long long q = 0, r = 0; int i; if (!b) return 0; for (i = 63; i >= 0; i--) { r = (r << 1) | ((a >> i) & 1ULL); if (r >= b) { r -= b; q |= (1ULL << i); } } return q; }
static unsigned long long __umoddi3(unsigned long long a, unsigned long long b) { unsigned long long r = 0; int i; if (!b) return 0; for (i = 63; i >= 0; i--) { r = (r << 1) | ((a >> i) & 1ULL); if (r >= b) r -= b; } return r; }
static inline char *strpbrk(const char *s, const char *accept) { const char *a; while (*s) { for (a = accept; *a; a++) if (*s == *a) return (char *)s; s++; } return 0; }
static inline char *realpath(const char *path, char *resolved) { size_t n; if (!path) return 0; n = strlen(path) + 1; if (!resolved) resolved = (char *)malloc(n); if (!resolved) return 0; memcpy(resolved, path, n); return resolved; }
static inline void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) { unsigned char *a = (unsigned char *)base; size_t i, j, k; for (i = 0; i < nmemb; i++) for (j = i + 1; j < nmemb; j++) if (compar(a + i * size, a + j * size) > 0) for (k = 0; k < size; k++) { unsigned char tmp = a[i * size + k]; a[i * size + k] = a[j * size + k]; a[j * size + k] = tmp; } }
static inline char *strerror(int err) { (void)err; return "openos error"; }
static inline void perror(const char *s) { if (s) { openos_write_str(s); openos_write_str(": "); } openos_write_str("openos error\n"); }

#define va_list __builtin_va_list
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)
#define va_copy(dst, src) __builtin_va_copy(dst, src)

#endif /* OPENOS_TCC_COMPAT_H */
