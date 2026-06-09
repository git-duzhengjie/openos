/* ============================================================
 * openos - 简易 Shell 实现 (Phase 3)
 * ============================================================ */

#include "shell.h"
#include "serial.h"
#include "vga.h"
#include "string.h"
#include "types.h"
#include "pmm.h"
#include "process.h"
#include "input_buffer.h"
#include "../fs/vfs.h"
#include "../fs/ramfs.h"
#include "include/io.h"
#ifndef NULL
#define NULL ((void *)0)
#endif

/* 打印到 VGA + 串口 */
static void print(const char *s)
{
    vga_write(s);
    serial_write(s);
}

#define CMD_BUF_SIZE 256
#define MAX_ARGS 16

static char cmd_buf[CMD_BUF_SIZE];
static int cmd_pos = 0;

/* 当前工作目录 */
static char cwd[MAX_PATH] = "/";

/* ---- 辅助函数 ---- */
static void shell_prompt(void)
{
    print("openos:");
    print(cwd);
    print("$ ");
}

static int split_args(char *buf, char *argv[], int max)
{
    int argc = 0;
    int i = 0;
    while (buf[i] && argc < max)
    {
        while (buf[i] == ' ')
            i++;
        if (!buf[i])
            break;
        argv[argc++] = &buf[i];
        while (buf[i] && buf[i] != ' ')
            i++;
        if (buf[i])
            buf[i++] = '\0';
    }
    return argc;
}

static void make_path(const char *arg, char *out)
{
    if (!arg)
    {
        out[0] = '/';
        out[1] = '\0';
        return;
    }
    if (arg[0] == '/')
    {
        /* 绝对路径 */
        int i;
        for (i = 0; arg[i] && i < MAX_PATH - 1; i++)
            out[i] = arg[i];
        out[i] = '\0';
    }
    else
    {
        /* 相对路径 */
        int ci = 0;
        while (cwd[ci])
            ci++;
        for (int i = 0; i < ci && i < MAX_PATH - 1; i++)
            out[i] = cwd[i];
        if (ci > 1)
        {
            out[ci] = '/';
            ci++;
        }
        for (int i = 0; arg[i] && ci < MAX_PATH - 1; i++, ci++)
            out[ci] = arg[i];
        out[ci] = '\0';
    }
}

/* ---- 内置命令 ---- */

static void cmd_ls(const char *path)
{
    char full[MAX_PATH];
    make_path(path, full);

    dentry_t *d = vfs_path_lookup(full);

    if (!d || !d->inode)
    {
        print("ls: not found\n");
        return;
    }
    if ((d->inode->mode & 0xF000) != FS_DIR)
    {
        print(full);
        print("\n");
        return;
    }

    int idx = 0;
    dentry_t *child = d->child;
    while (child)
    {
        print(child->name);
        if (child->inode && (child->inode->mode & 0xF000) == FS_DIR)
            print("/");
        print("  ");
        child = child->sibling;
        idx++;
    }
    if (idx > 0)
        print("\n");
}

static void cmd_cat(const char *path)
{
    char full[MAX_PATH];
    make_path(path, full);

    int fd = vfs_open(full, O_RDONLY, 0);
    if (fd < 0)
    {
        print("cat: cannot open\n");
        return;
    }

    char buf[256];
    int n;
    while ((n = vfs_read(fd, buf, 255)) > 0)
    {
        buf[n] = '\0';
        print(buf);
    }
    print("\n");
    vfs_close(fd);
}

static void cmd_mkdir(const char *path)
{
    char full[MAX_PATH];
    make_path(path, full);
    if (vfs_mkdir(full, 0755) < 0)
        print("mkdir: failed\n");
}

static void cmd_touch(const char *path)
{
    char full[MAX_PATH];
    make_path(path, full);
    int fd = vfs_open(full, O_CREAT | O_RDWR, 0644);
    if (fd < 0)
    {
        print("touch: failed\n");
        return;
    }
    vfs_close(fd);
}

