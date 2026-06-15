/* ============================================================
 * openos - minimal user runtime helpers
 * ============================================================ */

#ifndef OPENOS_USER_OPENOS_H
#define OPENOS_USER_OPENOS_H

#define SYS_EXIT        1
#define SYS_GETPID      20
#define SYS_GETTID      21
#define SYS_WRITE       64
#define SYS_READ        63
#define SYS_MALLOC      73
#define SYS_FREE        74
#define SYS_SLEEP       200
#define SYS_YIELD       201
#define SYS_FORK        220
#define SYS_EXEC        221
#define SYS_WAIT        222
#define SYS_WAITPID     223
#define SYS_GETPPID     224
#define SYS_OPEN        225
#define SYS_CLOSE       226
#define SYS_READ_FD     227
#define SYS_WRITE_FD    228
#define SYS_SEEK        229
#define SYS_MKDIR       230
#define SYS_UNLINK      231
#define SYS_RMDIR       232
#define SYS_SPAWN       233
#define SYS_EXEC_ENV    234
#define SYS_SPAWN_ENV   235
#define SYS_STAT        236
#define SYS_GETCWD      237
#define SYS_CHDIR       238
#define SYS_READDIR     239
#define SYS_FSTAT       240
#define SYS_LSTAT       241
#define SYS_DUP         242
#define SYS_DUP2        243
#define SYS_PIPE        244

#define WNOHANG         1
#define WIFEXITED(status)      (((status) & 0x7f) == 0)
#define WEXITSTATUS(status)    (((status) >> 8) & 0xff)

#define FS_FILE         0x1000
#define FS_DIR          0x2000
#define O_RDONLY        0
#define STDIN_FILENO    0
#define STDOUT_FILENO   1
#define STDERR_FILENO   2
#define OPENOS_PATH_MAX 128

typedef unsigned int openos_uint32_t;

typedef struct openos_stat {
    openos_uint32_t ino;
    openos_uint32_t mode;
    openos_uint32_t size;
    openos_uint32_t nlinks;
    openos_uint32_t fs_type;
} openos_stat_t;

typedef struct openos_dirent {
    openos_uint32_t ino;
    openos_uint32_t mode;
    openos_uint32_t size;
    char name[32];
} openos_dirent_t;

static inline int openos_syscall3(int num, int a, int b, int c)
{
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a), "c"(b), "d"(c)
        : "memory"
    );
    return ret;
}

static inline void openos_exit(int code)
{
    openos_syscall3(SYS_EXIT, code, 0, 0);
    for (;;) {
        __asm__ volatile("pause");
    }
}

static inline int openos_strlen(const char *s)
{
    int n = 0;
    while (s && s[n])
        n++;
    return n;
}

static inline int openos_strcmp(const char *a, const char *b)
{
    int i = 0;

    if (!a || !b)
        return a == b ? 0 : (a ? 1 : -1);

    while (a[i] && b[i] && a[i] == b[i])
        i++;
    return (unsigned char)a[i] - (unsigned char)b[i];
}

static inline int openos_str_copy(char *dst, const char *src, int size)
{
    int i;

    if (!dst || !src || size <= 0)
        return -1;

    for (i = 0; i < size - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = 0;

    if (src[i])
        return -1;
    return 0;
}

static inline void *openos_memset(void *dst, int value, int len)
{
    unsigned char *p = (unsigned char *)dst;
    int i;

    if (!dst || len <= 0)
        return dst;

    for (i = 0; i < len; i++)
        p[i] = (unsigned char)value;
    return dst;
}

static inline void *openos_memcpy(void *dst, const void *src, int len)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    int i;

    if (!dst || !src || len <= 0)
        return dst;

    for (i = 0; i < len; i++)
        d[i] = s[i];
    return dst;
}

static inline void *openos_memmove(void *dst, const void *src, int len)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    int i;

    if (!dst || !src || len <= 0)
        return dst;

    if (d < s) {
        for (i = 0; i < len; i++)
            d[i] = s[i];
    } else if (d > s) {
        for (i = len - 1; i >= 0; i--)
            d[i] = s[i];
    }
    return dst;
}

static inline int openos_memcmp(const void *a, const void *b, int len)
{
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    int i;

    if (a == b || len <= 0)
        return 0;
    if (!a || !b)
        return a ? 1 : -1;

    for (i = 0; i < len; i++) {
        if (pa[i] != pb[i])
            return (int)pa[i] - (int)pb[i];
    }
    return 0;
}

