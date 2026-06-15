/* ============================================================
 * openos - syscall wrapper regression test
 * ============================================================ */

#include "openos.h"

int main(int argc, char **argv, char **envp)
{
    openos_stat_t st;
    openos_dirent_t de;
    char cwd[128];
    int fd;
    int pipefd[2];
    char ch = 0;
    void *ptr;

    (void)argc;
    (void)argv;
    (void)envp;

    openos_puts("[systest] checking syscall wrappers...");

    if (openos_getpid() <= 0)
        openos_fail(1, "[systest] getpid failed");
    if (openos_gettid() < 0)
        openos_fail(2, "[systest] gettid failed");
    if (openos_getppid() < 0)
        openos_fail(3, "[systest] getppid failed");
    if (openos_yield() < 0)
        openos_fail(4, "[systest] yield failed");
    if (openos_sleep(0) < 0)
        openos_fail(5, "[systest] sleep failed");

    if (openos_getcwd(cwd, sizeof(cwd)) < 0)
        openos_fail(6, "[systest] getcwd failed");
    if (openos_chdir("/bin") < 0)
        openos_fail(7, "[systest] chdir /bin failed");
    if (openos_getcwd(cwd, sizeof(cwd)) < 0 || openos_strcmp(cwd, "/bin") != 0)
        openos_fail(8, "[systest] cwd mismatch");
    if (openos_chdir("/") < 0)
        openos_fail(9, "[systest] chdir / failed");

    if (openos_stat("/bin/hello", &st) < 0)
        openos_fail(10, "[systest] stat failed");
    if (openos_lstat("/bin/hello", &st) < 0)
        openos_fail(11, "[systest] lstat failed");
    fd = openos_open("/bin/hello", O_RDONLY, 0);
    if (fd < 0)
        openos_fail(12, "[systest] open failed");
    if (openos_fstat(fd, &st) < 0)
        openos_fail(13, "[systest] fstat failed");
    if (openos_seek(fd, 0, 0) < 0)
        openos_fail(14, "[systest] seek failed");
    if (openos_close(fd) < 0)
        openos_fail(15, "[systest] close failed");

    if (openos_readdir_path("/", 0, &de) < 0)
        openos_fail(16, "[systest] readdir_path failed");

    if (openos_pipe(pipefd) < 0)
        openos_fail(17, "[systest] pipe failed");
    if (openos_write_fd(pipefd[1], "Q", 1) != 1)
        openos_fail(18, "[systest] pipe write failed");
    if (openos_read(pipefd[0], &ch, 1) != 1 || ch != 'Q')
        openos_fail(19, "[systest] pipe read failed");
    openos_close(pipefd[0]);
    openos_close(pipefd[1]);

    ptr = openos_malloc(16);
    if (!ptr)
        openos_fail(20, "[systest] malloc failed");
    openos_memset(ptr, 0, 16);
    if (openos_free(ptr) < 0)
        openos_fail(21, "[systest] free failed");

    openos_puts("[systest] syscall wrappers ok");
    return 0;
}
