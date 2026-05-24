/* ============================================================
 * openos - 简易 Shell 实现 (Phase 3)
 * ============================================================ */

#include "shell.h"
#include "serial.h"
#include "string.h"
#include "types.h"
#include "pmm.h"
#include "process.h"
#include "input_buffer.h"
#include "../fs/vfs.h"
#include "../fs/ramfs.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

/* inb 辅助 */
static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    asm volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

#define CMD_BUF_SIZE 256
#define MAX_ARGS     16

static char cmd_buf[CMD_BUF_SIZE];
static int  cmd_pos = 0;

/* 当前工作目录 */
static char cwd[MAX_PATH] = "/";

/* ---- 辅助函数 ---- */
static void shell_prompt(void) {
    serial_write("openos:");
    serial_write(cwd);
    serial_write("$ ");
}

static int split_args(char *buf, char *argv[], int max) {
    int argc = 0;
    int i = 0;
    while (buf[i] && argc < max) {
        while (buf[i] == ' ') i++;
        if (!buf[i]) break;
        argv[argc++] = &buf[i];
        while (buf[i] && buf[i] != ' ') i++;
        if (buf[i]) buf[i++] = '\0';
    }
    return argc;
}

static void make_path(const char *arg, char *out) {
    if (!arg) { out[0] = '/'; out[1] = '\0'; return; }
    if (arg[0] == '/') {
        /* 绝对路径 */
        int i;
        for (i = 0; arg[i] && i < MAX_PATH - 1; i++) out[i] = arg[i];
        out[i] = '\0';
    } else {
        /* 相对路径 */
        int ci = 0;
        while (cwd[ci]) ci++;
        for (int i = 0; i < ci && i < MAX_PATH - 1; i++) out[i] = cwd[i];
        if (ci > 1) { out[ci] = '/'; ci++; }
        for (int i = 0; arg[i] && ci < MAX_PATH - 1; i++, ci++)
            out[ci] = arg[i];
        out[ci] = '\0';
    }
}

/* ---- 内置命令 ---- */

static void cmd_ls(const char *path) {
    char full[MAX_PATH];
    make_path(path, full);
    
    dentry_t *d = vfs_path_lookup(full);
    if (!d || !d->inode) {
        serial_write("ls: not found\n");
        return;
    }
    if ((d->inode->mode & 0xFF) != FS_DIR) {
        serial_write(full);
        serial_write("\n");
        return;
    }
    
    int idx = 0;
    dentry_t *child = d->child;
    while (child) {
        serial_write(child->name);
        if (child->inode && (child->inode->mode & 0xFF) == FS_DIR)
            serial_write("/");
        serial_write("  ");
        child = child->sibling;
        idx++;
    }
    if (idx > 0) serial_write("\n");
}

static void cmd_cat(const char *path) {
    char full[MAX_PATH];
    make_path(path, full);
    
    int fd = vfs_open(full, O_RDONLY, 0);
    if (fd < 0) {
        serial_write("cat: cannot open\n");
        return;
    }
    
    char buf[256];
    int n;
    while ((n = vfs_read(fd, buf, 255)) > 0) {
        buf[n] = '\0';
        serial_write(buf);
    }
    serial_write("\n");
    vfs_close(fd);
}

static void cmd_mkdir(const char *path) {
    char full[MAX_PATH];
    make_path(path, full);
    if (vfs_mkdir(full, 0755) < 0)
        serial_write("mkdir: failed\n");
}

static void cmd_touch(const char *path) {
    char full[MAX_PATH];
    make_path(path, full);
    int fd = vfs_open(full, O_CREAT | O_RDWR, 0644);
    if (fd < 0) {
        serial_write("touch: failed\n");
        return;
    }
    vfs_close(fd);
}

static void cmd_echo(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) serial_write(" ");
        serial_write(argv[i]);
    }
    serial_write("\n");
}

static void cmd_cd(const char *path) {
    if (!path) {
        cwd[0] = '/'; cwd[1] = '\0';
        return;
    }
    char full[MAX_PATH];
    make_path(path, full);
    dentry_t *d = vfs_path_lookup(full);
    if (!d || !d->inode || (d->inode->mode & 0xFF) != FS_DIR) {
        serial_write("cd: not a directory\n");
        return;
    }
    int i;
    for (i = 0; full[i] && i < MAX_PATH - 1; i++) cwd[i] = full[i];
    cwd[i] = '\0';
}

static void cmd_pwd(void) {
    serial_write(cwd);
    serial_write("\n");
}

static void cmd_rm(const char *path) {
    char full[MAX_PATH];
    make_path(path, full);
    if (vfs_unlink(full) < 0)
        serial_write("rm: failed\n");
}