static inline int openos_strncmp(const char *a, const char *b, int n)
{
    int i;

    if (n <= 0)
        return 0;
    if (!a || !b)
        return a == b ? 0 : (a ? 1 : -1);

    for (i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca != cb || ca == 0 || cb == 0)
            return (int)ca - (int)cb;
    }
    return 0;
}

static inline char *openos_strchr(const char *s, int ch)
{
    char c = (char)ch;

    if (!s)
        return 0;

    while (*s) {
        if (*s == c)
            return (char *)s;
        s++;
    }
    return c == 0 ? (char *)s : 0;
}

static inline char *openos_strrchr(const char *s, int ch)
{
    const char *last = 0;
    char c = (char)ch;

    if (!s)
        return 0;

    do {
        if (*s == c)
            last = s;
    } while (*s++);

    return (char *)last;
}

static inline char *openos_strstr(const char *haystack, const char *needle)
{
    int needle_len;
    int i;

    if (!haystack || !needle)
        return 0;
    if (!needle[0])
        return (char *)haystack;

    needle_len = openos_strlen(needle);
    for (i = 0; haystack[i]; i++) {
        if (openos_strncmp(&haystack[i], needle, needle_len) == 0)
            return (char *)&haystack[i];
    }
    return 0;
}

static inline int openos_isdigit(int ch)
{
    return ch >= '0' && ch <= '9';
}

static inline int openos_isspace(int ch)
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\v' || ch == '\f';
}

static inline int openos_atoi(const char *s)
{
    int sign = 1;
    int value = 0;

    if (!s)
        return 0;

    while (openos_isspace(*s))
        s++;
    if (*s == '-' || *s == '+') {
        if (*s == '-')
            sign = -1;
        s++;
    }
    while (openos_isdigit(*s)) {
        value = value * 10 + (*s - '0');
        s++;
    }
    return sign * value;
}

static inline char *openos_itoa(int value, char *buf, int base)
{
    char tmp[33];
    unsigned int v;
    int i = 0;
    int j = 0;
    int neg = 0;

    if (!buf || base < 2 || base > 16)
        return 0;

    if (value == 0) {
        buf[0] = '0';
        buf[1] = 0;
        return buf;
    }

    if (value < 0 && base == 10) {
        neg = 1;
        v = (unsigned int)(-value);
    } else {
        v = (unsigned int)value;
    }

    while (v > 0 && i < (int)sizeof(tmp)) {
        int digit = (int)(v % (unsigned int)base);
        tmp[i++] = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
        v /= (unsigned int)base;
    }

    if (neg)
        buf[j++] = '-';
    while (i > 0)
        buf[j++] = tmp[--i];
    buf[j] = 0;
    return buf;
}

static inline void openos_write(int fd, const char *s, int len)
{
    openos_syscall3(SYS_WRITE, fd, (int)s, len);
}

static inline int openos_open(const char *path, int flags, int mode)
{
    return openos_syscall3(SYS_OPEN, (int)path, flags, mode);
}

static inline int openos_close(int fd)
{
    return openos_syscall3(SYS_CLOSE, fd, 0, 0);
}

static inline int openos_read(int fd, void *buf, int len)
{
    return openos_syscall3(SYS_READ_FD, fd, (int)buf, len);
}

static inline int openos_write_fd(int fd, const void *buf, int len)
{
    return openos_syscall3(SYS_WRITE_FD, fd, (int)buf, len);
}

static inline int openos_dup(int oldfd)
{
    return openos_syscall3(SYS_DUP, oldfd, 0, 0);
}

static inline int openos_dup2(int oldfd, int newfd)
{
    return openos_syscall3(SYS_DUP2, oldfd, newfd, 0);
}

static inline int openos_pipe(int pipefd[2])
{
    return openos_syscall3(SYS_PIPE, (int)pipefd, 0, 0);
}

static inline void openos_write_str(const char *s)
{
    openos_write(1, s, openos_strlen(s));
}

static inline int openos_putchar(int ch)
{
    char c = (char)ch;
    return openos_write_fd(STDOUT_FILENO, &c, 1);
}

static inline int openos_puts(const char *s)
{
    if (s)
        openos_write_fd(STDOUT_FILENO, s, openos_strlen(s));
    openos_write_fd(STDOUT_FILENO, "\n", 1);
    return 0;
}

static inline int openos_print_int(int value)
{
    char buf[16];

    if (!openos_itoa(value, buf, 10))
        return -1;
    return openos_write_fd(STDOUT_FILENO, buf, openos_strlen(buf));
}

