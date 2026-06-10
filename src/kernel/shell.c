/* ============================================================
 * openos - 简�?Shell 实现 (Phase 3)
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
#include "../fs/tmpfs.h"
#include "../net/net.h"
#include "../net/discovery.h"
#include "../net/sync.h"
#include "../net/bus.h"
#include "ai.h"
#include "devmgr.h"
#include "ext4.h"
#include "include/io.h"
#ifndef NULL
#define NULL ((void *)0)
#endif

/* 打印�?VGA + 串口 */
static void print(const char *s)
{
    vga_write(s);
    serial_write(s);
}

static void shell_print_dec(int value)
{
    char buf[12];
    int i = 0;

    if (value == 0)
    {
        print("0");
        return;
    }

    while (value > 0 && i < (int)(sizeof(buf) - 1))
    {
        buf[i++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (i > 0)
    {
        char out[2];
        out[0] = buf[--i];
        out[1] = '\0';
        print(out);
    }
}

#define CMD_BUF_SIZE 256
#define MAX_ARGS 16

static char cmd_buf[CMD_BUF_SIZE];
static int cmd_pos = 0;      /* 当前命令长度 */
static int cmd_cursor = 0;   /* 当前光标在命令行内的位置 */

#define SHELL_HISTORY_SIZE 16
static char history[SHELL_HISTORY_SIZE][CMD_BUF_SIZE];
static int history_count = 0;
static int history_view = 0;

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
    "history",
    "help",
    "clear",
    "yield",
    "exec",
    "netinfo",
    "discovery",
    "sync",
    "ping_self",
    "ai_info",
    "ai_ask",
    "ai_backend",
    "ai_models",
    "ai_model_load",
    "ai_model_unload",
    "ai_repo",
    "ai_trust",
    "ai_ed25519",
    "ai_model_register",
    "ai_model_scan"
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

static void shell_pump_serial_input(void)
{
    while ((inb(0x3FD) & 0x01))
    {
        input_putc((char)inb(0x3F8));
    }
}

static char shell_read_input_char(int wait_loops)
{
    char c;

    while (wait_loops-- >= 0)
    {
        shell_pump_serial_input();
        c = input_getc();
        if (c)
            return c;
    }

    return 0;
}
static void shell_append_char(char c)
{
    if (cmd_pos >= CMD_BUF_SIZE - 1)
        return;
    /* 如果光标不在末尾，插入模式：右移后续字符 */
    if (cmd_cursor < cmd_pos) {
        for (int i = cmd_pos; i > cmd_cursor; i--)
            cmd_buf[i] = cmd_buf[i - 1];
    }
    cmd_buf[cmd_cursor] = c;
    cmd_pos++;
    cmd_cursor++;
    cmd_buf[cmd_pos] = '\0';
    /* 回显：从插入点重绘到末尾，再移光标回正确位置 */
    print(&cmd_buf[cmd_cursor - 1]);
    int overshoot = cmd_pos - cmd_cursor;
    if (overshoot > 0) {
        int vx, vy;
        vga_get_xy(&vx, &vy);
        int target = vx - overshoot;
        while (target < 0) { target += VGA_WIDTH; vy--; }
        vga_set_xy(target, vy);
    }
}

static void shell_redraw_input_line(void)
{
    shell_prompt();
    print(cmd_buf);
}

static void shell_copy_str(char *dst, const char *src)
{
    int i = 0;
    while (src[i] && i < CMD_BUF_SIZE - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void shell_set_buffer(const char *src)
{
    shell_copy_str(cmd_buf, src);
    cmd_pos = (int)strlen(cmd_buf);
    cmd_cursor = cmd_pos;
}

static void shell_save_history(void)
{
    if (cmd_pos <= 0)
        return;
    if (history_count > 0 && strcmp(history[history_count - 1], cmd_buf) == 0)
    {
        history_view = history_count;
        return;
    }
    if (history_count < SHELL_HISTORY_SIZE) {
        shell_copy_str(history[history_count], cmd_buf);
        history_count++;
    } else {
        for (int i = 1; i < SHELL_HISTORY_SIZE; i++)
            shell_copy_str(history[i - 1], history[i]);
        shell_copy_str(history[SHELL_HISTORY_SIZE - 1], cmd_buf);
    }
    history_view = history_count;
}

static void shell_history_save_file(void)
{
    int fd = vfs_open("/.shell_history", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0)
        return;
    for (int h = 0; h < history_count; h++)
    {
        int len = (int)strlen(history[h]);
        if (len > 0)
        {
            vfs_write(fd, history[h], len);
            vfs_write(fd, "\n", 1);
        }
    }
    vfs_close(fd);
}

static void shell_history_load_file(void)
{
    int fd = vfs_open("/.shell_history", O_RDONLY, 0);
    if (fd < 0)
        return;

    char buf[1024];
    int n = vfs_read(fd, buf, sizeof(buf) - 1);
    vfs_close(fd);
    if (n <= 0)
        return;
    buf[n] = '\0';

    history_count = 0;
    int line_start = 0;
    for (int i = 0; i <= n && history_count < SHELL_HISTORY_SIZE; i++)
    {
        if (buf[i] == '\n' || buf[i] == '\0')
        {
            int len = i - line_start;
            if (len > 0)
            {
                if (len >= CMD_BUF_SIZE)
                    len = CMD_BUF_SIZE - 1;
                for (int j = 0; j < len; j++)
                    history[history_count][j] = buf[line_start + j];
                history[history_count][len] = '\0';
                history_count++;
            }
            line_start = i + 1;
        }
    }
    history_view = history_count;
}

static void shell_clear_current_input(void)
{
    print("\r");
    shell_prompt();
    for (int i = 0; i < CMD_BUF_SIZE - 1; i++)
        print(" ");
    print("\r");
    shell_prompt();
}

static void shell_replace_input(const char *src)
{
    shell_clear_current_input();
    shell_set_buffer(src);
    print(cmd_buf);
}

static void shell_move_cursor_left(void)
{
    if (cmd_cursor <= 0)
        return;
    int vx, vy;
    vga_get_xy(&vx, &vy);
    if (vx > 0) {
        vx--;
    } else if (vy > 0) {
        vy--;
        vx = VGA_WIDTH - 1;
    }
    vga_set_xy(vx, vy);
    cmd_cursor--;
}

static void shell_move_cursor_right(void)
{
    if (cmd_cursor >= cmd_pos)
        return;
    int vx, vy;
    vga_get_xy(&vx, &vy);
    vx++;
    if (vx >= VGA_WIDTH) {
        vx = 0;
        vy++;
    }
    vga_set_xy(vx, vy);
    cmd_cursor++;
}

/* 移动光标到行首（Home）*/
static void shell_move_cursor_home(void)
{
    if (cmd_cursor <= 0)
        return;
    int vx, vy;
    vga_get_xy(&vx, &vy);
    int total_pos = vy * VGA_WIDTH + vx;
    int prompt_end = total_pos - cmd_cursor;
    vga_set_xy(prompt_end % VGA_WIDTH, prompt_end / VGA_WIDTH);
    cmd_cursor = 0;
}

/* 移动光标到行尾（End）*/
static void shell_move_cursor_end(void)
{
    if (cmd_cursor >= cmd_pos)
        return;
    int vx, vy;
    vga_get_xy(&vx, &vy);
    int total_pos = vy * VGA_WIDTH + vx;
    int prompt_end = total_pos - cmd_cursor;
    int target = prompt_end + cmd_pos;
    vga_set_xy(target % VGA_WIDTH, target / VGA_WIDTH);
    cmd_cursor = cmd_pos;
}

/* 删除光标处的字符（Delete 键）*/
static void shell_delete_char(void)
{
    if (cmd_cursor >= cmd_pos)
        return;

    int vx, vy;
    vga_get_xy(&vx, &vy);
    int original_total = vy * VGA_WIDTH + vx;

    for (int i = cmd_cursor; i < cmd_pos - 1; i++)
        cmd_buf[i] = cmd_buf[i + 1];
    cmd_pos--;
    cmd_buf[cmd_pos] = '\0';

    /* 从光标处重绘剩余内容，并用空格清掉旧尾字符 */
    print(&cmd_buf[cmd_cursor]);
    print(" ");

    /* 回到删除前的光标位置 */
    vga_set_xy(original_total % VGA_WIDTH, original_total / VGA_WIDTH);
}

/* 取消当前输入行（Ctrl+C）*/
static void shell_cancel_line(void)
{
    shell_move_cursor_end();
    print("^C\n");
    cmd_pos = 0;
    cmd_cursor = 0;
    cmd_buf[0] = '\0';
    history_view = history_count;
    shell_prompt();
}

static void shell_history_prev(void)
{
    if (history_count <= 0 || history_view <= 0)
        return;
    history_view--;
    shell_replace_input(history[history_view]);
}

static void shell_history_next(void)
{
    if (history_count <= 0 || history_view >= history_count)
        return;
    history_view++;
    if (history_view == history_count)
        shell_replace_input("");
    else
        shell_replace_input(history[history_view]);
}

static void shell_backspace(void)
{
    if (cmd_cursor <= 0)
        return;
    shell_move_cursor_left();
    for (int i = cmd_cursor; i < cmd_pos - 1; i++)
        cmd_buf[i] = cmd_buf[i + 1];
    cmd_pos--;
    cmd_buf[cmd_pos] = '\0';
    print(&cmd_buf[cmd_cursor]);
    print(" ");
    int overshoot = cmd_pos - cmd_cursor + 1;
    if (overshoot > 0) {
        int vx, vy;
        vga_get_xy(&vx, &vy);
        int target = vx - overshoot;
        while (target < 0) { target += VGA_WIDTH; vy--; }
        vga_set_xy(target, vy);
    }
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

/* 路径补全：补全命令参数中的文�?目录路径 */
static void shell_complete_path(void)
{
    /* 找到最后一个空格位置（即当前正在输入的参数起点�?*/
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

    /* 根据命令类型过滤补全候选 */
    int only_dirs = 0;
    int only_files = 0;
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
        else if (strcmp(first_word, "cat") == 0 || strcmp(first_word, "rm") == 0 ||
                 strcmp(first_word, "write") == 0 || strcmp(first_word, "exec") == 0)
            only_files = 1;
    }

    /* 提取当前参数作为前缀 */
    char prefix[MAX_NAME];
    int pi = 0;
    for (int i = 0; i < arg_len && pi < MAX_NAME - 1; i++)
        prefix[pi++] = cmd_buf[arg_start + i];
    prefix[pi] = '\0';
    int prefix_len = pi;

    /* 在参数中找到最后一�?'/' 分隔�?*/
    int last_slash = -1;
    for (int i = 0; i < prefix_len; i++)
    {
        if (prefix[i] == '/')
            last_slash = i;
    }

    char dir_path[MAX_PATH];      /* 要遍历的目录全路�?*/
    char match_prefix[MAX_NAME];  /* 要匹配的名字前缀 */
    int match_prefix_len;

    if (last_slash >= 0)
    {
        /* 有斜杠：目录部分 = 斜杠前内容，匹配前缀 = 斜杠后内�?*/
        char dir_rel[MAX_PATH];
        int di = 0;
        for (int i = 0; i < last_slash && i < MAX_PATH - 1; i++)
            dir_rel[di++] = prefix[i];
        dir_rel[di] = '\0';

        if (dir_rel[0] == '/' || last_slash == 0)
        {
            /* 绝对路径或以 / 开�?*/
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
            /* 相对路径：拼�?cwd */
            make_path(dir_rel, dir_path);
        }

        /* 匹配前缀 = 最后一个斜杠后的内�?*/
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

    /* 收集匹配的子�?*/
#define MAX_PATH_MATCHES 64
    const char *match_names[MAX_PATH_MATCHES];
    int match_is_dir[MAX_PATH_MATCHES];
    int match_count = 0;

    dentry_t *child = d->child;
    while (child && match_count < MAX_PATH_MATCHES)
    {
        int is_dir = child->inode && (child->inode->mode & 0xF000) == FS_DIR;
        if ((!only_dirs || is_dir) && (!only_files || !is_dir))
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
        /* 唯一匹配：补全完整名�?*/
        const char *name = match_names[0];
        int name_len = (int)strlen(name);
        for (int i = match_prefix_len; i < name_len; i++)
            shell_append_char(name[i]);
        /* 目录追加 /，文件追加空�?*/
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

    /* 公共前缀等于已输入内容：列出所有候选 */
    print("\n");
    for (int i = 0; i < match_count; i++)
    {
        print(match_names[i]);
        if (match_is_dir[i])
            print("/");
        print("  ");
        if ((i + 1) % 4 == 0)
            print("\n");
    }
    if (match_count % 4 != 0)
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
    char *src = buf;
    char *dst = buf;

    while (*src && argc < max)
    {
        char quote = '\0';

        while (*src == ' ' || *src == '\t')
            src++;
        if (!*src)
            break;

        argv[argc++] = dst;
        while (*src)
        {
            if (quote)
            {
                if (*src == quote)
                {
                    quote = '\0';
                    src++;
                }
                else
                    *dst++ = *src++;
            }
            else if (*src == '\'' || *src == '"')
            {
                quote = *src++;
            }
            else if (*src == ' ' || *src == '\t')
            {
                src++;
                break;
            }
            else
                *dst++ = *src++;
        }

        *dst++ = '\0';
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

static void cmd_cat_append(const char *path, const char *text)
{
    if (!path || !text)
    {
        print("cat: usage: cat >> <file> <text>\n");
        return;
    }

    char full[MAX_PATH];
    make_path(path, full);

    int fd = vfs_open(full, O_CREAT | O_RDWR, 0644);
    if (fd < 0)
    {
        print("cat: cannot open\n");
        return;
    }

    if (vfs_seek(fd, 0, SEEK_END) < 0)
    {
        print("cat: seek failed\n");
        vfs_close(fd);
        return;
    }

    int len = strlen(text);
    if (len > 0 && vfs_write(fd, text, len) < 0)
    {
        print("cat: write failed\n");
        vfs_close(fd);
        return;
    }

    if (vfs_write(fd, "\n", 1) < 0)
        print("cat: write failed\n");

    vfs_close(fd);
}

static void cmd_mkdir(const char *path)
{
    if (!path || path[0] == '\0')
    {
        print("mkdir: usage: mkdir <path>\n");
        return;
    }

    char full[MAX_PATH];
    make_path(path, full);
    if (vfs_path_lookup(full))
    {
        print("mkdir: already exists\n");
        return;
    }
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

static void cmd_echo_append(const char *path, const char *text)
{
    if (!path || !text)
    {
        print("echo: usage: echo <text> >> <file>\n");
        return;
    }

    char full[MAX_PATH];
    make_path(path, full);

    int fd = vfs_open(full, O_CREAT | O_RDWR, 0644);
    if (fd < 0)
    {
        print("echo: cannot open file\n");
        return;
    }

    if (vfs_seek(fd, 0, SEEK_END) < 0)
    {
        print("echo: seek failed\n");
        vfs_close(fd);
        return;
    }

    int len = strlen(text);
    if (len > 0 && vfs_write(fd, text, len) < 0)
    {
        print("echo: write failed\n");
        vfs_close(fd);
        return;
    }

    if (vfs_write(fd, "\n", 1) < 0)
        print("echo: write failed\n");

    vfs_close(fd);
}

static void cmd_cd(const char *path)
{
    if (!path)
    {
        vfs_chdir("/");
        strncpy(cwd, "/", sizeof(cwd)-1);
        cwd[sizeof(cwd)-1] = '\0';
        return;
    }
    if (vfs_chdir(path) < 0)
    {
        print("cd: failed\n");
        return;
    }
    vfs_getcwd(cwd, sizeof(cwd));
}

static void cmd_pwd(void)
{
    char buf[MAX_PATH];
    if (vfs_getcwd(buf, sizeof(buf)) < 0)
    {
        print("pwd: failed\n");
        return;
    }
    print(buf);
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

    int fd = vfs_open(full, O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd < 0)
    {
        /* Some filesystem drivers may not support truncate yet.  Fall back to
         * recreate semantics so overwrite commands do not leave stale tails. */
        vfs_unlink(full);
        fd = vfs_open(full, O_CREAT | O_RDWR, 0644);
    }
    if (fd < 0)
    {
        print("write: cannot open\n");
        return;
    }
    int len = 0;
    while (data[len])
        len++;
    if (len > 0 && vfs_write(fd, data, len) < 0)
        print("write: failed\n");
    vfs_close(fd);
}

static void cmd_history(void)
{
    if (history_count <= 0)
    {
        print("history: no commands\n");
        return;
    }

    for (int i = 0; i < history_count; i++)
    {
        print("  ");
        if (i + 1 < 10)
            print(" ");
        shell_print_dec(i + 1);
        print("  ");
        print(history[i]);
        print("\n");
    }
}

static void cmd_help(void)
{
    print("openos shell - Available commands:\n");
    print("  ls [path]       - List directory\n");
    print("  cat <file>      - Display file content\n");
    print("  cat >> <file> <text> - Append text to file\n");
    print("  mkdir <dir>     - Create directory\n");
    print("  touch <file>    - Create empty file\n");
    print("  rm <file>       - Delete file\n");
    print("  write <f> <txt> - Write text to file\n");
    print("  echo <text>     - Print text\n");
    print("  echo <text> >> <file> - Append text to file\n");
    print("  cd [path]       - Change directory\n");
    print("  pwd             - Print working directory\n");
    print("  history         - Show command history\n");
    print("  mkext4 [dev]    - Format test EXT4 volume (default ram0)\n");
    print("  mount_ext4 [dev] [path] - Mount EXT4 volume read-only (default ram0 /mnt)\n");
    print("  mount_tmpfs [path] - Mount tmpfs memory filesystem (default /tmp)\n");
    print("  netinfo         - Show network stack information\n");
    print("  discovery [scan|peers|announce|bye|name <n>|caps <c>|auth|auth_secret <s>|auth_peer <id>]\n");
    print("  sync [info|items|tasks|reliable|put|del|push|push_all|offer|accept|done] - Cross-device sync/tasks\n");
    print("  bus [info|stats|subs|pub <topic> <payload>|pub_local <topic> <payload>|sub <topic>] - Message bus\n");
    print("  ping_self       - Send ICMP echo to loopback network device\n");
    print("  ai_info         - Show AI engine status\n");
    print("  ai_ask <text>   - Ask AI engine with current backend\n");
    print("  ai_backend [local|cloud|hybrid] - Show or set AI backend\n");
    print("  ai_models       - List AI models\n");
    print("  ai_model_load <name> - Load and select AI model\n");
    print("  ai_model_unload <name> - Unload AI model\n");
    print("  ai_repo [path]  - Show or set AI model repository\n");
    print("  ai_trust [path|load <keyfile>] - Show/set trust root or load trusted key\n");
    print("  ai_ed25519 [selftest|verify_sha256 <pub> <sha256> <sig>] - Test/verify Ed25519\n");
    print("  ai_model_register <manifest> - Register model manifest\n");
    print("  ai_model_scan   - Scan repository and register manifests\n");
    print("  devices         - List registered kernel devices\n");
    print("  hotplug         - Show pending hotplug events\n");
    print("  hotplug_poll    - Pop one hotplug event from queue\n");
    print("  help            - Show this help\n");
    print("  clear           - Clear screen\n");
    print("  yield           - Yield CPU\n");
    print("  exec <elf>      - Execute ELF program\n");
}

/* ---- Shell 主循�?---- */
void shell_run(void)
{
    print("\n=== openos shell ===\n");
    print("Type 'help' for commands\n\n");

    cmd_buf[0] = '\0';
    cmd_pos = 0;
    cmd_cursor = 0;
    shell_history_load_file();
    history_view = history_count;
    shell_prompt();

    /* 从串口读取输入（轮询方式�?*/
    while (1)
    {
        /* 检查键盘输�?�?通过串口端口 0x3F8 读取 */
        /* 简化：使用 serial_read 如果可用，否则用键盘缓冲�?*/
        /* Phase 3 先用串口回显模式 */

        /* 先把串口数据灌入输入缓冲�?*/
        /* 从统一输入缓冲区读取；同时把串口数据灌入输入缓冲区 */
        char c = shell_read_input_char(0);

        if (!c)
        {
            /* 没有输入时，暂停CPU等待中断 */
            continue;
        }

        if (c == '\r' || c == '\n')
        {
            cmd_buf[cmd_pos] = '\0';
            print("\n");
            shell_save_history();
            shell_history_save_file();

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
                    if (argc > 2 && strcmp(argv[1], ">>") == 0)
                    {
                        /* cat >> <file> <text ...> */
                        if (argc > 3)
                        {
                            char text_buf[256];
                            int pos = 0;
                            for (int i = 3; i < argc; i++)
                            {
                                if (i > 3 && pos < 255)
                                    text_buf[pos++] = ' ';
                                int k = 0;
                                while (argv[i][k] && pos < 255)
                                    text_buf[pos++] = argv[i][k++];
                            }
                            text_buf[pos] = '\0';
                            cmd_cat_append(argv[2], text_buf);
                        }
                        else
                            print("cat: missing file or text\n");
                    }
                    else if (argc > 1)
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
                    /* echo <text...> >> <file> */
                    int redirect = -1;
                    for (int i = 1; i < argc - 1; i++)
                    {
                        if (strcmp(argv[i], ">>") == 0)
                        {
                            redirect = i;
                            break;
                        }
                    }
                    if (redirect > 0)
                    {
                        /* 拼接 >> 前面的所有参数作为文本 */
                        char text_buf[256];
                        int pos = 0;
                        for (int i = 1; i < redirect; i++)
                        {
                            if (i > 1 && pos < 255)
                                text_buf[pos++] = ' ';
                            int k = 0;
                            while (argv[i][k] && pos < 255)
                                text_buf[pos++] = argv[i][k++];
                        }
                        text_buf[pos] = '\0';
                        if (redirect + 1 < argc)
                            cmd_echo_append(argv[redirect + 1], text_buf);
                        else
                            print("echo: missing filename\n");
                    }
                    else
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
                else if (strcmp(cmd, "history") == 0)
                {
                    cmd_history();
                }
                else if (strcmp(cmd, "mkext4") == 0)
                {
                    const char *dev = argc > 1 ? argv[1] : "ram0";
                    if (ext4_format_test_volume(dev) < 0)
                        print("mkext4: format failed\n");
                    else
                        print("mkext4: ok\n");
                }
                else if (strcmp(cmd, "mount_ext4") == 0)
                {
                    const char *dev = argc > 1 ? argv[1] : "ram0";
                    const char *path = argc > 2 ? argv[2] : "/mnt";
                    if (ext4_mount(dev, path) < 0)
                        print("mount_ext4: failed\n");
                    else
                        print("mount_ext4: ok\n");
                }
                else if (strcmp(cmd, "mount_tmpfs") == 0)
                {
                    const char *path = argc > 1 ? argv[1] : "/tmp";
                    if (tmpfs_mount(path) < 0)
                        print("mount_tmpfs: failed\n");
                    else
                        print("mount_tmpfs: ok\n");
                }
                else if (strcmp(cmd, "write") == 0)
                {
                    if (argc > 2)
                    {
                        char text_buf[256];
                        int pos = 0;
                        for (int i = 2; i < argc; i++)
                        {
                            if (i > 2 && pos < 255)
                                text_buf[pos++] = ' ';
                            int k = 0;
                            while (argv[i][k] && pos < 255)
                                text_buf[pos++] = argv[i][k++];
                        }
                        text_buf[pos] = '\0';
                        cmd_write(argv[1], text_buf);
                    }
                    else
                        print("write: need file and text\n");
                }
                else if (strcmp(cmd, "netinfo") == 0)
                {
                    net_print_info();
                }
                else if (strcmp(cmd, "discovery") == 0)
                {
                    if (argc < 2)
                    {
                        discovery_print_info();
                    }
                    else if (strcmp(argv[1], "scan") == 0 || strcmp(argv[1], "query") == 0)
                    {
                        if (discovery_query() < 0)
                            print("discovery: scan failed\n");
                        else
                            print("discovery: query broadcast sent\n");
                    }
                    else if (strcmp(argv[1], "peers") == 0)
                    {
                        discovery_print_peers();
                    }
                    else if (strcmp(argv[1], "auth") == 0)
                    {
                        discovery_print_auth();
                    }
                    else if (strcmp(argv[1], "auth_secret") == 0)
                    {
                        if (argc < 3 || discovery_set_auth_secret(argv[2]) < 0)
                            print("discovery: invalid auth secret\n");
                        else
                            print("discovery: auth secret configured\n");
                    }
                    else if (strcmp(argv[1], "auth_peer") == 0)
                    {
                        if (argc < 3 || discovery_auth_peer(argv[2]) < 0)
                            print("discovery: auth peer failed\n");
                        else
                            print("discovery: auth challenge sent\n");
                    }
                    else if (strcmp(argv[1], "announce") == 0)
                    {
                        if (discovery_announce() < 0)
                            print("discovery: announce failed\n");
                        else
                            print("discovery: hello broadcast sent\n");
                    }
                    else if (strcmp(argv[1], "bye") == 0)
                    {
                        if (discovery_goodbye() < 0)
                            print("discovery: bye failed\n");
                        else
                            print("discovery: bye broadcast sent\n");
                    }
                    else if (strcmp(argv[1], "name") == 0)
                    {
                        if (argc < 3 || discovery_set_local_name(argv[2]) < 0)
                            print("discovery: invalid name\n");
                        else
                            print("discovery: name updated\n");
                    }
                    else if (strcmp(argv[1], "caps") == 0)
                    {
                        if (argc < 3 || discovery_set_local_capabilities(argv[2]) < 0)
                            print("discovery: invalid capabilities\n");
                        else
                            print("discovery: capabilities updated\n");
                    }
                    else
                    {
                        print("usage: discovery [scan|peers|announce|bye|name <n>|caps <c>|auth|auth_secret <s>|auth_peer <id>]\n");
                    }
                }
                else if (strcmp(cmd, "sync") == 0)
                {
                    if (argc < 2 || strcmp(argv[1], "info") == 0)
                    {
                        sync_print_info();
                    }
                    else if (strcmp(argv[1], "items") == 0)
                    {
                        sync_print_items();
                    }
                    else if (strcmp(argv[1], "tasks") == 0)
                    {
                        sync_print_tasks();
                    }
                    else if (strcmp(argv[1], "reliable") == 0)
                    {
                        sync_print_reliable();
                    }
                    else if (strcmp(argv[1], "put") == 0)
                    {
                        if (argc < 4 || sync_put(argv[2], argv[3]) < 0)
                            print("sync: put failed\n");
                        else
                            print("sync: item updated and broadcast\n");
                    }
                    else if (strcmp(argv[1], "del") == 0)
                    {
                        if (argc < 3 || sync_delete(argv[2]) < 0)
                            print("sync: delete failed\n");
                        else
                            print("sync: delete broadcast\n");
                    }
                    else if (strcmp(argv[1], "push") == 0)
                    {
                        if (argc < 3 || sync_broadcast_key(argv[2]) < 0)
                            print("sync: push failed\n");
                        else
                            print("sync: item broadcast\n");
                    }
                    else if (strcmp(argv[1], "push_all") == 0)
                    {
                        int sent = sync_broadcast_all();
                        print("sync: broadcast items ");
                        shell_print_dec(sent);
                        print("\n");
                    }
                    else if (strcmp(argv[1], "offer") == 0)
                    {
                        const char *target = 0;
                        if (argc >= 6)
                            target = argv[5];
                        if (argc < 5 || sync_task_offer(argv[2], argv[3], argv[4], target) < 0)
                            print("sync: offer failed\n");
                        else
                            print("sync: task offered\n");
                    }
                    else if (strcmp(argv[1], "accept") == 0)
                    {
                        if (argc < 3 || sync_task_accept(argv[2]) < 0)
                            print("sync: accept failed\n");
                        else
                            print("sync: task accepted\n");
                    }
                    else if (strcmp(argv[1], "done") == 0)
                    {
                        if (argc < 4 || sync_task_done(argv[2], argv[3]) < 0)
                            print("sync: done failed\n");
                        else
                            print("sync: task done\n");
                    }
                    else
                    {
                        print("usage: sync [info|items|tasks|reliable|put <k> <v>|del <k>|push <k>|push_all|offer <id> <title> <payload> [target]|accept <id>|done <id> <result>]\n");
                    }
                }
                else if (strcmp(cmd, "bus") == 0)
                {
                    if (argc < 2 || strcmp(argv[1], "info") == 0)
                    {
                        bus_print_info();
                    }
                    else if (strcmp(argv[1], "stats") == 0)
                    {
                        bus_print_stats();
                    }
                    else if (strcmp(argv[1], "subs") == 0)
                    {
                        bus_print_subscribers();
                    }
                    else if (strcmp(argv[1], "pub") == 0)
                    {
                        if (argc < 4 || bus_publish(argv[2], argv[3], BUS_PUBLISH_ALL) < 0)
                            print("bus: publish failed\n");
                        else
                            print("bus: published local+remote\n");
                    }
                    else if (strcmp(argv[1], "pub_local") == 0)
                    {
                        if (argc < 4 || bus_publish(argv[2], argv[3], BUS_PUBLISH_LOCAL) < 0)
                            print("bus: local publish failed\n");
                        else
                            print("bus: published local\n");
                    }
                    else if (strcmp(argv[1], "sub") == 0)
                    {
                        if (argc < 3 || bus_shell_subscribe(argv[2]) < 0)
                            print("bus: subscribe failed\n");
                        else
                            print("bus: shell subscriber updated\n");
                    }
                    else
                    {
                        print("usage: bus [info|stats|subs|pub <topic> <payload>|pub_local <topic> <payload>|sub <topic>]\n");
                    }
                }
                else if (strcmp(cmd, "ping_self") == 0)
                {
                    if (net_ping_self() < 0)
                        print("ping_self: failed\n");
                    else
                        print("ping_self: ok\n");
                }
                else if (strcmp(cmd, "ai_info") == 0)
                {
                    ai_print_info();
                }
                else if (strcmp(cmd, "ai_backend") == 0)
                {
                    if (argc < 2)
                    {
                        print("ai_backend: ");
                        print(ai_backend_name(ai_get_default_backend()));
                        print("\n");
                    }
                    else
                    {
                        ai_backend_type_t backend;
                        if (ai_parse_backend(argv[1], &backend) < 0 || ai_set_default_backend(backend) < 0)
                        {
                            print("ai_backend: expected local, cloud, or hybrid\n");
                        }
                        else
                        {
                            print("ai_backend: switched to ");
                            print(ai_backend_name(backend));
                            print("\n");
                        }
                    }
                }
                else if (strcmp(cmd, "ai_ask") == 0)
                {
                    if (argc < 2)
                    {
                        print("ai_ask: missing prompt\n");
                    }
                    else
                    {
                        char prompt_buf[AI_PROMPT_MAX];
                        int pos = 0;
                        for (int i = 1; i < argc; i++)
                        {
                            if (i > 1 && pos < (int)AI_PROMPT_MAX - 1)
                                prompt_buf[pos++] = ' ';
                            int k = 0;
                            while (argv[i][k] && pos < (int)AI_PROMPT_MAX - 1)
                                prompt_buf[pos++] = argv[i][k++];
                        }
                        prompt_buf[pos] = '\0';

                        ai_request_t request;
                        ai_response_t response;
                        memset(&request, 0, sizeof(request));
                        request.task_type = AI_TASK_CHAT;
                        request.backend_preference = ai_get_default_backend();
                        request.model = NULL;
                        request.prompt = prompt_buf;
                        request.system_prompt = "You are openos local AI assistant.";
                        request.max_tokens = AI_RESPONSE_MAX;
                        request.flags = 0;

                        if (ai_generate(&request, &response) < 0)
                        {
                            print("ai_ask: failed\n");
                        }
                        else
                        {
                            print(response.text);
                            print("\n");
                        }
                    }
                }
                else if (strcmp(cmd, "ai_models") == 0)
                {
                    ai_print_models();
                }
                else if (strcmp(cmd, "ai_model_load") == 0)
                {
                    if (argc < 2)
                    {
                        print("ai_model_load: missing model name\n");
                    }
                    else if (ai_model_load(argv[1]) < 0)
                    {
                        print("ai_model_load: failed\n");
                    }
                    else
                    {
                        print("ai_model_load: loaded ");
                        print(argv[1]);
                        print("\n");
                    }
                }
                else if (strcmp(cmd, "ai_model_unload") == 0)
                {
                    if (argc < 2)
                    {
                        print("ai_model_unload: missing model name\n");
                    }
                    else if (ai_model_unload(argv[1]) < 0)
                    {
                        print("ai_model_unload: failed\n");
                    }
                    else
                    {
                        print("ai_model_unload: unloaded ");
                        print(argv[1]);
                        print("\n");
                    }
                }
                else if (strcmp(cmd, "ai_repo") == 0)
                {
                    if (argc < 2)
                    {
                        ai_print_repo();
                    }
                    else if (ai_repo_set_path(argv[1]) < 0)
                    {
                        print("ai_repo: failed\n");
                    }
                    else
                    {
                        print("ai_repo: path set to ");
                        print(ai_repo_path());
                        print("\n");
                    }
                }
                else if (strcmp(cmd, "ai_trust") == 0)
                {
                    if (argc < 2)
                    {
                        ai_print_trust();
                    }
                    else if (strcmp(argv[1], "load") == 0)
                    {
                        if (argc < 3)
                        {
                            print("ai_trust: missing key file\n");
                        }
                        else if (ai_trust_key_load_file(argv[2]) < 0)
                        {
                            print("ai_trust: load key failed\n");
                        }
                        else
                        {
                            print("ai_trust: loaded ");
                            print(argv[2]);
                            print("\n");
                        }
                    }
                    else if (ai_trust_root_set_path(argv[1]) < 0)
                    {
                        print("ai_trust: failed\n");
                    }
                    else
                    {
                        print("ai_trust: trust root set to ");
                        print(ai_trust_root_path());
                        print("\n");
                    }
                }
                else if (strcmp(cmd, "ai_ed25519") == 0)
                {
                    if (argc < 2 || strcmp(argv[1], "selftest") == 0)
                    {
                        ai_print_ed25519_selftest();
                    }
                    else if (argc >= 5 && strcmp(argv[1], "verify_sha256") == 0)
                    {
                        int status = ai_ed25519_verify_sha256_hex(argv[2], argv[3], argv[4]);
                        if (status == AI_STATUS_OK)
                        {
                            print("ai_ed25519: signature valid\n");
                        }
                        else
                        {
                            print("ai_ed25519: signature invalid or unsupported, status=");
                            shell_print_dec(status);
                            print("\n");
                        }
                    }
                    else
                    {
                        print("usage: ai_ed25519 [selftest|verify_sha256 <public_key_hex> <sha256_hex> <signature_hex>]\n");
                    }
                }
                else if (strcmp(cmd, "ai_model_register") == 0)
                {
                    if (argc < 2)
                    {
                        print("ai_model_register: missing manifest path\n");
                    }
                    else if (ai_model_register_manifest_file(argv[1]) < 0)
                    {
                        print("ai_model_register: failed\n");
                    }
                    else
                    {
                        print("ai_model_register: registered ");
                        print(argv[1]);
                        print("\n");
                    }
                }
                else if (strcmp(cmd, "ai_model_scan") == 0)
                {
                    int n = ai_repo_scan();
                    if (n < 0)
                    {
                        print("ai_model_scan: failed\n");
                    }
                    else
                    {
                        print("ai_model_scan: registered ");
                        shell_print_dec(n);
                        print(" manifest(s)\n");
                    }
                }
                else if (strcmp(cmd, "devices") == 0)
                {
                    devmgr_print_devices();
                }
                else if (strcmp(cmd, "hotplug") == 0)
                {
                    devmgr_print_hotplug_events();
                }
                else if (strcmp(cmd, "hotplug_poll") == 0)
                {
                    hotplug_event_t event;
                    if (devmgr_poll_event(&event) < 0)
                    {
                        print("hotplug_poll: no event\n");
                    }
                    else
                    {
                        print("hotplug_poll: ");
                        print(devmgr_action_name(event.action));
                        print(" ");
                        print(event.name);
                        print("\n");
                    }
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
            cmd_cursor = 0;
            cmd_buf[0] = '\0';
            history_view = history_count;
            shell_prompt();
        }
        else if (c == 0x1B)
        {
            char c2 = shell_read_input_char(10000);
            char c3 = shell_read_input_char(10000);
            if (c2 == '[' || c2 == 'O')
            {
                if (c3 == 'A')
                    shell_history_prev();
                else if (c3 == 'B')
                    shell_history_next();
                else if (c3 == 'C')
                    shell_move_cursor_right();
                else if (c3 == 'D')
                    shell_move_cursor_left();
                else if (c3 == 'H')
                    shell_move_cursor_home();
                else if (c3 == 'F')
                    shell_move_cursor_end();
                else if (c3 == '1' || c3 == '3' || c3 == '4' || c3 == '7' || c3 == '8')
                {
                    char c4 = shell_read_input_char(10000);
                    if ((c3 == '1' || c3 == '7') && c4 == '~')
                        shell_move_cursor_home();
                    else if ((c3 == '4' || c3 == '8') && c4 == '~')
                        shell_move_cursor_end();
                    else if (c3 == '3' && c4 == '~')
                        shell_delete_char();
                }
            }
        }
        else if (c == '\t')
        {
            shell_complete();
            cmd_cursor = cmd_pos;
        }
        else if (c == 0x01)  /* Ctrl+A - 跳转到行首 */
        {
            shell_move_cursor_home();
        }
        else if (c == 0x05)  /* Ctrl+E - 跳转到行尾 */
        {
            shell_move_cursor_end();
        }
        else if (c == 0x03)  /* Ctrl+C - 取消当前行 */
        {
            shell_cancel_line();
        }
        else if (c == 0x7F || c == 0x08)
        {
            shell_backspace();
        }
        else if (c >= ' ')
        {
            shell_append_char(c);
        }
    }
}
