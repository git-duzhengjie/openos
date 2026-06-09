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

static const char *builtin_commands[] = {
    "ls",
    "cat",
    "mkdir",
    "touch",
    "rm",
    "write",
    "echo",
    "cd",
    "pwd",
    "help",
    "clear",
    "yield",
    "exec"
};

#define BUILTIN_COMMAND_COUNT (sizeof(builtin_commands) / sizeof(builtin_commands[0]))

static int shell_starts_with(const char *s, const char *prefix, int prefix_len)
{
    for (int i = 0; i < prefix_len; i++)
    {
        if (s[i] != prefix[i] || s[i] == '\0')
            return 0;
    }
    return 1;
}

static int shell_common_prefix_len(const char *a, const char *b)
{
    int i = 0;
    while (a[i] && b[i] && a[i] == b[i])
        i++;
    return i;
}

static int shell_is_command_completion_context(void)
{
    for (int i = 0; i < cmd_pos; i++)
    {
        if (cmd_buf[i] == ' ')
            return 0;
    }
    return 1;
}

static void shell_append_char(char c)
{
    if (cmd_pos < CMD_BUF_SIZE - 1)
    {
        cmd_buf[cmd_pos++] = c;
        cmd_buf[cmd_pos] = '\0';
        char echo[2] = {c, '\0'};
        print(echo);
    }
}

static void shell_redraw_input_line(void)
{
    shell_prompt();
    print(cmd_buf);
}

static void make_path(const char *arg, char *out);

static void shell_complete_command(void)
{
    const char *first_match = NULL;
    int match_count = 0;
    int common_len = 0;

    if (!shell_is_command_completion_context())
        return;

    cmd_buf[cmd_pos] = '\0';

    for (int i = 0; i < (int)BUILTIN_COMMAND_COUNT; i++)
    {
        const char *name = builtin_commands[i];
        if (shell_starts_with(name, cmd_buf, cmd_pos))
        {
            if (match_count == 0)
            {
                first_match = name;
                common_len = (int)strlen(name);
            }
            else
            {
                int len = shell_common_prefix_len(first_match, name);
                if (len < common_len)
                    common_len = len;
            }
            match_count++;
        }
    }

    if (match_count == 0)
        return;

    if (match_count == 1)
    {
        int len = (int)strlen(first_match);
        for (int i = cmd_pos; i < len; i++)
            shell_append_char(first_match[i]);
        if (cmd_pos < CMD_BUF_SIZE - 1)
            shell_append_char(' ');
        return;
    }

    if (common_len > cmd_pos)
    {
        for (int i = cmd_pos; i < common_len; i++)
            shell_append_char(first_match[i]);
        return;
    }

    print("\n");
    for (int i = 0; i < (int)BUILTIN_COMMAND_COUNT; i++)
    {
        const char *name = builtin_commands[i];
        if (shell_starts_with(name, cmd_buf, cmd_pos))
        {
            print(name);
            print("  ");
        }
    }
    print("\n");
    shell_redraw_input_line();
}

