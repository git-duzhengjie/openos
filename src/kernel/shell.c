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
#include "framebuffer.h"
#include "gui.h"
#include "mouse.h"
#include "ext4.h"
#include "include/io.h"
extern int spawn_user_process(const char *path, char *const argv[]);
extern int spawn_user_process_env(const char *path, char *const argv[], char *const envp[]);
extern int sys_waitpid(int pid, int *status, int options);
extern void sched_yield(void);
#ifndef NULL
#define NULL ((void *)0)
#endif

static void make_path(const char *arg, char *out);
static int shell_spawn_user_program(const char *path, char *argv[], int argc);
static int shell_run_pipeline(char *cmdline, int background);
static int shell_run_external_with_redirect(char *cmd, char *argv[], int argc);
static int shell_register_background_job(int *pids, int count, const char *command);
static void shell_jobs_poll(void);
static void cmd_jobs(void);
static void cmd_fg(int argc, char *argv[]);
static void shell_complete_command(void);
static void shell_history_prev(void);
static void shell_history_next(void);
static void shell_move_cursor_left(void);
static void shell_move_cursor_right(void);
static void shell_move_cursor_home(void);
static void shell_move_cursor_end(void);
static void shell_delete_char(void);
static void shell_cancel_line(void);
static void shell_backspace(void);

#define SHELL_WAITPID_WNOHANG 1
#define SHELL_CTRL_C 0x03
#define SHELL_CTRL_D 0x04
#define SHELL_MAX_JOBS 16
#define SHELL_MAX_JOB_PIDS 8
#define SHELL_JOB_CMD_MAX 128

typedef enum shell_job_state {
    SHELL_JOB_UNUSED = 0,
    SHELL_JOB_RUNNING,
    SHELL_JOB_DONE
} shell_job_state_t;

typedef struct shell_job {
    int id;
    shell_job_state_t state;
    int pid_count;
    int pids[SHELL_MAX_JOB_PIDS];
    int completed[SHELL_MAX_JOB_PIDS];
    int statuses[SHELL_MAX_JOB_PIDS];
    char command[SHELL_JOB_CMD_MAX];
} shell_job_t;

static shell_job_t shell_jobs[SHELL_MAX_JOBS];
static int shell_next_job_id = 1;

/* 打印：默认输出到 VGA + 串口 + GUI；命令重定向时输出到 fd 1/2 */
static int shell_stdout_redirect_depth = 0;
static int shell_stderr_redirect_depth = 0;

static void shell_print_terminal(const char *s)
{
    vga_write(s);
    serial_write(s);
    if (gui_is_ready()) {
        gui_terminal_enqueue_output(s);
    }
}

static void print(const char *s)
{
    if (!s)
        return;

    if (shell_stdout_redirect_depth > 0 && vfs_get_file(1)) {
        vfs_write(1, s, strlen(s));
        return;
    }

    shell_print_terminal(s);
}

static void print_err(const char *s)
{
    if (!s)
        return;

    if (shell_stderr_redirect_depth > 0 && vfs_get_file(2)) {
        vfs_write(2, s, strlen(s));
        return;
    }

    shell_print_terminal(s);
}

static void print_err_dec(int value)
{
    char buf[12];
    int i = 0;

    if (value == 0)
    {
        print_err("0");
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
        print_err(out);
    }
}

static char shell_ascii_lower(char c)
{
    if (c >= 'A' && c <= 'Z')
        return (char)(c - 'A' + 'a');
    return c;
}

static int shell_strcasecmp(const char *a, const char *b)
{
    int i = 0;
    while (a[i] && b[i]) {
        char ca = shell_ascii_lower(a[i]);
        char cb = shell_ascii_lower(b[i]);
        if (ca != cb)
            return (int)((unsigned char)ca) - (int)((unsigned char)cb);
        i++;
    }
    return (int)((unsigned char)shell_ascii_lower(a[i])) -
           (int)((unsigned char)shell_ascii_lower(b[i]));
}

