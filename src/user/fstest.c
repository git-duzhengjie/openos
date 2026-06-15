/* ============================================================
 * openos - filesystem syscall regression test
 * ============================================================ */

#define SYS_EXIT    1
#define SYS_WRITE   64
#define SYS_OPEN    225
#define SYS_CLOSE   226
#define SYS_STAT    236
#define SYS_GETCWD  237
#define SYS_CHDIR   238
#define SYS_READDIR 239
#define SYS_FSTAT   240
#define SYS_LSTAT   241

#define FS_DIR      0x2000
#define FS_FILE     0x1000
#define O_RDONLY    0
#define OPENOS_PATH_MAX 128

struct openos_stat {
    unsigned int ino;
    unsigned int mode;
    unsigned int size;
    unsigned int nlinks;
    unsigned int fs_type;
};

struct openos_dirent {
    unsigned int ino;
    unsigned int mode;
    unsigned int size;
    char name[32];
};

struct openos_DIR {
    char path[OPENOS_PATH_MAX];
    int index;
    int open;
    struct openos_dirent entry;
};

static int syscall3(int num, int a, int b, int c)
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

static int strlen(const char *s)
{
    int n = 0;
    while (s && s[n])
        n++;
    return n;
}

static int strcmp(const char *a, const char *b)
{
    int i = 0;
    while (a[i] && b[i] && a[i] == b[i])
        i++;
    return (unsigned char)a[i] - (unsigned char)b[i];
}

static int str_copy(char *dst, const char *src, int size)
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

static void write_str(const char *s)
{
    syscall3(SYS_WRITE, 1, (int)s, strlen(s));
}

static void fail(int code, const char *msg)
{
    write_str(msg);
    syscall3(SYS_EXIT, code, 0, 0);
}

static struct openos_DIR *openos_opendir(const char *path)
{
    static struct openos_DIR dir;
    struct openos_stat st;

    if (!path)
        return 0;
    if (syscall3(SYS_STAT, (int)path, (int)&st, 0) < 0)
        return 0;
    if ((st.mode & FS_DIR) != FS_DIR)
        return 0;
    if (str_copy(dir.path, path, sizeof(dir.path)) < 0)
        return 0;

    dir.index = 0;
    dir.open = 1;
    return &dir;
}

static struct openos_dirent *openos_readdir(struct openos_DIR *dir)
{
    int r;

    if (!dir || !dir->open)
        return 0;

    r = syscall3(SYS_READDIR, (int)dir->path, dir->index, (int)&dir->entry);
    if (r <= 0)
        return 0;

    dir->index++;
    return &dir->entry;
}

static int openos_closedir(struct openos_DIR *dir)
{
    if (!dir || !dir->open)
        return -1;
    dir->open = 0;
    return 0;
}

void _start(int argc, char **argv, char **envp)
{
    (void)argc;
    (void)argv;
    (void)envp;

    struct openos_stat st;
    struct openos_DIR *dir;
    struct openos_dirent *de;
    char cwd[128];
    int found_bin = 0;
    int fd;

    write_str("[fstest] checking fs syscalls...\n");

    if (syscall3(SYS_STAT, (int)"/bin", (int)&st, 0) < 0)
        fail(1, "[fstest] stat /bin failed\n");
    if ((st.mode & FS_DIR) != FS_DIR)
        fail(2, "[fstest] /bin is not dir\n");

    if (syscall3(SYS_GETCWD, (int)cwd, sizeof(cwd), 0) < 0)
        fail(3, "[fstest] getcwd failed\n");
    if (strcmp(cwd, "/") != 0)
        fail(4, "[fstest] initial cwd mismatch\n");

    if (syscall3(SYS_CHDIR, (int)"/bin", 0, 0) < 0)
        fail(5, "[fstest] chdir /bin failed\n");
    if (syscall3(SYS_GETCWD, (int)cwd, sizeof(cwd), 0) < 0)
        fail(6, "[fstest] getcwd after chdir failed\n");
    if (strcmp(cwd, "/bin") != 0)
        fail(7, "[fstest] cwd after chdir mismatch\n");

    dir = openos_opendir("/");
    if (!dir)
        fail(8, "[fstest] opendir / failed\n");
    while ((de = openos_readdir(dir)) != 0) {
        if (strcmp(de->name, "bin") == 0)
            found_bin = 1;
    }
    if (openos_closedir(dir) < 0)
        fail(9, "[fstest] closedir / failed\n");
    if (!found_bin)
        fail(10, "[fstest] /bin not found in root dir\n");

    if (syscall3(SYS_STAT, (int)"hello", (int)&st, 0) < 0)
        fail(11, "[fstest] relative stat hello failed\n");
    if ((st.mode & FS_FILE) != FS_FILE)
        fail(12, "[fstest] hello is not file\n");

    if (syscall3(SYS_LSTAT, (int)"hello", (int)&st, 0) < 0)
        fail(13, "[fstest] lstat hello failed\n");
    if ((st.mode & FS_FILE) != FS_FILE)
        fail(14, "[fstest] lstat hello is not file\n");

    fd = syscall3(SYS_OPEN, (int)"hello", O_RDONLY, 0);
    if (fd < 0)
        fail(15, "[fstest] open hello failed\n");
    if (syscall3(SYS_FSTAT, fd, (int)&st, 0) < 0)
        fail(16, "[fstest] fstat hello failed\n");
    if ((st.mode & FS_FILE) != FS_FILE)
        fail(17, "[fstest] fstat hello is not file\n");
    if (syscall3(SYS_CLOSE, fd, 0, 0) < 0)
        fail(18, "[fstest] close hello failed\n");

    syscall3(SYS_CHDIR, (int)"/", 0, 0);
    write_str("[fstest] fs syscalls ok\n");
    syscall3(SYS_EXIT, 0, 0, 0);
}