/* 路径补全：补全命令参数中的文件/目录路径 */
static void shell_complete_path(void)
{
    /* 找到最后一个空格位置（即当前正在输入的参数起点） */
    int last_space = -1;
    for (int i = 0; i < cmd_pos; i++)
    {
        if (cmd_buf[i] == ' ')
            last_space = i;
    }
    if (last_space < 0)
        return;

    int arg_start = last_space + 1;
    int arg_len = cmd_pos - arg_start;

    /* 判断命令：cd / mkdir 只补目录 */
    int only_dirs = 0;
    if (last_space > 0)
    {
        char first_word[MAX_NAME];
        int wi = 0;
        while (wi < last_space && wi < MAX_NAME - 1)
        {
            first_word[wi] = cmd_buf[wi];
            wi++;
        }
        first_word[wi] = '\0';
        if (strcmp(first_word, "cd") == 0 || strcmp(first_word, "mkdir") == 0)
            only_dirs = 1;
    }

    /* 提取当前参数作为前缀 */
    char prefix[MAX_NAME];
    int pi = 0;
    for (int i = 0; i < arg_len && pi < MAX_NAME - 1; i++)
        prefix[pi++] = cmd_buf[arg_start + i];
    prefix[pi] = '\0';
    int prefix_len = pi;

    /* 在参数中找到最后一个 '/' 分隔符 */
    int last_slash = -1;
    for (int i = 0; i < prefix_len; i++)
    {
        if (prefix[i] == '/')
            last_slash = i;
    }

    char dir_path[MAX_PATH];      /* 要遍历的目录全路径 */
    char match_prefix[MAX_NAME];  /* 要匹配的名字前缀 */
    int match_prefix_len;

    if (last_slash >= 0)
    {
        /* 有斜杠：目录部分 = 斜杠前内容，匹配前缀 = 斜杠后内容 */
        char dir_rel[MAX_PATH];
        int di = 0;
        for (int i = 0; i < last_slash && i < MAX_PATH - 1; i++)
            dir_rel[di++] = prefix[i];
        dir_rel[di] = '\0';

        if (dir_rel[0] == '/' || last_slash == 0)
        {
            /* 绝对路径或以 / 开头 */
            if (di == 0) {
                dir_path[0] = '/';
                dir_path[1] = '\0';
            } else {
                int di2 = 0;
                for (int i = 0; dir_rel[i] && di2 < MAX_PATH - 1; i++)
                    dir_path[di2++] = dir_rel[i];
                dir_path[di2] = '\0';
            }
        }
        else
        {
            /* 相对路径：拼接 cwd */
            make_path(dir_rel, dir_path);
        }

        /* 匹配前缀 = 最后一个斜杠后的内容 */
        int mpi = 0;
        for (int i = last_slash + 1; i < prefix_len && mpi < MAX_NAME - 1; i++)
            match_prefix[mpi++] = prefix[i];
        match_prefix[mpi] = '\0';
        match_prefix_len = mpi;
    }
    else
    {
        /* 没有斜杠：在当前目录查找，匹配前缀 = 整个参数 */
        make_path(".", dir_path);
        int mpi = 0;
        for (int i = 0; i < prefix_len && mpi < MAX_NAME - 1; i++)
            match_prefix[mpi++] = prefix[i];
        match_prefix[mpi] = '\0';
        match_prefix_len = mpi;
    }

    /* 查找目录 */
    dentry_t *d = vfs_path_lookup(dir_path);
    if (!d || !d->inode || (d->inode->mode & 0xF000) != FS_DIR)
        return;

    /* 收集匹配的子项 */
#define MAX_PATH_MATCHES 64
    const char *match_names[MAX_PATH_MATCHES];
    int match_is_dir[MAX_PATH_MATCHES];
    int match_count = 0;

    dentry_t *child = d->child;
    while (child && match_count < MAX_PATH_MATCHES)
    {
        int is_dir = child->inode && (child->inode->mode & 0xF000) == FS_DIR;
        if (!only_dirs || is_dir)
        {
            if (shell_starts_with(child->name, match_prefix, match_prefix_len))
            {
                match_names[match_count] = child->name;
                match_is_dir[match_count] = is_dir;
                match_count++;
            }
        }
        child = child->sibling;
    }

    if (match_count == 0)
        return;

    /* 计算公共前缀 */
    const char *first = match_names[0];
    int common_len = (int)strlen(first);
    for (int i = 1; i < match_count; i++)
    {
        int len = shell_common_prefix_len(first, match_names[i]);
        if (len < common_len)
            common_len = len;
    }

    if (match_count == 1)
    {
        /* 唯一匹配：补全完整名字 */
        const char *name = match_names[0];
        int name_len = (int)strlen(name);
        for (int i = match_prefix_len; i < name_len; i++)
            shell_append_char(name[i]);
        /* 目录追加 /，文件追加空格 */
        if (match_is_dir[0])
            shell_append_char('/');
        else
            shell_append_char(' ');
        return;
    }

    /* 多个匹配：填入公共前缀 */
    if (common_len > match_prefix_len)
    {
        for (int i = match_prefix_len; i < common_len; i++)
            shell_append_char(first[i]);
        return;
    }

    /* 公共前缀等于已输入内容 → 列出所有候选 */
    print("\n");
    for (int i = 0; i < match_count; i++)
    {
        print(match_names[i]);
        if (match_is_dir[i])
            print("/");
        print("  ");
    }
    print("\n");
    shell_redraw_input_line();
}

static void shell_complete(void)
{
    if (shell_is_command_completion_context())
        shell_complete_command();
    else
        shell_complete_path();
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
        else if (c == '\t')
        {
            shell_complete();
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
        else if (c >= ' ')
        {
            shell_append_char(c);
        }
    }
}
