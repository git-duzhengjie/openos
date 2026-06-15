/* ============================================================
 * openos - 系统调用实现
 * ============================================================ */

#include "../include/syscall.h"
#include "../include/process.h"
#include "../include/pmm.h"
#include "../include/serial.h"
#include "../include/vga.h"
#include "../include/input_buffer.h"
#include "../include/usermem.h"
#include "../proc/process.h"
#include "../fs/vfs.h"
#include "../include/string.h"
#include <stddef.h>  /* NULL */

/* VGA */
#define VGA ((volatile uint16_t *)0xB8000)

#define SYSCALL_IO_CHUNK 256u

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

static void vga_write_str(const char *s, uint8_t color) {
    int row = 1;
    static int col = 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] == '\n') {
            col = 0;
            row++;
        } else {
            VGA[row * 80 + col] = (uint16_t)((color << 8) | s[i]);
            col++;
            if (col >= 80) { col = 0; row++; }
        }
    }
}

static uint32_t syscall_write_user_buffer(int fd, const void *user_buf, uint32_t count)
{
    char chunk[SYSCALL_IO_CHUNK];
    uint32_t done = 0;

    if (count == 0)
        return 0;
    if (!user_ptr_valid(user_buf, count, USERMEM_READ))
        return (uint32_t)-1;

    if ((fd == STDOUT_FILENO || fd == STDERR_FILENO) && !vfs_get_file(fd)) {
        serial_write(fd == STDERR_FILENO ? "[USER-ERR] " : "[USER] ");
    }

    while (done < count) {
        uint32_t n = count - done;
        int written;

        if (n > SYSCALL_IO_CHUNK)
            n = SYSCALL_IO_CHUNK;
        if (copy_from_user(chunk, (const char *)user_buf + done, n) < 0)
            return done ? done : (uint32_t)-1;

        if ((fd == STDOUT_FILENO || fd == STDERR_FILENO) && !vfs_get_file(fd)) {
            for (uint32_t i = 0; i < n; i++) {
                serial_putc(chunk[i]);
                vga_putc(chunk[i]);
            }
            written = (int)n;
        } else {
            written = vfs_write(fd, chunk, n);
        }

        if (written < 0)
            return done ? done : (uint32_t)-1;
        if (written == 0)
            break;

        done += (uint32_t)written;
        if ((uint32_t)written < n)
            break;
    }

    return done;
}

static uint32_t syscall_read_user_buffer(int fd, void *user_buf, uint32_t count)
{
    char chunk[SYSCALL_IO_CHUNK];
    uint32_t done = 0;

    if (count == 0)
        return 0;
    if (!user_ptr_valid(user_buf, count, USERMEM_WRITE))
        return (uint32_t)-1;

    while (done < count) {
        uint32_t n = count - done;
        int got;

        if (n > SYSCALL_IO_CHUNK)
            n = SYSCALL_IO_CHUNK;

        if (fd == STDIN_FILENO && !vfs_get_file(fd)) {
            got = 0;
            while (got == 0) {
                while ((uint32_t)got < n && input_has_data()) {
                    chunk[got++] = input_getc();
                }
                if (got == 0) {
                    sched_yield();
                }
            }
        } else {
            got = vfs_read(fd, chunk, n);
        }
        if (got < 0)
            return done ? done : (uint32_t)-1;
        if (got == 0)
            break;

        if (copy_to_user((char *)user_buf + done, chunk, (uint32_t)got) < 0)
            return done ? done : (uint32_t)-1;

        done += (uint32_t)got;
        if ((uint32_t)got < n)
            break;
    }

    return done;
}

static int syscall_copy_user_path(char *dst, const char *user_path)
{
    return strncpy_from_user(dst, user_path, USERMEM_CSTR_MAX);
}

static void syscall_fill_user_stat(openos_stat_t *user_st, const inode_t *st)
{
    user_st->ino = st->ino;
    user_st->mode = st->mode;
    user_st->size = st->size;
    user_st->nlinks = st->nlinks;
    user_st->fs_type = st->fs_type;
}

/* ============================================================
 * 系统调用分发
 * ============================================================ */
