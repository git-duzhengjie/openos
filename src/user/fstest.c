/* ============================================================
 * openos - filesystem syscall regression test
 * ============================================================ */

#include "openos.h"

void _start(int argc, char **argv, char **envp)
{
    (void)envp;

    if (argc > 1 && openos_strcmp(argv[1], "--leak-fd-child") == 0) {
        int child_fd = openos_open("/bin/hello", O_RDONLY, 0);
        if (child_fd != 0)
            openos_fail(30, "[fstest] child first fd mismatch\n");
        openos_write_str("[fstest] child leaked fd intentionally\n");
        openos_exit(0);
    }

    openos_stat_t st;
    openos_DIR *dir;
    openos_dirent_t *de;
    char cwd[128];
    int found_bin = 0;
    int fd;
    int child;
    int status;

    openos_write_str("[fstest] checking fs syscalls...\n");

    if (openos_stat("/bin", &st) < 0)
        openos_fail(1, "[fstest] stat /bin failed\n");
    if ((st.mode & FS_DIR) != FS_DIR)
        openos_fail(2, "[fstest] /bin is not dir\n");

    if (openos_getcwd(cwd, sizeof(cwd)) < 0)
        openos_fail(3, "[fstest] getcwd failed\n");
    if (openos_strcmp(cwd, "/") != 0)
        openos_fail(4, "[fstest] initial cwd mismatch\n");

    if (openos_chdir("/bin") < 0)
        openos_fail(5, "[fstest] chdir /bin failed\n");
    if (openos_getcwd(cwd, sizeof(cwd)) < 0)
        openos_fail(6, "[fstest] getcwd after chdir failed\n");
    if (openos_strcmp(cwd, "/bin") != 0)
        openos_fail(7, "[fstest] cwd after chdir mismatch\n");

    dir = openos_opendir("/");
    if (!dir)
        openos_fail(8, "[fstest] opendir / failed\n");
    while ((de = openos_readdir(dir)) != 0) {
        if (openos_strcmp(de->name, "bin") == 0)
            found_bin = 1;
    }
    if (openos_closedir(dir) < 0)
        openos_fail(9, "[fstest] closedir / failed\n");
    if (!found_bin)
        openos_fail(10, "[fstest] /bin not found in root dir\n");

    if (openos_stat("hello", &st) < 0)
        openos_fail(11, "[fstest] relative stat hello failed\n");
    if ((st.mode & FS_FILE) != FS_FILE)
        openos_fail(12, "[fstest] hello is not file\n");

    if (openos_lstat("hello", &st) < 0)
        openos_fail(13, "[fstest] lstat hello failed\n");
    if ((st.mode & FS_FILE) != FS_FILE)
        openos_fail(14, "[fstest] lstat hello is not file\n");

    fd = openos_open("hello", O_RDONLY, 0);
    if (fd < 0)
        openos_fail(15, "[fstest] open hello failed\n");
    if (openos_fstat(fd, &st) < 0)
        openos_fail(16, "[fstest] fstat hello failed\n");
    if ((st.mode & FS_FILE) != FS_FILE)
        openos_fail(17, "[fstest] fstat hello is not file\n");
    if (openos_close(fd) < 0)
        openos_fail(18, "[fstest] close hello failed\n");

    if (openos_chmod("hello", FS_FILE | 0640) < 0)
        openos_fail(19, "[fstest] chmod hello failed\n");
    if (openos_chown("hello", 1000, 100) < 0)
        openos_fail(20, "[fstest] chown hello failed\n");
    if (openos_stat("hello", &st) < 0)
        openos_fail(21, "[fstest] stat after chmod/chown failed\n");
    if ((st.mode & 0777) != 0640)
        openos_fail(22, "[fstest] chmod mode mismatch\n");
    if (st.uid != 1000 || st.gid != 100)
        openos_fail(23, "[fstest] chown owner mismatch\n");

    {
        char *child_argv[] = { "fstest", "--leak-fd-child", 0 };
        child = openos_spawn("/bin/fstest", child_argv);
    }
    if (child < 0)
        openos_fail(24, "[fstest] spawn fd leak child failed\n");
    if (openos_waitpid(child, &status, 0) != child)
        openos_fail(25, "[fstest] wait fd leak child failed\n");
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        openos_fail(26, "[fstest] fd leak child failed\n");

    fd = openos_open("hello", O_RDONLY, 0);
    if (fd != 0)
        openos_fail(27, "[fstest] fd table leaked across processes\n");
    if (openos_close(fd) < 0)
        openos_fail(28, "[fstest] close after leak check failed\n");

    openos_chdir("/");
    openos_write_str("[fstest] fs syscalls ok\n");
    openos_exit(0);
}