static void cmd_write(const char *path, const char *data) {
    char full[MAX_PATH];
    make_path(path, full);
    int fd = vfs_open(full, O_CREAT | O_RDWR, 0644);
    if (fd < 0) {
        serial_write("write: cannot open\n");
        return;
    }
    int len = 0;
    while (data[len]) len++;
    vfs_write(fd, data, len);
    vfs_close(fd);
}

static void cmd_help(void) {
    serial_write("openos shell - Available commands:\n");
    serial_write("  ls [path]       - List directory\n");
    serial_write("  cat <file>      - Display file content\n");
    serial_write("  mkdir <dir>     - Create directory\n");
    serial_write("  touch <file>    - Create empty file\n");
    serial_write("  rm <file>       - Delete file\n");
    serial_write("  write <f> <txt> - Write text to file\n");
    serial_write("  echo <text>     - Print text\n");
    serial_write("  cd [path]       - Change directory\n");
    serial_write("  pwd             - Print working directory\n");
    serial_write("  help            - Show this help\n");
    serial_write("  clear           - Clear screen\n");
}

/* ---- Shell 主循环 ---- */
void shell_run(void) {
    serial_write("\n=== openos shell ===\n");
    serial_write("Type 'help' for commands\n\n");
    
    cmd_buf[0] = '\0';
    cmd_pos = 0;
    shell_prompt();
    
    /* 从串口读取输入（轮询方式） */
    while (1) {
        /* 检查键盘输入 — 通过串口端口 0x3F8 读取 */
        /* 简化：使用 serial_read 如果可用，否则用键盘缓冲区 */
        /* Phase 3 先用串口回显模式 */
        
        /* 先把串口数据灌入输入缓冲区 */
        if ((inb(0x3FD) & 0x01)) {
            char sc = inb(0x3F8);
            input_putc(sc);
        }
        
        /* 从统一输入缓冲区读取 */
        char c = input_getc();
        
        if (!c) continue;
        
        if (c == '\r' || c == '\n') {
            cmd_buf[cmd_pos] = '\0';
            serial_write("\n");
            
            /* 解析命令 */
            char *argv[MAX_ARGS];
            int argc = split_args(cmd_buf, argv, MAX_ARGS);
            
            if (argc > 0) {
                char *cmd = argv[0];
                if (strcmp(cmd, "ls") == 0) {
                    cmd_ls(argc > 1 ? argv[1] : ".");
                } else if (strcmp(cmd, "cat") == 0) {
                    if (argc > 1) cmd_cat(argv[1]);
                    else serial_write("cat: missing argument\n");
                } else if (strcmp(cmd, "mkdir") == 0) {
                    if (argc > 1) cmd_mkdir(argv[1]);
                    else serial_write("mkdir: missing argument\n");
                } else if (strcmp(cmd, "touch") == 0) {
                    if (argc > 1) cmd_touch(argv[1]);
                    else serial_write("touch: missing argument\n");
                } else if (strcmp(cmd, "rm") == 0) {
                    if (argc > 1) cmd_rm(argv[1]);
                    else serial_write("rm: missing argument\n");
                } else if (strcmp(cmd, "echo") == 0) {
                    cmd_echo(argc, argv);
                } else if (strcmp(cmd, "cd") == 0) {
                    cmd_cd(argc > 1 ? argv[1] : NULL);
                } else if (strcmp(cmd, "pwd") == 0) {
                    cmd_pwd();
                } else if (strcmp(cmd, "write") == 0) {
                    if (argc > 2) cmd_write(argv[1], argv[2]);
                    else serial_write("write: need file and text\n");
                } else if (strcmp(cmd, "help") == 0) {
                    cmd_help();
                } else if (strcmp(cmd, "clear") == 0) {
                    serial_write("\x1b[2J\x1b[H");
                } else if (strcmp(cmd, "yield") == 0) {
                    /* 协作式调度测试：主动让出 CPU */
                    serial_write("[YIELD] giving up CPU...\n");
                    __asm__ volatile("int $0x80" : : "a"(201) : "memory");
                } else {
                    serial_write(cmd);
                    serial_write(": command not found\n");
                }
            }
            
            cmd_pos = 0;
            cmd_buf[0] = '\0';
            shell_prompt();
        } else if (c == 0x7F || c == 0x08) {
            /* 退格 */
            if (cmd_pos > 0) {
                cmd_pos--;
                cmd_buf[cmd_pos] = '\0';
                serial_write("\b \b");
            }
        } else if (c >= ' ' && cmd_pos < CMD_BUF_SIZE - 1) {
            cmd_buf[cmd_pos++] = c;
            /* 回显 */
            char echo[2] = {c, '\0'};
            serial_write(echo);
        }
    }
}
