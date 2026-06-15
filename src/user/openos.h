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

#define WNOHANG         1
#define WIFEXITED(status)      (((status) & 0x7f) == 0)
#define WEXITSTATUS(status)    (((status) >> 8) & 0xff)

#define FS_FILE         0x1000
#define FS_DIR          0x2000
#define O_RDONLY        0
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

static inline void openos_write_str(const char *s)
{
    openos_write(1, s, openos_strlen(s));
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