static void cmd_echo(int argc, char *argv[])
{
    for (int i = 1; i < argc; i++)
    {
        if (i > 1)
            print(" ");
        print(argv[i]);
    }
    print("\n");
}

static void cmd_cd(const char *path)
{
    if (!path)
    {
        cwd[0] = '/';
        cwd[1] = '\0';
        return;
    }
    char full[MAX_PATH];
    make_path(path, full);
    dentry_t *d = vfs_path_lookup(full);
    if (!d || !d->inode || (d->inode->mode & 0xF000) != FS_DIR)
    {
        print("cd: not a directory\n");
        return;
    }
    int i;
    for (i = 0; full[i] && i < MAX_PATH - 1; i++)
        cwd[i] = full[i];
    while (i > 1 && cwd[i - 1] == '/')
        i--; /* strip trailing slashes */
    cwd[i] = '\0';
}

static void cmd_pwd(void)
{
    print(cwd);
    print("\n");
}

static void cmd_rm(const char *path)
{
    char full[MAX_PATH];
    make_path(path, full);
    dentry_t *d = vfs_path_lookup(full);
    if (d && d->inode && (d->inode->mode & 0xF000) == FS_DIR)
    {
        print("rm: cannot remove directory, use rmdir\n");
        return;
    }
    if (vfs_unlink(full) < 0)
        print("rm: failed\n");
}

static void cmd_write(const char *path, const char *data)
{
    char full[MAX_PATH];
    make_path(path, full);
    int fd = vfs_open(full, O_CREAT | O_RDWR, 0644);
    if (fd < 0)
    {
        print("write: cannot open\n");
        return;
    }
    int len = 0;
    while (data[len])
        len++;
    vfs_write(fd, data, len);
    vfs_close(fd);
}

static void cmd_help(void)
{
    print("openos shell - Available commands:\n");
    print("  ls [path]       - List directory\n");
    print("  cat <file>      - Display file content\n");
    print("  mkdir <dir>     - Create directory\n");
    print("  touch <file>    - Create empty file\n");
    print("  rm <file>       - Delete file\n");
    print("  write <f> <txt> - Write text to file\n");
    print("  echo <text>     - Print text\n");
    print("  cd [path]       - Change directory\n");
    print("  pwd             - Print working directory\n");
    print("  help            - Show this help\n");
    print("  clear           - Clear screen\n");
    print("  yield           - Yield CPU\n");
    print("  exec <elf>      - Execute ELF program\n");
}

