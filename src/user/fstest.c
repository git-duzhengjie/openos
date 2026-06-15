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

static void write_str(const char *s)
{
    syscall3(SYS_WRITE, 1, (int)s, strlen(s));
}

static void fail(int code, const char *msg)
{
    write_str(msg);
    syscall3(SYS_EXIT, code, 0, 0);
}

void _start(int argc, char **argv, char **envp)
{
    (void)argc;
    (void)argv;
    (void)envp;

    struct openos_stat st;
    struct openos_dirent de;
    char cwd[128];
    int found_bin = 0;
    int fd;
    int i;

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

    for (i = 0; i < 32; i++) {
        int r = syscall3(SYS_READDIR, (int)"/", i, (int)&de);
        if (r < 0)
            fail(8, "[fstest] readdir / failed\n");
        if (r == 0)
            break;
        if (strcmp(de.name, "bin") == 0)
            found_bin = 1;
    }
    if (!found_bin)
        fail(9, "[fstest] /bin not found in root dir\n");

    if (syscall3(SYS_STAT, (int)"hello", (int)&st, 0) < 0)
        fail(10, "[fstest] relative stat hello failed\n");
    if ((st.mode & FS_FILE) != FS_FILE)
        fail(11, "[fstest] hello is not file\n");

    if (syscall3(SYS_LSTAT, (int)"hello", (int)&st, 0) < 0)
        fail(12, "[fstest] lstat hello failed\n");
    if ((st.mode & FS_FILE) != FS_FILE)
        fail(13, "[fstest] lstat hello is not file\n");

    fd = syscall3(SYS_OPEN, (int)"hello", O_RDONLY, 0);
    if (fd < 0)
        fail(14, "[fstest] open hello failed\n");
    if (syscall3(SYS_FSTAT, fd, (int)&st, 0) < 0)
        fail(15, "[fstest] fstat hello failed\n");
    if ((st.mode & FS_FILE) != FS_FILE)
        fail(16, "[fstest] fstat hello is not file\n");
    if (syscall3(SYS_CLOSE, fd, 0, 0) < 0)
        fail(17, "[fstest] close hello failed\n");

    syscall3(SYS_CHDIR, (int)"/", 0, 0);
    write_str("[fstest] fs syscalls ok\n");
    syscall3(SYS_EXIT, 0, 0, 0);
}