uint32_t syscall_dispatch(uint32_t num,
                          uint32_t a, uint32_t b, uint32_t c,
                          uint32_t d, uint32_t e)
{
    (void)d; (void)e;
    switch (num) {
    case SYS_GETPID:
        return sys_getpid();

    case SYS_GETTID:
        return sys_gettid();

    case SYS_WRITE:
        return syscall_write_user_buffer((int)a, (const void *)b, c);

    case SYS_READ:
        return syscall_read_user_buffer((int)a, (void *)b, c);

    case SYS_EXIT:
        sys_exit((int)a);
        return 0;

    case SYS_SLEEP:
        thread_sleep(a);
        return 0;

    case SYS_YIELD:
        sched_yield();
        return 0;

    case SYS_MALLOC:
        {
            void *p = pmm_alloc_page();
            return (uint32_t)p;
        }

    case SYS_FREE:
        pmm_free_page((void *)a);
        return 0;

    case SYS_FORK:
        return sys_fork();

    case SYS_WAIT:
        {
            int status = 0;
            uint32_t ret;
            if (a && !user_ptr_valid((void *)a, sizeof(status), USERMEM_WRITE))
                return (uint32_t)-1;
            ret = sys_wait(a ? &status : NULL);
            if (a && copy_to_user((void *)a, &status, sizeof(status)) < 0)
                return (uint32_t)-1;
            return ret;
        }

    case SYS_WAITPID:
        {
            int status = 0;
            uint32_t ret;
            if (b && !user_ptr_valid((void *)b, sizeof(status), USERMEM_WRITE))
                return (uint32_t)-1;
            ret = sys_waitpid((int)a, b ? &status : NULL, (int)c);
            if (b && copy_to_user((void *)b, &status, sizeof(status)) < 0)
                return (uint32_t)-1;
            return ret;
        }

    case SYS_GETPPID:
        return sys_getppid();

    case SYS_OPEN:
        {
            char path[USERMEM_CSTR_MAX];
            if (syscall_copy_user_path(path, (const char *)a) < 0)
                return (uint32_t)-1;
            return (uint32_t)vfs_open(path, (int)b, (int)c);
        }

    case SYS_CLOSE:
        return (uint32_t)vfs_close((int)a);

    case SYS_READ_FD:
        return syscall_read_user_buffer((int)a, (void *)b, c);

    case SYS_WRITE_FD:
        return syscall_write_user_buffer((int)a, (const void *)b, c);

    case SYS_SEEK:
        return (uint32_t)vfs_seek((int)a, (int)b, (int)c);

    case SYS_DUP:
        return (uint32_t)vfs_dup((int)a);

    case SYS_DUP2:
        return (uint32_t)vfs_dup2((int)a, (int)b);

    case SYS_MKDIR:
        {
            char path[USERMEM_CSTR_MAX];
            if (syscall_copy_user_path(path, (const char *)a) < 0)
                return (uint32_t)-1;
            return (uint32_t)vfs_mkdir(path, (int)b);
        }

    case SYS_UNLINK:
        {
            char path[USERMEM_CSTR_MAX];
            if (syscall_copy_user_path(path, (const char *)a) < 0)
                return (uint32_t)-1;
            return (uint32_t)vfs_unlink(path);
        }

    case SYS_RMDIR:
        {
            char path[USERMEM_CSTR_MAX];
            if (syscall_copy_user_path(path, (const char *)a) < 0)
                return (uint32_t)-1;
            return (uint32_t)vfs_rmdir(path);
        }

    case SYS_EXEC:
        {
            char path[USERMEM_CSTR_MAX];
            char *const *argv = (char *const *)b;
            if (syscall_copy_user_path(path, (const char *)a) < 0)
                return (uint32_t)-1;
            return (uint32_t)sys_exec(path, argv);
        }

    case SYS_SPAWN:
        {
            char path[USERMEM_CSTR_MAX];
            char *const *argv = (char *const *)b;
            if (syscall_copy_user_path(path, (const char *)a) < 0)
                return (uint32_t)-1;
            return (uint32_t)spawn_user_process(path, argv);
        }

    case SYS_EXEC_ENV:
        {
            char path[USERMEM_CSTR_MAX];
            char *const *argv = (char *const *)b;
            char *const *envp = (char *const *)c;
            if (syscall_copy_user_path(path, (const char *)a) < 0)
                return (uint32_t)-1;
            return (uint32_t)sys_exec_env(path, argv, envp);
        }

    case SYS_SPAWN_ENV:
        {
            char path[USERMEM_CSTR_MAX];
            char *const *argv = (char *const *)b;
            char *const *envp = (char *const *)c;
            if (syscall_copy_user_path(path, (const char *)a) < 0)
                return (uint32_t)-1;
            return (uint32_t)spawn_user_process_env(path, argv, envp);
        }

    case SYS_STAT:
        {
            char path[USERMEM_CSTR_MAX];
            inode_t st;
            openos_stat_t user_st;
            if (!b || !user_ptr_valid((void *)b, sizeof(user_st), USERMEM_WRITE))
                return (uint32_t)-1;
            if (syscall_copy_user_path(path, (const char *)a) < 0)
                return (uint32_t)-1;
            if (vfs_stat(path, &st) < 0)
                return (uint32_t)-1;
            syscall_fill_user_stat(&user_st, &st);
            if (copy_to_user((void *)b, &user_st, sizeof(user_st)) < 0)
                return (uint32_t)-1;
            return 0;
        }

    case SYS_FSTAT:
        {
            file_t *f;
            openos_stat_t user_st;
            if (!b || !user_ptr_valid((void *)b, sizeof(user_st), USERMEM_WRITE))
                return (uint32_t)-1;
            f = vfs_get_file((int)a);
            if (!f || !f->inode)
                return (uint32_t)-1;
            syscall_fill_user_stat(&user_st, f->inode);
            if (copy_to_user((void *)b, &user_st, sizeof(user_st)) < 0)
                return (uint32_t)-1;
            return 0;
        }

    case SYS_LSTAT:
        {
            char path[USERMEM_CSTR_MAX];
            inode_t st;
            openos_stat_t user_st;
            if (!b || !user_ptr_valid((void *)b, sizeof(user_st), USERMEM_WRITE))
                return (uint32_t)-1;
            if (syscall_copy_user_path(path, (const char *)a) < 0)
                return (uint32_t)-1;
            /* 当前 VFS 尚未区分 symlink，lstat 先与 stat 等价。 */
            if (vfs_stat(path, &st) < 0)
                return (uint32_t)-1;
            syscall_fill_user_stat(&user_st, &st);
            if (copy_to_user((void *)b, &user_st, sizeof(user_st)) < 0)
                return (uint32_t)-1;
            return 0;
        }

    case SYS_GETCWD:
        {
            char cwd[MAX_PATH];
            uint32_t len;
            if (!a || b == 0 || b > USERMEM_CSTR_MAX)
                return (uint32_t)-1;
            if (!user_ptr_valid((void *)a, b, USERMEM_WRITE))
                return (uint32_t)-1;
            if (vfs_getcwd(cwd, sizeof(cwd)) < 0)
                return (uint32_t)-1;
            len = (uint32_t)strlen(cwd) + 1;
            if (len > b)
                return (uint32_t)-1;
            if (copy_to_user((void *)a, cwd, len) < 0)
                return (uint32_t)-1;
            return 0;
        }

    case SYS_CHDIR:
        {
            char path[USERMEM_CSTR_MAX];
            if (syscall_copy_user_path(path, (const char *)a) < 0)
                return (uint32_t)-1;
            return (uint32_t)vfs_chdir(path);
        }

    case SYS_READDIR:
        {
            char path[USERMEM_CSTR_MAX];
            dentry_t *de;
            openos_dirent_t user_de;
            if (!c || !user_ptr_valid((void *)c, sizeof(user_de), USERMEM_WRITE))
                return (uint32_t)-1;
            if (syscall_copy_user_path(path, (const char *)a) < 0)
                return (uint32_t)-1;
            de = vfs_readdir(path, (int)b);
            if (!de || !de->inode)
                return 0;
            user_de.ino = de->inode->ino;
            user_de.mode = de->inode->mode;
            user_de.size = de->inode->size;
            strncpy(user_de.name, de->name, sizeof(user_de.name) - 1);
            user_de.name[sizeof(user_de.name) - 1] = 0;
            if (copy_to_user((void *)c, &user_de, sizeof(user_de)) < 0)
                return (uint32_t)-1;
            return 1;
        }

    default:
        return 0xFFFFFFFF;
    }
}

/* ============================================================
 * 初始化系统调用
 * ============================================================ */
void syscall_init(void)
{
    /* IDT 中断 0x80 设置在 idt.c 中 */
    /* 汇编入口在 isr.asm 中作为 sysenter / int 0x80 处理 */
    vga_write_str("[SYSCALL] initialized\n", 0x0A);
}