/* ---- Shell 主循环 ---- */
void shell_run(void)
{
    print("\n=== openos shell ===\n");
    print("Type 'help' for commands\n\n");

    cmd_buf[0] = '\0';
    cmd_pos = 0;
    shell_prompt();

    /* 从串口读取输入（轮询方式） */
    while (1)
    {
        /* 检查键盘输入 — 通过串口端口 0x3F8 读取 */
        /* 简化：使用 serial_read 如果可用，否则用键盘缓冲区 */
        /* Phase 3 先用串口回显模式 */

        /* 先把串口数据灌入输入缓冲区 */
        if ((inb(0x3FD) & 0x01))
        {
            char sc = inb(0x3F8);
            input_putc(sc);
        }
        /*读键盘硬件端口 */
        if ((inb(0x64) & 0x01))
        {
            uint8_t sc = inb(0x60);
            if (!(sc & 0x80))
            {
                char kc = 0;
                if (sc >= 0x02 && sc <= 0x0B)
                    kc = '1' + (sc - 0x02);
                else if (sc == 0x0E)
                    kc = '\b';
                else if (sc == 0x1C)
                    kc = '\n';
                else if (sc >= 0x10 && sc <= 0x19)
                    kc = 'q' + (sc - 0x10);
                else if (sc >= 0x1E && sc <= 0x26)
                    kc = 'a' + (sc - 0x1E);
                else if (sc >= 0x2C && sc <= 0x32)
                    kc = 'z' + (sc - 0x2C);
                else if (sc == 0x39)
                    kc = ' ';
                if (kc != 0)
                    input_putc(kc);
            }
            outb(0x20, 0x20);
        }
        /* 从统一输入缓冲区读取 */
        char c = input_getc();

        if (!c)
        {
            /* 没有输入时，暂停CPU等待中断 */
            continue;
        }

        if (c == '\r' || c == '\n')
        {
            cmd_buf[cmd_pos] = '\0';
            print("\n");

            /* 解析命令 */
            char *argv[MAX_ARGS];
            int argc = split_args(cmd_buf, argv, MAX_ARGS);

            if (argc > 0)
            {
                char *cmd = argv[0];
                if (strcmp(cmd, "ls") == 0)
                {
                    cmd_ls(argc > 1 ? argv[1] : ".");
                }
                else if (strcmp(cmd, "cat") == 0)
                {
                    if (argc > 1)
                        cmd_cat(argv[1]);
                    else
                        print("cat: missing argument\n");
                }
                else if (strcmp(cmd, "mkdir") == 0)
                {
                    if (argc > 1)
                        cmd_mkdir(argv[1]);
                    else
                        print("mkdir: missing argument\n");
                }
                else if (strcmp(cmd, "touch") == 0)
                {
                    if (argc > 1)
                        cmd_touch(argv[1]);
                    else
                        print("touch: missing argument\n");
                }
                else if (strcmp(cmd, "rm") == 0)
                {
                    if (argc > 1)
                        cmd_rm(argv[1]);
                    else
                        print("rm: missing argument\n");
                }
                else if (strcmp(cmd, "echo") == 0)
                {
                    cmd_echo(argc, argv);
                }
                else if (strcmp(cmd, "cd") == 0)
                {
                    cmd_cd(argc > 1 ? argv[1] : NULL);
                }
                else if (strcmp(cmd, "pwd") == 0)
                {
                    cmd_pwd();
                }
                else if (strcmp(cmd, "write") == 0)
                {
                    if (argc > 2)
                        cmd_write(argv[1], argv[2]);
                    else
                        print("write: need file and text\n");
                }
                else if (strcmp(cmd, "help") == 0)
                {
                    cmd_help();
                }
                else if (strcmp(cmd, "clear") == 0)
                {
                    vga_clear();
                }
                else if (strcmp(cmd, "yield") == 0)
                {
                    /* 协作式调度测试：主动让出 CPU */
                    serial_write("[YIELD] giving up CPU...\n");
                    __asm__ volatile("int $0x80" : : "a"(201) : "memory");
                }
                else if (strcmp(cmd, "exec") == 0)
                {
                    /* 执行 ELF 程序 */
                    if (argc > 1)
                    {
                        serial_write("[EXEC] loading ");
                        serial_write(argv[1]);
                        serial_write("\n");
                        /* syscall SYS_EXEC=221, path in ebx */
                        int ret;
                        __asm__ volatile(
                            "int $0x80"
                            : "=a"(ret)
                            : "a"(221), "b"(argv[1])
                            : "memory");
                        if (ret < 0)
                        {
                            print("exec: failed to load ");
                            print(argv[1]);
                            print("\n");
                        }
                    }
                    else
                    {
                        print("exec: missing argument\n");
                    }
                }
                else
                {
                    print(cmd);
                    print(": command not found\n");
                }
            }

            cmd_pos = 0;
            cmd_buf[0] = '\0';
            shell_prompt();
        }
        else if (c == 0x7F || c == 0x08)
        {
            /* 退格 */
            if (cmd_pos > 0)
            {
                cmd_pos--;
                cmd_buf[cmd_pos] = '\0';
                print("\b \b");
            }
        }
        else if (c >= ' ' && cmd_pos < CMD_BUF_SIZE - 1)
        {
            cmd_buf[cmd_pos++] = c;
            /* 回显 */
            char echo[2] = {c, '\0'};
            print(echo);
        }
    }
}
