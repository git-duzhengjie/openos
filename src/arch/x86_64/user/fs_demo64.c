/*
 * M5.4a fs_demo64.c — end-to-end test for the writable VFS (ramfs64) layer
 * exercised from a real ring3 process.
 *
 * The kernel already hosts a writable in-RAM filesystem (gui64/ramfs64.c)
 * behind the unified VFS syscalls (open/read/write/close/lseek/mkdir/
 * unlink/rmdir/stat). This program proves, from user space, that a program
 * can create directories and files, write/append/seek/truncate, read them
 * back, stat them, and finally unlink/rmdir them — the create/write/query/
 * delete closed loop that M5.4 (package management) will build upon.
 *
 * It calls the STANDARD C symbols (printf/strcmp/strlen/memset) from the
 * M5.3 libc subset plus the openos64_* file API for the raw syscalls.
 *
 * Launch chain tail: /bin/libc_demo execve's into /bin/fs_demo.
 */

#include <stddef.h>
#include <stdint.h>

#include "openos64.h"
#include "libc/string.h"
#include "libc/stdio.h"

static int g_pass = 0;
static int g_fail = 0;

static void check(const char *name, int cond)
{
    if (cond) {
        g_pass++;
        printf("  PASS  %s\n", name);
    } else {
        g_fail++;
        printf("  FAIL  %s\n", name);
    }
}

int openos64_main(int argc, char **argv, char **envp)
{
    (void)argc; (void)argv; (void)envp;
    printf("\n[fs_demo] M5.4a writable-VFS end-to-end test\n");

    /* ---- 1. mkdir: create a writable working directory ---- */
    {
        int r = openos64_mkdir("/pkg", 0755);
        /* Accept success or "already exists" (idempotent across reboots
         * would matter once ramfs snapshots persist). */
        check("mkdir /pkg", r == 0 || r == -17 /*EEXIST*/);

        r = openos64_mkdir("/pkg/demo", 0755);
        check("mkdir /pkg/demo", r == 0 || r == -17);
    }

    /* ---- 2. create + write a file ---- */
    {
        const char *msg = "hello writable vfs\n";
        int fd = openos64_open("/pkg/demo/note.txt",
                               OPENOS64_O_WRONLY | OPENOS64_O_CREAT | OPENOS64_O_TRUNC,
                               0644);
        check("open O_CREAT note.txt", fd >= 0);

        if (fd >= 0) {
            long n = openos64_write(fd, msg, (openos64_size_t)strlen(msg));
            check("write full message", n == (long)strlen(msg));
            openos64_close(fd);
        }
    }

    /* ---- 3. read it back and compare ---- */
    {
        char buf[64];
        memset(buf, 0, sizeof(buf));
        int fd = openos64_open("/pkg/demo/note.txt", OPENOS64_O_RDONLY, 0);
        check("open RDONLY note.txt", fd >= 0);

        if (fd >= 0) {
            long n = openos64_read(fd, buf, sizeof(buf) - 1);
            check("read back length", n == 19);
            check("read back content", strcmp(buf, "hello writable vfs\n") == 0);
            openos64_close(fd);
        }
    }

    /* ---- 4. append + lseek verification ---- */
    {
        /* The kernel VFS has no O_APPEND; emulate append by opening RDWR,
         * seeking to end, then writing. */
        int fd = openos64_open("/pkg/demo/note.txt", OPENOS64_O_RDWR, 0);
        check("open RDWR note.txt", fd >= 0);
        if (fd >= 0) {
            long endpos = openos64_lseek(fd, 0, OPENOS64_SEEK_END);
            check("lseek END == 19", endpos == 19);
            const char *extra = "line2\n";
            openos64_write(fd, extra, (openos64_size_t)strlen(extra));
            openos64_close(fd);
        }

        char buf[64];
        memset(buf, 0, sizeof(buf));
        fd = openos64_open("/pkg/demo/note.txt", OPENOS64_O_RDONLY, 0);
        if (fd >= 0) {
            /* seek past first line (19 bytes) then read the appended part */
            long pos = openos64_lseek(fd, 19, OPENOS64_SEEK_SET);
            check("lseek SET to 19", pos == 19);
            long n = openos64_read(fd, buf, sizeof(buf) - 1);
            check("read appended line", n == 6 && strcmp(buf, "line2\n") == 0);
            openos64_close(fd);
        }
    }

    /* ---- 5. stat the file ---- */
    {
        openos64_stat_t st;
        memset(&st, 0, sizeof(st));
        int r = openos64_stat("/pkg/demo/note.txt", &st);
        check("stat note.txt", r == 0);
        /* first line 19 + appended 6 = 25 bytes */
        check("stat size == 25", st.size == 25);
    }

    /* ---- 6. unlink the file, confirm it is gone ---- */
    {
        int r = openos64_unlink("/pkg/demo/note.txt");
        check("unlink note.txt", r == 0);

        int fd = openos64_open("/pkg/demo/note.txt", OPENOS64_O_RDONLY, 0);
        check("open after unlink fails", fd < 0);
        if (fd >= 0) openos64_close(fd);
    }

    /* ---- 7. rmdir the now-empty directory ---- */
    {
        int r = openos64_rmdir("/pkg/demo");
        check("rmdir /pkg/demo", r == 0);
    }

    /* ---- summary ---- */
    printf("[fs_demo] results: %d passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) {
        printf("[fs] PASS\n");
        /* M5.4c launch-chain tail: hand off to /bin/opk_demo to exercise
         * the .opk package installer end to end. */
        char *const argv[] = { "/bin/opk_demo", 0 };
        printf("[fs_demo] execve /bin/opk_demo (M5.4c)...\n");
        openos64_execve("/bin/opk_demo", argv, envp);
        printf("[fs_demo] execve /bin/opk_demo FAILED\n");
        return 0;
    }
    printf("[fs] FAIL\n");
    return 1;
}