static int shell_cmd_equals(const char *cmd, const char *name)
{
    return shell_strcasecmp(cmd, name) == 0;
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
#define SHELL_ENV_MAX 32
#define SHELL_ENV_NAME_MAX 32
#define SHELL_ENV_VALUE_MAX 160
#define SHELL_ENV_STRING_MAX (SHELL_ENV_NAME_MAX + 1 + SHELL_ENV_VALUE_MAX)

static char cmd_buf[CMD_BUF_SIZE];
static int cmd_pos = 0;      /* 当前命令长度 */
static int cmd_cursor = 0;   /* 当前光标在命令行内的位置 */

#define SHELL_HISTORY_SIZE 16
static char history[SHELL_HISTORY_SIZE][CMD_BUF_SIZE];
static int history_count = 0;
static int history_view = 0;
static char history_saved_line[CMD_BUF_SIZE];
static int history_saved_valid = 0;

/* 当前工作目录 */
static char cwd[MAX_PATH] = "/";

typedef struct shell_env_entry {
    char name[SHELL_ENV_NAME_MAX];
    char value[SHELL_ENV_VALUE_MAX];
    char kv[SHELL_ENV_STRING_MAX];
    int used;
} shell_env_entry_t;

static shell_env_entry_t shell_env[SHELL_ENV_MAX];
static char *shell_envp[SHELL_ENV_MAX + 1];
static int shell_env_initialized = 0;

/* ---- 辅助函数 ---- */
static void shell_prompt(void)
{
    print("openos:");
    print(cwd);
    print("$ ");
}

static const char *builtin_commands[] = {
    "env",
    "export",
    "unset",
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
    "ai_model_scan",
    "jobs",
    "fg"
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

static int shell_env_name_valid(const char *name)
{
    if (!name || !name[0])
        return 0;

    char c0 = name[0];
    if (!((c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z') || c0 == '_'))
        return 0;

    for (int i = 1; name[i]; i++) {
        char c = name[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_'))
            return 0;
    }
    return 1;
}

static char *shell_find_char(char *s, char target)
{
    if (!s)
        return NULL;

    for (int i = 0; s[i]; i++) {
        if (s[i] == target)
            return &s[i];
    }
    return NULL;
}

static int shell_env_find(const char *name)
{
    if (!name)
        return -1;

    for (int i = 0; i < SHELL_ENV_MAX; i++) {
        if (shell_env[i].used && strcmp(shell_env[i].name, name) == 0)
            return i;
    }
    return -1;
}

static void shell_env_rebuild_entry(int idx)
{
    int pos = 0;

    if (idx < 0 || idx >= SHELL_ENV_MAX || !shell_env[idx].used)
        return;

    for (int i = 0; shell_env[idx].name[i] && pos + 1 < SHELL_ENV_STRING_MAX; i++)
        shell_env[idx].kv[pos++] = shell_env[idx].name[i];
    if (pos + 1 < SHELL_ENV_STRING_MAX)
        shell_env[idx].kv[pos++] = '=';
    for (int i = 0; shell_env[idx].value[i] && pos + 1 < SHELL_ENV_STRING_MAX; i++)
        shell_env[idx].kv[pos++] = shell_env[idx].value[i];
    shell_env[idx].kv[pos] = '\0';
}

static int shell_setenv(const char *name, const char *value)
{
    if (!shell_env_name_valid(name))
        return -1;
    if (!value)
        value = "";
    if ((int)strlen(name) >= SHELL_ENV_NAME_MAX || (int)strlen(value) >= SHELL_ENV_VALUE_MAX)
        return -2;

    int idx = shell_env_find(name);
    if (idx < 0) {
        for (int i = 0; i < SHELL_ENV_MAX; i++) {
            if (!shell_env[i].used) {
                idx = i;
                break;
            }
        }
        if (idx < 0)
            return -3;
    }

    strncpy(shell_env[idx].name, name, SHELL_ENV_NAME_MAX - 1);
    shell_env[idx].name[SHELL_ENV_NAME_MAX - 1] = '\0';
    strncpy(shell_env[idx].value, value, SHELL_ENV_VALUE_MAX - 1);
    shell_env[idx].value[SHELL_ENV_VALUE_MAX - 1] = '\0';
    shell_env[idx].used = 1;
    shell_env_rebuild_entry(idx);
    return 0;
}

static int shell_unsetenv(const char *name)
{
    if (!shell_env_name_valid(name))
        return -1;

    int idx = shell_env_find(name);
    if (idx < 0)
        return 0;

    shell_env[idx].used = 0;
    shell_env[idx].name[0] = '\0';
    shell_env[idx].value[0] = '\0';
    shell_env[idx].kv[0] = '\0';
    return 0;
}

static char *const *shell_build_envp(void)
{
    int pos = 0;

    if (!shell_env_initialized) {
        shell_setenv("PATH", "/bin");
        shell_setenv("PWD", cwd);
        shell_setenv("SHELL", "/bin/shell");
        shell_env_initialized = 1;
    }

    shell_setenv("PWD", cwd);
    for (int i = 0; i < SHELL_ENV_MAX && pos < SHELL_ENV_MAX; i++) {
        if (shell_env[i].used)
            shell_envp[pos++] = shell_env[i].kv;
    }
    shell_envp[pos] = NULL;
    return shell_envp;
}

static void cmd_env(void)
{
    shell_build_envp();
    for (int i = 0; i < SHELL_ENV_MAX; i++) {
        if (shell_env[i].used) {
            print(shell_env[i].kv);
            print("\n");
        }
    }
}

static void cmd_export(int argc, char *argv[])
{
    if (argc == 1) {
        cmd_env();
        return;
    }

    for (int i = 1; i < argc; i++) {
        char *eq = shell_find_char(argv[i], '=');
        int ret;

        if (eq) {
            *eq = '\0';
            ret = shell_setenv(argv[i], eq + 1);
            *eq = '=';
        } else {
            ret = shell_setenv(argv[i], "");
        }

        if (ret == -1)
            print_err("export: invalid name\n");
        else if (ret == -2)
            print_err("export: value too long\n");
        else if (ret == -3)
            print_err("export: environment full\n");
    }
}

static void cmd_unset(int argc, char *argv[])
{
    if (argc == 1) {
        print_err("unset: missing name\n");
        return;
    }

    for (int i = 1; i < argc; i++) {
        if (shell_unsetenv(argv[i]) < 0)
            print_err("unset: invalid name\n");
    }
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
    /* 每次最多读 4 个字符，避免串口持续输入时独占 CPU 导致 GUI 饥饿 */
    int max_read = 4;
    while ((inb(0x3FD) & 0x01) && max_read-- > 0)
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
    while (overshoot-- > 0)
        print("\b");
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
        history_saved_valid = 0;
        history_saved_line[0] = '\0';
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
    history_saved_valid = 0;
    history_saved_line[0] = '\0';
}

static void shell_history_save_file(void)
{
    /* Temporarily disabled for stability.
     * Persistent history touches VFS file operations on every Enter.
     * The current #UD eip=0x00000003 appears before command-specific code,
     * which is consistent with a corrupted/zeroed indirect file-op call.
     * Keep in-memory history only until the VFS ops corruption is fixed.
     */
    static int warned = 0;
    if (!warned) {
        serial_write("[SHELL] persistent history save disabled\n");
        warned = 1;
    }
    return;
}
static void shell_history_load_file(void)
{
    /* See shell_history_save_file(): keep history purely in-memory for now. */
    serial_write("[SHELL] persistent history load disabled\n");
    history_count = 0;
    history_view = 0;
    history_saved_valid = 0;
    history_saved_line[0] = '\0';
    return;
}
static void shell_replace_input(const char *src)
{
    int old_len = cmd_pos;

    shell_set_buffer(src);

    /*
     * Use standard carriage-return semantics for line redraw: CR returns to
     * column 0 without advancing to the next row, then the prompt and command
     * are redrawn in place. If the new command is shorter than the old one,
     * erase the stale tail and move the terminal cursor back to the logical
     * end of the command.
     */
    print("\r");
    shell_redraw_input_line();
    for (int i = cmd_pos; i < old_len; i++)
        print(" ");
    for (int i = cmd_pos; i < old_len; i++)
        print("\b");
}

static void shell_move_cursor_left(void)
{
    if (cmd_cursor <= 0)
        return;
    print("\b");
    cmd_cursor--;
}

static void shell_move_cursor_right(void)
{
    if (cmd_cursor >= cmd_pos)
        return;
    char out[2];
    out[0] = cmd_buf[cmd_cursor];
    out[1] = '\0';
    print(out);
    cmd_cursor++;
}

/* move cursor to Home */
static void shell_move_cursor_home(void)
{
    while (cmd_cursor > 0)
        shell_move_cursor_left();
}

/* move cursor to End */
static void shell_move_cursor_end(void)
{
    while (cmd_cursor < cmd_pos)
        shell_move_cursor_right();
}

/* delete char at cursor */
static void shell_delete_char(void)
{
    if (cmd_cursor >= cmd_pos)
        return;

    for (int i = cmd_cursor; i < cmd_pos - 1; i++)
        cmd_buf[i] = cmd_buf[i + 1];
    cmd_pos--;
    cmd_buf[cmd_pos] = '\0';

    print(&cmd_buf[cmd_cursor]);
    print(" ");
    int rewind = cmd_pos - cmd_cursor + 1;
    while (rewind-- > 0)
        print("\b");
}

/* cancel current line */
static void shell_cancel_line(void)
{
    shell_move_cursor_end();
    print("^C\n");
    cmd_pos = 0;
    cmd_cursor = 0;
    cmd_buf[0] = '\0';
    history_view = history_count;
    history_saved_valid = 0;
    history_saved_line[0] = '\0';
    shell_prompt();
}

static void shell_history_prev(void)
{
    if (history_count <= 0 || history_view <= 0)
        return;

    if (history_view == history_count && !history_saved_valid)
    {
        shell_copy_str(history_saved_line, cmd_buf);
        history_saved_valid = 1;
    }

    history_view--;
    shell_replace_input(history[history_view]);
}

static void shell_history_next(void)
{
    if (history_count <= 0 || history_view >= history_count)
        return;

    history_view++;
    if (history_view == history_count)
    {
        shell_replace_input(history_saved_valid ? history_saved_line : "");
        history_saved_valid = 0;
        history_saved_line[0] = '\0';
    }
    else
    {
        shell_replace_input(history[history_view]);
    }
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

    /* Redraw from the new cursor position, erase the old tail,
     * then move the terminal cursor back to the logical cursor.
     * This uses terminal output instead of VGA-only cursor writes,
     * so both text VGA and GUI Terminal stay in sync.
     */
    print(&cmd_buf[cmd_cursor]);
    print(" ");
    int rewind = cmd_pos - cmd_cursor + 1;
    while (rewind-- > 0)
        print("\b");
}

static void shell_paste_text(const char *text)
{
    int i = 0;
    if (!text) return;
    while (text[i]) {
        char ch = text[i++];
        if (ch == '\r') continue;
        if (ch == '\n' || ch == '\t') ch = ' ';
        if ((unsigned char)ch >= 32 && (unsigned char)ch < 127)
            shell_append_char(ch);
    }
}

static int shell_add_command_match(const char *name, const char *prefix, int prefix_len,
                                   const char *matches[], int *match_count, int max_matches)
{
    if (!name || !name[0] || !shell_starts_with(name, prefix, prefix_len))
        return 0;
    for (int i = 0; i < *match_count; i++)
    {
        if (strcmp(matches[i], name) == 0)
            return 0;
    }
    if (*match_count >= max_matches)
        return 0;
    matches[*match_count] = name;
    (*match_count)++;
    return 1;
}

static void shell_collect_bin_command_matches(const char *prefix, int prefix_len,
                                              const char *matches[], int *match_count,
                                              int max_matches)
{
    dentry_t *bin = vfs_path_lookup("/bin");
    if (!bin || !bin->inode || (bin->inode->mode & 0xF000) != FS_DIR)
        return;

    dentry_t *child = bin->child;
    while (child && *match_count < max_matches)
    {
        int is_dir = child->inode && (child->inode->mode & 0xF000) == FS_DIR;
        if (!is_dir)
            shell_add_command_match(child->name, prefix, prefix_len, matches, match_count, max_matches);
        child = child->sibling;
    }
}

static void shell_complete_command(void)
{
    if (!shell_is_command_completion_context())
        return;

    cmd_buf[cmd_pos] = '\0';

#define MAX_COMMAND_MATCHES 96
    const char *matches[MAX_COMMAND_MATCHES];
    int match_count = 0;

    for (int i = 0; i < (int)BUILTIN_COMMAND_COUNT; i++)
        shell_add_command_match(builtin_commands[i], cmd_buf, cmd_pos, matches, &match_count, MAX_COMMAND_MATCHES);
    shell_collect_bin_command_matches(cmd_buf, cmd_pos, matches, &match_count, MAX_COMMAND_MATCHES);

    if (match_count == 0)
        return;

    const char *first_match = matches[0];
    int common_len = (int)strlen(first_match);
    for (int i = 1; i < match_count; i++)
    {
        int len = shell_common_prefix_len(first_match, matches[i]);
        if (len < common_len)
            common_len = len;
    }

    if (match_count == 1)
    {
        int len = (int)strlen(first_match);
        for (int i = cmd_pos; i < len; i++)
            shell_append_char(first_match[i]);
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
    for (int i = 0; i < match_count; i++)
    {
        print(matches[i]);
        print("  ");
        if ((i + 1) % 4 == 0)
            print("\n");
    }
    if (match_count % 4 != 0)
        print("\n");
    shell_redraw_input_line();
}

/* path completion for file or directory */
static void shell_complete_path(void)
{
    /* find current argument start */
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

    /* 根据命令类型过滤补全候�?*/
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

    /* 公共前缀等于已输入内容：列出所有候�?*/
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

static const char *shell_basename(const char *path)
{
    const char *base = path;

    if (!path)
        return "";
    for (int i = 0; path[i]; i++)
    {
        if (path[i] == '/')
            base = &path[i + 1];
    }
    return base;
}

static char *shell_trim(char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;

    int len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t'))
        s[--len] = '\0';

    return s;
}

static int shell_execute_user_argv(int argc, char *argv[])
{
    if (argc <= 0)
        return -1;

    char path[MAX_PATH];
    make_path(argv[0], path);
    return shell_spawn_user_program(path, argv, argc);
}

#define SHELL_FD_UNCHANGED (-2)

static void shell_restore_fd(int target_fd, int saved_fd)
{
    if (saved_fd == SHELL_FD_UNCHANGED)
        return;

    if (saved_fd >= 0) {
        vfs_dup2(saved_fd, target_fd);
        vfs_close(saved_fd);
    } else {
        vfs_close(target_fd);
    }
}

static void shell_close_child_fd(int pid, int fd)
{
    process_t *child = proc_find((uint32_t)pid);
    if (child)
        vfs_close_fd_for_process(child, fd);
}

typedef struct shell_redirect {
    char *stdin_path;
    char *stdout_path;
    char *stderr_path;
    int stdout_append;
    int stderr_append;
} shell_redirect_t;

static int shell_set_redirect_path(shell_redirect_t *redir, int target, char *path, int append)
{
    if (!path || !path[0]) {
        print_err("redirect: missing filename\n");
        return -1;
    }

    if (target == 0) {
        redir->stdin_path = path;
    } else if (target == 1) {
        redir->stdout_path = path;
        redir->stdout_append = append;
    } else {
        redir->stderr_path = path;
        redir->stderr_append = append;
    }

    return 0;
}

static int shell_parse_redirects(char *argv[], int argc, shell_redirect_t *redir)
{
    int out = 0;

    if (!redir)
        return argc;

    redir->stdin_path = NULL;
    redir->stdout_path = NULL;
    redir->stderr_path = NULL;
    redir->stdout_append = 0;
    redir->stderr_append = 0;

    for (int i = 0; i < argc; i++) {
        char *arg = argv[i];
        int target = -1;
        int append = 0;
        char *op = NULL;

        if (strcmp(arg, "<") == 0) {
            target = 0;
        } else if (strcmp(arg, ">") == 0) {
            target = 1;
        } else if (strcmp(arg, ">>") == 0) {
            target = 1;
            append = 1;
        } else if (strcmp(arg, "2>") == 0) {
            target = 2;
        } else if (strcmp(arg, "2>>") == 0) {
            target = 2;
            append = 1;
        }

        if (target >= 0) {
            if (i + 1 >= argc) {
                print_err("redirect: missing filename\n");
                return -1;
            }
            if (shell_set_redirect_path(redir, target, argv[++i], append) < 0)
                return -1;
            continue;
        }

        if (arg[0] == '2' && arg[1] == '>' && arg[2] == '>' && arg[3]) {
            if (shell_set_redirect_path(redir, 2, arg + 3, 1) < 0)
                return -1;
            continue;
        }

        if (arg[0] == '2' && arg[1] == '>' && arg[2]) {
            if (shell_set_redirect_path(redir, 2, arg + 2, 0) < 0)
                return -1;
            continue;
        }

        for (char *p = arg; *p; p++) {
            if (*p == '<' || *p == '>') {
                op = p;
                target = (*p == '<') ? 0 : 1;
                append = (*p == '>' && p[1] == '>');
                break;
            }
        }

        if (target >= 0 && op) {
            char *path = op + (append ? 2 : 1);
            char *prefix = arg;
            int has_prefix = (op != arg);

            *op = '\0';
            if (!path[0]) {
                if (i + 1 >= argc) {
                    print_err("redirect: missing filename\n");
                    return -1;
                }
                path = argv[++i];
            }

            if (has_prefix)
                argv[out++] = prefix;

            if (shell_set_redirect_path(redir, target, path, append) < 0)
                return -1;
            continue;
        }

        argv[out++] = arg;
    }

    return out;
}

static int shell_open_redirect_path(const char *path, int fd, int append)
{
    char full[MAX_PATH];
    make_path(path, full);

    if (fd == 0)
        return vfs_open(full, O_RDONLY, 0);

    int flags = O_WRONLY | O_CREAT;
    if (!append)
        flags |= O_TRUNC;

    int opened = vfs_open(full, flags, S_IRUSR | S_IWUSR);
    if (opened >= 0 && append) {
        if (vfs_seek(opened, 0, SEEK_END) < 0) {
            vfs_close(opened);
            return -1;
        }
    }
    return opened;
}

static int shell_apply_one_redirect(const char *path, int target_fd, int append, int *saved_fd)
{
    int fd = shell_open_redirect_path(path, target_fd, append);
    if (fd < 0) {
        print_err("redirect: cannot open ");
        print_err(path);
        print_err("\n");
        return -1;
    }

    *saved_fd = vfs_dup(target_fd);
    if (vfs_dup2(fd, target_fd) < 0) {
        vfs_close(fd);
        print_err("redirect: dup failed\n");
        return -1;
    }

    vfs_close(fd);
    return 0;
}

static void shell_restore_redirects(int saved_stdin, int saved_stdout, int saved_stderr)
{
    shell_restore_fd(0, saved_stdin);
    shell_restore_fd(1, saved_stdout);
    shell_restore_fd(2, saved_stderr);
}

static void shell_interrupt_pid(int pid)
{
    if (pid < 0)
        return;
    proc_terminate((uint32_t)pid, 130);
}

static int shell_wait_foreground_pids(int *pids, int count)
{
    int remaining = 0;
    int interrupted = 0;

    for (int i = 0; i < count; i++) {
        if (pids[i] >= 0)
            remaining++;
    }

    while (remaining > 0) {
        for (int i = 0; i < count; i++) {
            if (pids[i] < 0)
                continue;
            int ret = sys_waitpid(pids[i], NULL, SHELL_WAITPID_WNOHANG);
            if (ret == pids[i]) {
                pids[i] = -1;
                remaining--;
            }
        }

        if (remaining <= 0)
            break;

        while (input_has_data()) {
            char c = input_getc();
            if (c == SHELL_CTRL_C) {
                for (int i = 0; i < count; i++) {
                    if (pids[i] >= 0)
                        shell_interrupt_pid(pids[i]);
                }
                interrupted = 1;
                print("^C\n");
            } else if (c == SHELL_CTRL_D) {
                input_mark_eof();
            }
        }

        sched_yield();
    }

    return interrupted ? -1 : 0;
}

static int shell_wait_foreground_pid(int pid)
{
    int pids[1];
    pids[0] = pid;
    return shell_wait_foreground_pids(pids, 1);
}

static int shell_extract_background(char *cmdline)
{
    if (!cmdline)
        return 0;

    int len = strlen(cmdline);
    while (len > 0 && (cmdline[len - 1] == ' ' || cmdline[len - 1] == '\t'))
        len--;

    if (len <= 0 || cmdline[len - 1] != '&')
        return 0;

    cmdline[len - 1] = '\0';
    len--;
    while (len > 0 && (cmdline[len - 1] == ' ' || cmdline[len - 1] == '\t')) {
        cmdline[len - 1] = '\0';
        len--;
    }

    return 1;
}

static void shell_print_background_job(int pid)
{
    print("[bg] pid ");
    shell_print_dec(pid);
    print("\n");
}

static void shell_print_background_pipeline(int *pids, int count)
{
    print("[bg] pipeline");
    for (int i = 0; i < count; i++) {
        if (pids[i] >= 0) {
            print(" ");
            shell_print_dec(pids[i]);
        }
    }
    print("\n");
}

static void shell_copy_job_command(char *dst, const char *src)
{
    int i = 0;
    if (!dst)
        return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    while (src[i] && i + 1 < SHELL_JOB_CMD_MAX) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void shell_print_job_line(shell_job_t *job)
{
    if (!job || job->state == SHELL_JOB_UNUSED)
        return;

    print("[");
    shell_print_dec(job->id);
    print("] ");
    if (job->state == SHELL_JOB_DONE)
        print("Done    ");
    else
        print("Running ");
    if (job->command[0])
        print(job->command);
    else
        print("<background job>");
    print("\n");
}

static shell_job_t *shell_find_job_by_id(int id)
{
    for (int i = 0; i < SHELL_MAX_JOBS; i++) {
        if (shell_jobs[i].state != SHELL_JOB_UNUSED && shell_jobs[i].id == id)
            return &shell_jobs[i];
    }
    return NULL;
}

static shell_job_t *shell_find_recent_job(void)
{
    shell_job_t *best = NULL;
    for (int i = 0; i < SHELL_MAX_JOBS; i++) {
        if (shell_jobs[i].state != SHELL_JOB_UNUSED && shell_jobs[i].state != SHELL_JOB_DONE) {
            if (!best || shell_jobs[i].id > best->id)
                best = &shell_jobs[i];
        }
    }
    if (best)
        return best;

    for (int i = 0; i < SHELL_MAX_JOBS; i++) {
        if (shell_jobs[i].state != SHELL_JOB_UNUSED) {
            if (!best || shell_jobs[i].id > best->id)
                best = &shell_jobs[i];
        }
    }
    return best;
}

static int shell_job_active_pid_count(shell_job_t *job)
{
    int active = 0;
    if (!job)
        return 0;
    for (int i = 0; i < job->pid_count; i++) {
        if (job->pids[i] >= 0 && !job->completed[i])
            active++;
    }
    return active;
}

static void shell_job_poll_one(shell_job_t *job)
{
    if (!job || job->state == SHELL_JOB_UNUSED || job->state == SHELL_JOB_DONE)
        return;

    int all_done = 1;
    for (int i = 0; i < job->pid_count; i++) {
        if (job->pids[i] < 0)
            continue;
        if (job->completed[i])
            continue;

        int status = 0;
        int ret = sys_waitpid(job->pids[i], &status, SHELL_WAITPID_WNOHANG);
        if (ret == job->pids[i]) {
            job->completed[i] = 1;
            job->statuses[i] = status;
        } else if (ret == 0) {
            all_done = 0;
        } else {
            job->completed[i] = 1;
        }
    }

    for (int i = 0; i < job->pid_count; i++) {
        if (job->pids[i] >= 0 && !job->completed[i]) {
            all_done = 0;
            break;
        }
    }

    if (all_done)
        job->state = SHELL_JOB_DONE;
}

static void shell_jobs_poll(void)
{
    for (int i = 0; i < SHELL_MAX_JOBS; i++)
        shell_job_poll_one(&shell_jobs[i]);
}

static int shell_register_background_job(int *pids, int count, const char *command)
{
    shell_job_t *slot = NULL;
    shell_jobs_poll();

    for (int i = 0; i < SHELL_MAX_JOBS; i++) {
        if (shell_jobs[i].state == SHELL_JOB_UNUSED) {
            slot = &shell_jobs[i];
            break;
        }
    }
    if (!slot) {
        for (int i = 0; i < SHELL_MAX_JOBS; i++) {
            if (shell_jobs[i].state == SHELL_JOB_DONE) {
                slot = &shell_jobs[i];
                break;
            }
        }
    }
    if (!slot) {
        print_err("jobs: table full\n");
        return -1;
    }

    slot->id = shell_next_job_id++;
    if (shell_next_job_id <= 0)
        shell_next_job_id = 1;
    slot->state = SHELL_JOB_RUNNING;
    slot->pid_count = 0;
    shell_copy_job_command(slot->command, command);

    for (int i = 0; i < SHELL_MAX_JOB_PIDS; i++) {
        slot->pids[i] = -1;
        slot->completed[i] = 1;
        slot->statuses[i] = 0;
    }

    for (int i = 0; i < count && slot->pid_count < SHELL_MAX_JOB_PIDS; i++) {
        if (pids[i] < 0)
            continue;
        int out = slot->pid_count++;
        slot->pids[out] = pids[i];
        slot->completed[out] = 0;
        slot->statuses[out] = 0;
    }

    if (slot->pid_count <= 0) {
        slot->state = SHELL_JOB_UNUSED;
        return -1;
    }

    print("[");
    shell_print_dec(slot->id);
    print("] ");
    for (int i = 0; i < slot->pid_count; i++) {
        if (i > 0)
            print(" ");
        shell_print_dec(slot->pids[i]);
    }
    print("\n");
    return slot->id;
}

static void cmd_jobs(void)
{
    int shown = 0;
    shell_jobs_poll();
    for (int i = 0; i < SHELL_MAX_JOBS; i++) {
        if (shell_jobs[i].state != SHELL_JOB_UNUSED) {
            shell_print_job_line(&shell_jobs[i]);
            shown = 1;
        }
    }
    if (!shown)
        print("jobs: no background jobs\n");
}

static int shell_parse_job_id(const char *s, int *out)
{
    int i = 0;
    int value = 0;
    if (!s || !out)
        return -1;
    if (s[0] == '%')
        i = 1;
    if (!s[i])
        return -1;
    for (; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9')
            return -1;
        value = value * 10 + (s[i] - '0');
    }
    *out = value;
    return 0;
}

static void cmd_fg(int argc, char *argv[])
{
    shell_jobs_poll();

    shell_job_t *job = NULL;
    if (argc > 1) {
        int id = 0;
        if (shell_parse_job_id(argv[1], &id) < 0) {
            print_err("fg: invalid job id\n");
            return;
        }
        job = shell_find_job_by_id(id);
    } else {
        job = shell_find_recent_job();
    }

    if (!job) {
        print_err("fg: no current job\n");
        return;
    }
    if (job->state == SHELL_JOB_DONE || shell_job_active_pid_count(job) <= 0) {
        print_err("fg: job already done\n");
        shell_print_job_line(job);
        return;
    }

    print(job->command[0] ? job->command : "<background job>");
    print("\n");

    int pids[SHELL_MAX_JOB_PIDS];
    int count = 0;
    for (int i = 0; i < job->pid_count; i++) {
        if (job->pids[i] >= 0 && !job->completed[i])
            pids[count++] = job->pids[i];
    }

    shell_wait_foreground_pids(pids, count);
    job->state = SHELL_JOB_DONE;
    for (int i = 0; i < job->pid_count; i++)
        job->completed[i] = 1;
}

static int shell_run_external_with_redirect(char *cmd, char *argv[], int argc)
{
    shell_redirect_t redir;
    int saved_stdin = SHELL_FD_UNCHANGED;
    int saved_stdout = SHELL_FD_UNCHANGED;
    int saved_stderr = SHELL_FD_UNCHANGED;

    argc = shell_parse_redirects(argv, argc, &redir);
    if (argc < 0)
        return -1;
    if (argc <= 0)
        return 0;
    argv[argc] = NULL;

    if (redir.stdin_path && shell_apply_one_redirect(redir.stdin_path, 0, 0, &saved_stdin) < 0)
        goto fail;
    if (redir.stdout_path && shell_apply_one_redirect(redir.stdout_path, 1, redir.stdout_append, &saved_stdout) < 0)
        goto fail;
    if (redir.stderr_path && shell_apply_one_redirect(redir.stderr_path, 2, redir.stderr_append, &saved_stderr) < 0)
        goto fail;

    int ret = shell_spawn_user_program(cmd, argv, argc);
    if (ret < 0) {
        shell_restore_redirects(saved_stdin, saved_stdout, saved_stderr);
        print_err(cmd);
        print_err(": command not found\n");
        return -1;
    }

    if (redir.stdin_path || redir.stdout_path || redir.stderr_path)
        shell_wait_foreground_pid(ret);
    else
        print("exec: spawned\n");

    shell_restore_redirects(saved_stdin, saved_stdout, saved_stderr);
    return ret;

fail:
    shell_restore_redirects(saved_stdin, saved_stdout, saved_stderr);
    return -1;
}

static int shell_run_pipeline(char *cmdline, int background)
{
    enum { SHELL_MAX_PIPE_CMDS = 8 };
    char job_command[SHELL_JOB_CMD_MAX];
    shell_copy_job_command(job_command, cmdline);

    char *segments[SHELL_MAX_PIPE_CMDS];
    char *stage_argv[SHELL_MAX_PIPE_CMDS][MAX_ARGS];
    int stage_argc[SHELL_MAX_PIPE_CMDS];
    shell_redirect_t stage_redir[SHELL_MAX_PIPE_CMDS];
    int pids[SHELL_MAX_PIPE_CMDS];
    int pipes[SHELL_MAX_PIPE_CMDS - 1][2];
    int segment_count = 0;
    int saw_pipe = 0;
    char *start = cmdline;

    for (int i = 0; i < SHELL_MAX_PIPE_CMDS; i++) {
        pids[i] = -1;
        stage_argc[i] = 0;
        stage_redir[i].stdin_path = NULL;
        stage_redir[i].stdout_path = NULL;
        stage_redir[i].stderr_path = NULL;
        stage_redir[i].stdout_append = 0;
        stage_redir[i].stderr_append = 0;
    }
    for (int i = 0; i < SHELL_MAX_PIPE_CMDS - 1; i++) {
        pipes[i][0] = -1;
        pipes[i][1] = -1;
    }

    for (int i = 0;; i++) {
        if (cmdline[i] != '|' && cmdline[i] != '\0')
            continue;

        int is_end = (cmdline[i] == '\0');
        if (!is_end) {
            saw_pipe = 1;
            cmdline[i] = '\0';
        }

        if (segment_count >= SHELL_MAX_PIPE_CMDS) {
            print_err("pipe: too many commands\n");
            return 1;
        }

        segments[segment_count] = shell_trim(start);
        if (!segments[segment_count][0]) {
            print_err("pipe: missing command\n");
            return 1;
        }
        segment_count++;

        if (is_end)
            break;
        start = &cmdline[i + 1];
    }

    if (!saw_pipe)
        return 0;

    for (int i = 0; i < segment_count; i++) {
        stage_argc[i] = split_args(segments[i], stage_argv[i], MAX_ARGS);
        if (stage_argc[i] <= 0) {
            print_err("pipe: missing command\n");
            return 1;
        }

        stage_argc[i] = shell_parse_redirects(stage_argv[i], stage_argc[i], &stage_redir[i]);
        if (stage_argc[i] <= 0) {
            print_err("pipe: missing command\n");
            return 1;
        }
        stage_argv[i][stage_argc[i]] = NULL;
    }

    int pipe_count = segment_count - 1;
    for (int i = 0; i < pipe_count; i++) {
        if (vfs_pipe(pipes[i]) < 0) {
            print_err("pipe: create failed\n");
            for (int j = 0; j < i; j++) {
                vfs_close(pipes[j][0]);
                vfs_close(pipes[j][1]);
            }
            return 1;
        }
        vfs_mark_fd_cloexec(pipes[i][0]);
        vfs_mark_fd_cloexec(pipes[i][1]);
    }

    int saved_stdin = SHELL_FD_UNCHANGED;
    int saved_stdout = SHELL_FD_UNCHANGED;
    int saved_stderr = SHELL_FD_UNCHANGED;

    for (int i = 0; i < segment_count; i++) {
        saved_stdin = SHELL_FD_UNCHANGED;
        saved_stdout = SHELL_FD_UNCHANGED;
        saved_stderr = SHELL_FD_UNCHANGED;

        if (stage_redir[i].stdin_path) {
            if (shell_apply_one_redirect(stage_redir[i].stdin_path, 0, 0, &saved_stdin) < 0)
                goto done;
        } else if (i > 0) {
            saved_stdin = vfs_dup(0);
            if (vfs_dup2(pipes[i - 1][0], 0) < 0) {
                print_err("pipe: dup stdin failed\n");
                goto done;
            }
        }

        if (stage_redir[i].stdout_path) {
            if (shell_apply_one_redirect(stage_redir[i].stdout_path, 1, stage_redir[i].stdout_append, &saved_stdout) < 0)
                goto done;
        } else if (i + 1 < segment_count) {
            saved_stdout = vfs_dup(1);
            if (vfs_dup2(pipes[i][1], 1) < 0) {
                print_err("pipe: dup stdout failed\n");
                goto done;
            }
        }

        if (stage_redir[i].stderr_path && shell_apply_one_redirect(stage_redir[i].stderr_path, 2, stage_redir[i].stderr_append, &saved_stderr) < 0)
            goto done;

        pids[i] = shell_execute_user_argv(stage_argc[i], stage_argv[i]);
        if (pids[i] < 0) {
            print_err(stage_argv[i][0]);
            print_err(": command not found\n");
        } else {
            for (int j = 0; j < pipe_count; j++) {
                shell_close_child_fd(pids[i], pipes[j][0]);
                shell_close_child_fd(pids[i], pipes[j][1]);
            }
        }

        shell_restore_redirects(saved_stdin, saved_stdout, saved_stderr);
        saved_stdin = SHELL_FD_UNCHANGED;
        saved_stdout = SHELL_FD_UNCHANGED;
        saved_stderr = SHELL_FD_UNCHANGED;
    }

done:
    shell_restore_redirects(saved_stdin, saved_stdout, saved_stderr);

    for (int i = 0; i < pipe_count; i++) {
        if (pipes[i][0] >= 0)
            vfs_close(pipes[i][0]);
        if (pipes[i][1] >= 0)
            vfs_close(pipes[i][1]);
    }

    if (background) {
        shell_register_background_job(pids, segment_count, job_command);
    } else {
        shell_wait_foreground_pids(pids, segment_count);
    }

    return 1;
}

static int shell_path_has_slash(const char *path)
{
    if (!path)
        return 0;

    for (int i = 0; path[i]; i++) {
        if (path[i] == '/')
            return 1;
    }
    return 0;
}

static int shell_make_bin_path(const char *cmd, char *out, int out_size)
{
    const char *prefix = "/bin/";
    int pos = 0;

    if (!cmd || !cmd[0] || !out || out_size <= 0)
        return -1;

    for (int i = 0; prefix[i]; i++) {
        if (pos + 1 >= out_size)
            return -1;
        out[pos++] = prefix[i];
    }
    for (int i = 0; cmd[i]; i++) {
        if (pos + 1 >= out_size)
            return -1;
        out[pos++] = cmd[i];
    }
    out[pos] = 0;
    return 0;
}

static int shell_spawn_user_program_at(const char *full, char *argv[], int argc, const char *display_name)
{
    char *child_argv[MAX_ARGS + 1];
    const char *arg0;

    if (!full || !full[0])
        return -1;

    arg0 = shell_basename(full);
    child_argv[0] = (char *)(arg0 && arg0[0] ? arg0 : display_name);

    int child_argc = 1;
    for (int i = 1; i < argc && child_argc < MAX_ARGS; i++)
        child_argv[child_argc++] = argv[i];
    child_argv[child_argc] = NULL;

    serial_write("[EXEC] spawning ");
    serial_write(full);
    serial_write("\n");

    return spawn_user_process_env(full, child_argv, (char *const *)shell_build_envp());
}

static int shell_spawn_user_program(const char *path, char *argv[], int argc)
{
    char full[MAX_PATH];
    char bin_path[MAX_PATH];
    int ret;

    if (!path || !path[0])
        return -1;

    make_path(path, full);
    ret = shell_spawn_user_program_at(full, argv, argc, path);
    if (ret >= 0 || shell_path_has_slash(path))
        return ret;

    if (shell_make_bin_path(path, bin_path, MAX_PATH) < 0)
        return ret;

    return shell_spawn_user_program_at(bin_path, argv, argc, path);
}

/* ---- 内置命令 ---- */

static void cmd_ls(const char *path)
{
    char full[MAX_PATH];
    make_path(path, full);

    dentry_t *d = vfs_path_lookup(full);

    if (!d || !d->inode)
    {
        print_err("ls: not found\n");
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
        print_err("cat: cannot open\n");
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
        print_err("cat: usage: cat >> <file> <text>\n");
        return;
    }

    char full[MAX_PATH];
    make_path(path, full);

    int fd = vfs_open(full, O_CREAT | O_RDWR, 0644);
    if (fd < 0)
    {
        print_err("cat: cannot open\n");
        return;
    }

    if (vfs_seek(fd, 0, SEEK_END) < 0)
    {
        print_err("cat: seek failed\n");
        vfs_close(fd);
        return;
    }

    int len = strlen(text);
    if (len > 0 && vfs_write(fd, text, len) < 0)
    {
        print_err("cat: write failed\n");
        vfs_close(fd);
        return;
    }

    if (vfs_write(fd, "\n", 1) < 0)
        print_err("cat: write failed\n");

    vfs_close(fd);
}

static void cmd_mkdir(const char *path)
{
    if (!path || path[0] == '\0')
    {
        print_err("mkdir: usage: mkdir <path>\n");
        return;
    }

    char full[MAX_PATH];
    make_path(path, full);
    if (vfs_path_lookup(full))
    {
        print_err("mkdir: already exists\n");
        return;
    }
    if (vfs_mkdir(full, 0755) < 0)
        print_err("mkdir: failed\n");
}

static void cmd_touch(const char *path)
{
    char full[MAX_PATH];
    make_path(path, full);
    int fd = vfs_open(full, O_CREAT | O_RDWR, 0644);
    if (fd < 0)
    {
        print_err("touch: failed\n");
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
        print_err("echo: usage: echo <text> >> <file>\n");
        return;
    }

    char full[MAX_PATH];
    make_path(path, full);

    int fd = vfs_open(full, O_CREAT | O_RDWR, 0644);
    if (fd < 0)
    {
        print_err("echo: cannot open file\n");
        return;
    }

    if (vfs_seek(fd, 0, SEEK_END) < 0)
    {
        print_err("echo: seek failed\n");
        vfs_close(fd);
        return;
    }

    int len = strlen(text);
    if (len > 0 && vfs_write(fd, text, len) < 0)
    {
        print_err("echo: write failed\n");
        vfs_close(fd);
        return;
    }

    if (vfs_write(fd, "\n", 1) < 0)
        print_err("echo: write failed\n");

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
        print_err("cd: failed\n");
        return;
    }
    vfs_getcwd(cwd, sizeof(cwd));
}

static void cmd_pwd(void)
{
    char buf[MAX_PATH];
    if (vfs_getcwd(buf, sizeof(buf)) < 0)
    {
        print_err("pwd: failed\n");
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
        print_err("rm: cannot remove directory, use rmdir\n");
        return;
    }
    if (vfs_unlink(full) < 0)
        print_err("rm: failed\n");
}

static void cmd_write(const char *path, const char *data)
{
    char full[MAX_PATH];
    make_path(path, full);

    /* Use recreate semantics for now.  This avoids reusing old inodes that may
     * have been created before fs_type/file ops initialization was fixed, and
     * also emulates O_TRUNC for filesystems that do not support it yet. */
    vfs_unlink(full);
    int fd = vfs_open(full, O_CREAT | O_RDWR, 0644);
    if (fd < 0)
    {
        print_err("write: cannot open\n");
        return;
    }
    int len = 0;
    while (data[len])
        len++;
    if (len > 0 && vfs_write(fd, data, len) < 0)
        print_err("write: failed\n");
    vfs_close(fd);
}

static void cmd_history(void)
{
    if (history_count <= 0)
    {
        print_err("history: no commands\n");
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
    print("  jobs            - Show background jobs\n");
    print("  fg [%job]       - Bring background job to foreground\n");
    print("  env             - Show shell environment\n");
    print("  export NAME=VALUE - Set shell environment variable\n");
    print("  unset NAME      - Remove shell environment variable\n");
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
    print("  Ctrl+C          - Interrupt foreground command or cancel line\n");
    print("  Ctrl+D          - Send EOF to stdin or logout on empty line\n");
    print("  fbinfo          - Show framebuffer information\n");
    print("  fbtest          - Draw framebuffer test pattern\n");
    print("  gui             - Show GUI/window system information\n");
    print("  guitest         - Start GUI demo desktop\n");
    print("  cursor [on|off] - Show/hide OpenOS software cursor\n");
    print("  mouse           - Show PS/2 mouse driver status\n");
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

    char last_enter_char = 0;

    /* 从串口读取输入（轮询方式�?*/
    while (1)
    {
        /* 检查键盘输�?�?通过串口端口 0x3F8 读取 */
        /* 简化：使用 serial_read 如果可用，否则用键盘缓冲�?*/
        /* Phase 3 先用串口回显模式 */

        /* 先把串口数据灌入输入缓冲�?*/
        /* 从统一输入缓冲区读取；同时把串口数据灌入输入缓冲区 */
        char c = shell_read_input_char(0);

        if (!c) {
            static unsigned idle_spin = 0;
            __asm__ volatile("pause");
            idle_spin++;
            if ((idle_spin & 0x3fu) == 0) {
                sched_yield();
            }
            continue;
        }

        if (c == 0x1B)
        {
            char c2 = shell_read_input_char(10000);
            char c3 = shell_read_input_char(10000);
            int gui_key = 0;

            if (c2 == '[' || c2 == 'O')
            {
                if (c3 == 'A')
                    gui_key = GUI_KEY_UP;
                else if (c3 == 'B')
                    gui_key = GUI_KEY_DOWN;
                else if (c3 == 'C')
                    gui_key = GUI_KEY_RIGHT;
                else if (c3 == 'D')
                    gui_key = GUI_KEY_LEFT;
                else if (c3 == 'H')
                    gui_key = GUI_KEY_HOME;
                else if (c3 == 'F')
                    gui_key = GUI_KEY_END;
                else if (c3 == '1' || c3 == '3' || c3 == '4' || c3 == '7' || c3 == '8')
                {
                    char c4 = shell_read_input_char(10000);
                    if ((c3 == '1' || c3 == '7') && c4 == '~')
                        gui_key = GUI_KEY_HOME;
                    else if ((c3 == '4' || c3 == '8') && c4 == '~')
                        gui_key = GUI_KEY_END;
                    else if (c3 == '3' && c4 == '~')
                        gui_key = GUI_KEY_DELETE;
                }
            }

            if (gui_key && gui_should_capture_key_code(gui_key))
            {
                gui_post_key_code(gui_key);
                continue;
            }

            if (gui_key == GUI_KEY_UP)
                shell_history_prev();
            else if (gui_key == GUI_KEY_DOWN)
                shell_history_next();
            else if (gui_key == GUI_KEY_RIGHT)
                shell_move_cursor_right();
            else if (gui_key == GUI_KEY_LEFT)
                shell_move_cursor_left();
            else if (gui_key == GUI_KEY_HOME)
                shell_move_cursor_home();
            else if (gui_key == GUI_KEY_END)
                shell_move_cursor_end();
            else if (gui_key == GUI_KEY_DELETE)
                shell_delete_char();

            continue;
        }

        if ((c == '\r' && last_enter_char == '\n') ||
            (c == '\n' && last_enter_char == '\r'))
        {
            last_enter_char = 0;
            continue;
        }

        if (c == '\r' || c == '\n')
            last_enter_char = c;
        else
            last_enter_char = 0;

        {
            int gui_key = 0;
            int gui_text_key = 0;

            if (c == '\r' || c == '\n')
                gui_key = GUI_KEY_ENTER;
            else if (c == '\t')
                gui_key = GUI_KEY_TAB;
            else if (c == 0x01)
                gui_key = GUI_KEY_HOME;
            else if (c == 0x05)
                gui_key = GUI_KEY_END;
            else if (c == 0x7F || c == 0x08)
                gui_key = GUI_KEY_BACKSPACE;
            else if (c >= ' ')
            {
                gui_key = (unsigned char)c;
                gui_text_key = 1;
            }

            if (gui_key && gui_should_capture_key_code(gui_key))
            {
                if (gui_text_key)
                    gui_post_key(c);
                else
                    gui_post_key_code(gui_key);
                continue;
            }
        }

        if (c == '\r' || c == '\n')
        {
            cmd_buf[cmd_pos] = '\0';
            print("\n");
            shell_save_history();
            shell_history_save_file();

            int background = shell_extract_background(cmd_buf);
            if (background && !shell_trim(cmd_buf)[0]) {
                print_err("background: missing command\n");
                cmd_pos = 0;
                cmd_cursor = 0;
                cmd_buf[0] = '\0';
                shell_prompt();
                continue;
            }

            if (shell_run_pipeline(cmd_buf, background))
            {
                cmd_pos = 0;
                cmd_cursor = 0;
                cmd_buf[0] = '\0';
                shell_prompt();
                continue;
            }

            /* 解析命令 */
            char *argv[MAX_ARGS];
            int argc = split_args(cmd_buf, argv, MAX_ARGS);
            shell_redirect_t redir;
            int saved_stdin = SHELL_FD_UNCHANGED;
            int saved_stdout = SHELL_FD_UNCHANGED;
            int saved_stderr = SHELL_FD_UNCHANGED;
            int stdout_redirected = 0;
            int stderr_redirected = 0;
            int redirect_failed = 0;

            if (argc > 0) {
                argc = shell_parse_redirects(argv, argc, &redir);
                if (argc < 0 || argc <= 0) {
                    redirect_failed = 1;
                    argc = 0;
                } else {
                    argv[argc] = NULL;
                    if (redir.stdin_path && shell_apply_one_redirect(redir.stdin_path, 0, 0, &saved_stdin) < 0)
                        redirect_failed = 1;
                    if (!redirect_failed && redir.stdout_path && shell_apply_one_redirect(redir.stdout_path, 1, redir.stdout_append, &saved_stdout) < 0)
                        redirect_failed = 1;
                    if (!redirect_failed && redir.stderr_path && shell_apply_one_redirect(redir.stderr_path, 2, redir.stderr_append, &saved_stderr) < 0)
                        redirect_failed = 1;
                    if (!redirect_failed && redir.stdout_path) {
                        stdout_redirected = 1;
                        shell_stdout_redirect_depth++;
                    }
                    if (!redirect_failed && redir.stderr_path) {
                        stderr_redirected = 1;
                        shell_stderr_redirect_depth++;
                    }
                }
            }

            if (argc > 0 && !redirect_failed)
            {
                char *cmd = argv[0];
                if (shell_cmd_equals(cmd, "ls"))
                {
                    cmd_ls(argc > 1 ? argv[1] : ".");
                }
                else if (shell_cmd_equals(cmd, "cat"))
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
                            print_err("cat: missing file or text\n");
                    }
                    else if (argc > 1)
                        cmd_cat(argv[1]);
                    else
                        print_err("cat: missing argument\n");
                }
                else if (shell_cmd_equals(cmd, "mkdir"))
                {
                    if (argc > 1)
                        cmd_mkdir(argv[1]);
                    else
                        print_err("mkdir: missing argument\n");
                }
                else if (shell_cmd_equals(cmd, "touch"))
                {
                    if (argc > 1)
                        cmd_touch(argv[1]);
                    else
                        print_err("touch: missing argument\n");
                }
                else if (shell_cmd_equals(cmd, "rm"))
                {
                    if (argc > 1)
                        cmd_rm(argv[1]);
                    else
                        print_err("rm: missing argument\n");
                }
                else if (shell_cmd_equals(cmd, "echo"))
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
                        /* 拼接 >> 前面的所有参数作为文�?*/
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
                            print_err("echo: missing filename\n");
                    }
                    else
                        cmd_echo(argc, argv);
                }
                else if (shell_cmd_equals(cmd, "cd"))
                {
                    cmd_cd(argc > 1 ? argv[1] : NULL);
                }
                else if (shell_cmd_equals(cmd, "pwd"))
                {
                    cmd_pwd();
                }
                else if (shell_cmd_equals(cmd, "history"))
                {
                    cmd_history();
                }
                else if (shell_cmd_equals(cmd, "env"))
                {
                    cmd_env();
                }
                else if (shell_cmd_equals(cmd, "export"))
                {
                    cmd_export(argc, argv);
                }
                else if (shell_cmd_equals(cmd, "unset"))
                {
                    cmd_unset(argc, argv);
                }
                else if (shell_cmd_equals(cmd, "mkext4"))
                {
                    const char *dev = argc > 1 ? argv[1] : "ram0";
                    if (ext4_format_test_volume(dev) < 0)
                        print_err("mkext4: format failed\n");
                    else
                        print("mkext4: ok\n");
                }
                else if (shell_cmd_equals(cmd, "mount_ext4"))
                {
                    const char *dev = argc > 1 ? argv[1] : "ram0";
                    const char *path = argc > 2 ? argv[2] : "/mnt";
                    if (ext4_mount(dev, path) < 0)
                        print_err("mount_ext4: failed\n");
                    else
                        print("mount_ext4: ok\n");
                }
                else if (shell_cmd_equals(cmd, "mount_tmpfs"))
                {
                    const char *path = argc > 1 ? argv[1] : "/tmp";
                    if (tmpfs_mount(path) < 0)
                        print_err("mount_tmpfs: failed\n");
                    else
                        print("mount_tmpfs: ok\n");
                }
                else if (shell_cmd_equals(cmd, "write"))
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
                        print_err("write: need file and text\n");
                }
                else if (shell_cmd_equals(cmd, "fbinfo"))
                {
                    framebuffer_print_info();
                    const framebuffer_info_t *info = framebuffer_get_info();
                    print("framebuffer: ");
                    print(info->available ? "available" : "not available");
                    print(info->mode_set ? ", mode set\n" : ", text mode\n");
                }
                else if (shell_cmd_equals(cmd, "fbtest"))
                {
                    if (!framebuffer_is_available())
                    {
                        print_err("fbtest: framebuffer not available\n");
                    }
                    else
                    {
                        print("fbtest: switching to graphics mode...\n");
                        framebuffer_test_pattern();
                    }
                }
                else if (shell_cmd_equals(cmd, "netinfo"))
                {
                    net_print_info();
                }
                else if (shell_cmd_equals(cmd, "discovery"))
                {
                    if (argc < 2)
                    {
                        discovery_print_info();
                    }
                    else if (strcmp(argv[1], "scan") == 0 || strcmp(argv[1], "query") == 0)
                    {
                        if (discovery_query() < 0)
                            print_err("discovery: scan failed\n");
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
                            print_err("discovery: invalid auth secret\n");
                        else
                            print("discovery: auth secret configured\n");
                    }
                    else if (strcmp(argv[1], "auth_peer") == 0)
                    {
                        if (argc < 3 || discovery_auth_peer(argv[2]) < 0)
                            print_err("discovery: auth peer failed\n");
                        else
                            print("discovery: auth challenge sent\n");
                    }
                    else if (strcmp(argv[1], "announce") == 0)
                    {
                        if (discovery_announce() < 0)
                            print_err("discovery: announce failed\n");
                        else
                            print("discovery: hello broadcast sent\n");
                    }
                    else if (strcmp(argv[1], "bye") == 0)
                    {
                        if (discovery_goodbye() < 0)
                            print_err("discovery: bye failed\n");
                        else
                            print("discovery: bye broadcast sent\n");
                    }
                    else if (strcmp(argv[1], "name") == 0)
                    {
                        if (argc < 3 || discovery_set_local_name(argv[2]) < 0)
                            print_err("discovery: invalid name\n");
                        else
                            print("discovery: name updated\n");
                    }
                    else if (strcmp(argv[1], "caps") == 0)
                    {
                        if (argc < 3 || discovery_set_local_capabilities(argv[2]) < 0)
                            print_err("discovery: invalid capabilities\n");
                        else
                            print("discovery: capabilities updated\n");
                    }
                    else
                    {
                        print_err("usage: discovery [scan|peers|announce|bye|name <n>|caps <c>|auth|auth_secret <s>|auth_peer <id>]\n");
                    }
                }
                else if (shell_cmd_equals(cmd, "sync"))
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
                            print_err("sync: put failed\n");
                        else
                            print("sync: item updated and broadcast\n");
                    }
                    else if (strcmp(argv[1], "del") == 0)
                    {
                        if (argc < 3 || sync_delete(argv[2]) < 0)
                            print_err("sync: delete failed\n");
                        else
                            print("sync: delete broadcast\n");
                    }
                    else if (strcmp(argv[1], "push") == 0)
                    {
                        if (argc < 3 || sync_broadcast_key(argv[2]) < 0)
                            print_err("sync: push failed\n");
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
                            print_err("sync: offer failed\n");
                        else
                            print("sync: task offered\n");
                    }
                    else if (strcmp(argv[1], "accept") == 0)
                    {
                        if (argc < 3 || sync_task_accept(argv[2]) < 0)
                            print_err("sync: accept failed\n");
                        else
                            print("sync: task accepted\n");
                    }
                    else if (strcmp(argv[1], "done") == 0)
                    {
                        if (argc < 4 || sync_task_done(argv[2], argv[3]) < 0)
                            print_err("sync: done failed\n");
                        else
                            print("sync: task done\n");
                    }
                    else
                    {
                        print_err("usage: sync [info|items|tasks|reliable|put <k> <v>|del <k>|push <k>|push_all|offer <id> <title> <payload> [target]|accept <id>|done <id> <result>]\n");
                    }
                }
                else if (shell_cmd_equals(cmd, "bus"))
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
                            print_err("bus: publish failed\n");
                        else
                            print("bus: published local+remote\n");
                    }
                    else if (strcmp(argv[1], "pub_local") == 0)
                    {
                        if (argc < 4 || bus_publish(argv[2], argv[3], BUS_PUBLISH_LOCAL) < 0)
                            print_err("bus: local publish failed\n");
                        else
                            print("bus: published local\n");
                    }
                    else if (strcmp(argv[1], "sub") == 0)
                    {
                        if (argc < 3 || bus_shell_subscribe(argv[2]) < 0)
                            print_err("bus: subscribe failed\n");
                        else
                            print("bus: shell subscriber updated\n");
                    }
                    else
                    {
                        print_err("usage: bus [info|stats|subs|pub <topic> <payload>|pub_local <topic> <payload>|sub <topic>]\n");
                    }
                }
                else if (shell_cmd_equals(cmd, "ping_self"))
                {
                    if (net_ping_self() < 0)
                        print_err("ping_self: failed\n");
                    else
                        print("ping_self: ok\n");
                }
                else if (shell_cmd_equals(cmd, "ai_info"))
                {
                    ai_print_info();
                }
                else if (shell_cmd_equals(cmd, "ai_backend"))
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
                            print_err("ai_backend: expected local, cloud, or hybrid\n");
                        }
                        else
                        {
                            print("ai_backend: switched to ");
                            print(ai_backend_name(backend));
                            print("\n");
                        }
                    }
                }
                else if (shell_cmd_equals(cmd, "ai_ask"))
                {
                    if (argc < 2)
                    {
                        print_err("ai_ask: missing prompt\n");
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
                            print_err("ai_ask: failed\n");
                        }
                        else
                        {
                            print(response.text);
                            print("\n");
                        }
                    }
                }
                else if (shell_cmd_equals(cmd, "ai_models"))
                {
                    ai_print_models();
                }
                else if (shell_cmd_equals(cmd, "ai_model_load"))
                {
                    if (argc < 2)
                    {
                        print_err("ai_model_load: missing model name\n");
                    }
                    else if (ai_model_load(argv[1]) < 0)
                    {
                        print_err("ai_model_load: failed\n");
                    }
                    else
                    {
                        print("ai_model_load: loaded ");
                        print(argv[1]);
                        print("\n");
                    }
                }
                else if (shell_cmd_equals(cmd, "ai_model_unload"))
                {
                    if (argc < 2)
                    {
                        print_err("ai_model_unload: missing model name\n");
                    }
                    else if (ai_model_unload(argv[1]) < 0)
                    {
                        print_err("ai_model_unload: failed\n");
                    }
                    else
                    {
                        print("ai_model_unload: unloaded ");
                        print(argv[1]);
                        print("\n");
                    }
                }
                else if (shell_cmd_equals(cmd, "ai_repo"))
                {
                    if (argc < 2)
                    {
                        ai_print_repo();
                    }
                    else if (ai_repo_set_path(argv[1]) < 0)
                    {
                        print_err("ai_repo: failed\n");
                    }
                    else
                    {
                        print("ai_repo: path set to ");
                        print(ai_repo_path());
                        print("\n");
                    }
                }
                else if (shell_cmd_equals(cmd, "ai_trust"))
                {
                    if (argc < 2)
                    {
                        ai_print_trust();
                    }
                    else if (strcmp(argv[1], "load") == 0)
                    {
                        if (argc < 3)
                        {
                            print_err("ai_trust: missing key file\n");
                        }
                        else if (ai_trust_key_load_file(argv[2]) < 0)
                        {
                            print_err("ai_trust: load key failed\n");
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
                        print_err("ai_trust: failed\n");
                    }
                    else
                    {
                        print("ai_trust: trust root set to ");
                        print(ai_trust_root_path());
                        print("\n");
                    }
                }
                else if (shell_cmd_equals(cmd, "ai_ed25519"))
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
                            print_err("ai_ed25519: signature invalid or unsupported, status=");
                            print_err_dec(status);
                            print_err("\n");
                        }
                    }
                    else
                    {
                        print_err("usage: ai_ed25519 [selftest|verify_sha256 <public_key_hex> <sha256_hex> <signature_hex>]\n");
                    }
                }
                else if (shell_cmd_equals(cmd, "ai_model_register"))
                {
                    if (argc < 2)
                    {
                        print_err("ai_model_register: missing manifest path\n");
                    }
                    else if (ai_model_register_manifest_file(argv[1]) < 0)
                    {
                        print_err("ai_model_register: failed\n");
                    }
                    else
                    {
                        print("ai_model_register: registered ");
                        print(argv[1]);
                        print("\n");
                    }
                }
                else if (shell_cmd_equals(cmd, "ai_model_scan"))
                {
                    int n = ai_repo_scan();
                    if (n < 0)
                    {
                        print_err("ai_model_scan: failed\n");
                    }
                    else
                    {
                        print("ai_model_scan: registered ");
                        shell_print_dec(n);
                        print(" manifest(s)\n");
                    }
                }
                else if (shell_cmd_equals(cmd, "devices"))
                {
                    devmgr_print_devices();
                }
                else if (shell_cmd_equals(cmd, "hotplug"))
                {
                    devmgr_print_hotplug_events();
                }
                else if (shell_cmd_equals(cmd, "hotplug_poll"))
                {
                    hotplug_event_t event;
                    if (devmgr_poll_event(&event) < 0)
                    {
                        print_err("hotplug_poll: no event\n");
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
                else if (shell_cmd_equals(cmd, "gui"))
                {
                    gui_print_info();
                }
                else if (shell_cmd_equals(cmd, "guitest"))
                {
                    print("guitest: starting GUI demo...\n");
                    if (!gui_is_ready()) {
                        if (gui_start(1024, 768) != 0) {
                            print_err("guitest: gui_start failed\n");
                            goto shell_command_done;
                        }
                    }
                    gui_demo();
                    print("guitest: GUI demo rendered; graphical terminal remains interactive\n");
                }
                else if (shell_cmd_equals(cmd, "cursor"))
                {
                    if (argc >= 2 && strcmp(argv[1], "on") == 0) {
                        gui_set_cursor_visible(1);
                        print("cursor: OpenOS software cursor on\n");
                    } else if (argc >= 2 && strcmp(argv[1], "off") == 0) {
                        gui_set_cursor_visible(0);
                        print("cursor: OpenOS software cursor off\n");
                    } else {
                        print("cursor: ");
                        print(gui_is_cursor_visible() ? "on" : "off");
                        print("\nusage: cursor [on|off]\n");
                    }
                }
                else if (shell_cmd_equals(cmd, "mouse"))
                {
                    mouse_print_info();
                    print("mouse: status written to serial log\n");
                }
                else if (shell_cmd_equals(cmd, "help"))
                {
                    cmd_help();
                }
                else if (shell_cmd_equals(cmd, "jobs"))
                {
                    cmd_jobs();
                }
                else if (shell_cmd_equals(cmd, "fg"))
                {
                    cmd_fg(argc, argv);
                }
                else if (shell_cmd_equals(cmd, "clear"))
                {
                    vga_clear();
                    gui_terminal_clear();
                }
                else if (shell_cmd_equals(cmd, "yield"))
                {
                    /* 协作式调度测试：主动让出 CPU */
                    serial_write("[YIELD] giving up CPU...\n");
                    __asm__ volatile("int $0x80" : : "a"(201) : "memory");
                }
                else if (shell_cmd_equals(cmd, "exec"))
                {
                    /* 执行 ELF 程序，支持参数：exec /bin/app arg1 arg2 */
                    if (argc > 1)
                    {
                        int ret = shell_spawn_user_program(argv[1], &argv[1], argc - 1);
                        if (ret < 0)
                        {
                            print_err("exec: failed to spawn ");
                            print_err(argv[1]);
                            print_err("\n");
                        }
                        else if (background)
                        {
                            int bg_pids[1];
                            bg_pids[0] = ret;
                            shell_register_background_job(bg_pids, 1, cmd);
                        }
                        else
                        {
                            shell_wait_foreground_pid(ret);
                        }
                    }
                    else
                    {
                        print_err("exec: missing argument\n");
                    }
                }
                else
                {
                    int ret = shell_spawn_user_program(cmd, argv, argc);
                    if (ret < 0) {
                        print_err(cmd);
                        print_err(": command not found\n");
                    } else if (background) {
                        int bg_pids[1];
                        bg_pids[0] = ret;
                        shell_register_background_job(bg_pids, 1, cmd);
                    } else {
                        shell_wait_foreground_pid(ret);
                    }
                }
            }

shell_command_done:
            if (stdout_redirected) {
                shell_stdout_redirect_depth--;
            }
            if (stderr_redirected) {
                shell_stderr_redirect_depth--;
            }
            shell_restore_redirects(saved_stdin, saved_stdout, saved_stderr);

            cmd_pos = 0;
            cmd_cursor = 0;
            cmd_buf[0] = '\0';
            history_view = history_count;
            history_saved_valid = 0;
            history_saved_line[0] = '\0';
            shell_jobs_poll();
            uint32_t reaped = proc_reap_zombies_for_parent(proc_current_pid());
            if (reaped > 0) {
                serial_write("[REAP] collected zombie children: ");
                serial_write_hex(reaped);
                serial_write("\n");
            }
            shell_prompt();
        }
        else if (c == '\t')
        {
            shell_complete();
            cmd_cursor = cmd_pos;
        }
        else if (c == 0x01)  /* Ctrl+A - 跳转到行�?*/
        {
            shell_move_cursor_home();
        }
        else if (c == 0x05)  /* Ctrl+E - 跳转到行�?*/
        {
            shell_move_cursor_end();
        }
        else if (c == SHELL_CTRL_C)  /* Ctrl+C - copy selection or cancel current line */
        {
            if (gui_terminal_copy_selection()) {
                gui_terminal_clear_selection();
            } else {
                shell_cancel_line();
            }
        }
        else if (c == SHELL_CTRL_D)  /* Ctrl+D - send EOF to stdin or logout on empty line */
        {
            if (cmd_pos == 0) {
                print("logout\n");
                return;
            }
            input_mark_eof();
        }
        else if (c == 0x16)  /* Ctrl+V - paste terminal clipboard */
        {
            if (gui_terminal_has_clipboard_text())
                shell_paste_text(gui_terminal_get_clipboard_text());
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