static inline int openos_printf(const char *fmt, ...)
{
    __builtin_va_list ap;
    int written = 0;

    if (!fmt)
        return -1;

    __builtin_va_start(ap, fmt);
    while (*fmt) {
        if (*fmt != '%') {
            openos_write_fd(STDOUT_FILENO, fmt, 1);
            written++;
            fmt++;
            continue;
        }

        fmt++;
        if (*fmt == 0)
            break;

        if (*fmt == '%') {
            openos_write_fd(STDOUT_FILENO, "%", 1);
            written++;
        } else if (*fmt == 's') {
            const char *s = __builtin_va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            openos_write_fd(STDOUT_FILENO, s, openos_strlen(s));
            written += openos_strlen(s);
        } else if (*fmt == 'c') {
            char c = (char)__builtin_va_arg(ap, int);
            openos_write_fd(STDOUT_FILENO, &c, 1);
            written++;
        } else if (*fmt == 'd' || *fmt == 'i') {
            char buf[16];
            openos_itoa(__builtin_va_arg(ap, int), buf, 10);
            openos_write_fd(STDOUT_FILENO, buf, openos_strlen(buf));
            written += openos_strlen(buf);
        } else if (*fmt == 'x') {
            char buf[16];
            openos_itoa(__builtin_va_arg(ap, int), buf, 16);
            openos_write_fd(STDOUT_FILENO, buf, openos_strlen(buf));
            written += openos_strlen(buf);
        } else {
            openos_write_fd(STDOUT_FILENO, "%", 1);
            openos_write_fd(STDOUT_FILENO, fmt, 1);
            written += 2;
        }
        fmt++;
    }
    __builtin_va_end(ap);
    return written;
}

static inline int openos_getpid(void)
{
    return openos_syscall3(SYS_GETPID, 0, 0, 0);
}

static inline int openos_getcwd(char *buf, int size)
{
    return openos_syscall3(SYS_GETCWD, (int)buf, size, 0);
}

static inline int openos_mkdir(const char *path, int mode)
{
    return openos_syscall3(SYS_MKDIR, (int)path, mode, 0);
}

static inline int openos_unlink(const char *path)
{
    return openos_syscall3(SYS_UNLINK, (int)path, 0, 0);
}

static inline int openos_rmdir(const char *path)
{
    return openos_syscall3(SYS_RMDIR, (int)path, 0, 0);
}

static inline int openos_spawn(const char *path, char *const argv[])
{
    return openos_syscall3(SYS_SPAWN, (int)path, (int)argv, 0);
}

static inline int openos_spawn_env(const char *path, char *const argv[], char *const envp[])
{
    return openos_syscall3(SYS_SPAWN_ENV, (int)path, (int)argv, (int)envp);
}

static inline int openos_waitpid(int pid, int *status, int options)
{
    return openos_syscall3(SYS_WAITPID, pid, (int)status, options);
}


typedef struct openos_DIR {
    char path[OPENOS_PATH_MAX];
    int index;
    int open;
    openos_dirent_t entry;
} openos_DIR;

static inline int openos_stat(const char *path, openos_stat_t *st)
{
    return openos_syscall3(SYS_STAT, (int)path, (int)st, 0);
}

static inline int openos_lstat(const char *path, openos_stat_t *st)
{
    return openos_syscall3(SYS_LSTAT, (int)path, (int)st, 0);
}

static inline openos_DIR *openos_opendir(const char *path)
{
    static openos_DIR dir;
    openos_stat_t st;

    if (!path)
        return 0;
    if (openos_stat(path, &st) < 0)
        return 0;
    if ((st.mode & FS_DIR) != FS_DIR)
        return 0;
    if (openos_str_copy(dir.path, path, sizeof(dir.path)) < 0)
        return 0;

    dir.index = 0;
    dir.open = 1;
    return &dir;
}

static inline openos_dirent_t *openos_readdir(openos_DIR *dir)
{
    int r;

    if (!dir || !dir->open)
        return 0;

    r = openos_syscall3(SYS_READDIR, (int)dir->path, dir->index, (int)&dir->entry);
    if (r <= 0)
        return 0;

    dir->index++;
    return &dir->entry;
}

static inline int openos_closedir(openos_DIR *dir)
{
    if (!dir || !dir->open)
        return -1;
    dir->open = 0;
    return 0;
}

static inline void openos_fail(int code, const char *msg)
{
    openos_write_str(msg);
    openos_exit(code);
}

#endif /* OPENOS_USER_OPENOS_H */
