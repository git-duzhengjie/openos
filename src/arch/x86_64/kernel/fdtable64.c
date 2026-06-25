/*
 * x86_64 极简文件描述符表
 *
 * 当前仅支持只读 vfs 节点 + 三个标准描述符。
 * 设计原则：零行为依赖于进程模型（无 proc64 时也能工作）。
 */

#include "../include/fdtable64.h"

#include <stddef.h>

#include "../include/early_console64.h"

typedef struct fd_entry {
    const x86_64_vfs_node_t *node;  /* NULL 表示空闲 */
    x86_64_size_t            offset;
    uint8_t                  reserved;  /* 1=stdin/out/err 保留槽 */
} fd_entry_t;

static fd_entry_t g_fd_table[ARCH_X86_64_FD_MAX];

void arch_x86_64_fd_init(void) {
    for (int i = 0; i < ARCH_X86_64_FD_MAX; ++i) {
        g_fd_table[i].node     = NULL;
        g_fd_table[i].offset   = 0;
        g_fd_table[i].reserved = 0;
    }
    /* fd 0/1/2：标准流，永远占用，不可关闭 */
    g_fd_table[0].reserved = 1;
    g_fd_table[1].reserved = 1;
    g_fd_table[2].reserved = 1;
}

int arch_x86_64_fd_open(const x86_64_vfs_node_t *node) {
    if (node == NULL)
        return -1;
    for (int i = 3; i < ARCH_X86_64_FD_MAX; ++i) {
        if (g_fd_table[i].node == NULL && !g_fd_table[i].reserved) {
            g_fd_table[i].node   = node;
            g_fd_table[i].offset = 0;
            return i;
        }
    }
    return -1;
}

int arch_x86_64_fd_close(int fd) {
    if (fd < 3 || fd >= ARCH_X86_64_FD_MAX)
        return -1;
    if (g_fd_table[fd].node == NULL)
        return -1;
    g_fd_table[fd].node   = NULL;
    g_fd_table[fd].offset = 0;
    return 0;
}

int arch_x86_64_fd_read(int fd, void *buf, x86_64_size_t count) {
    if (buf == NULL && count > 0)
        return -1;
    if (fd == 0) /* stdin 暂无输入源 */
        return 0;
    if (fd < 0 || fd >= ARCH_X86_64_FD_MAX)
        return -1;
    if (fd == 1 || fd == 2) /* stdout/stderr 不可读 */
        return -1;

    fd_entry_t *e = &g_fd_table[fd];
    if (e->node == NULL)
        return -1;

    x86_64_size_t remain = (e->offset >= e->node->size)
                             ? 0
                             : (e->node->size - e->offset);
    x86_64_size_t n = (count < remain) ? count : remain;
    if (n == 0)
        return 0;

    /* 字节级 memcpy（避免拉入完整 string 库依赖） */
    uint8_t       *dst = (uint8_t *)buf;
    const uint8_t *src = e->node->data + e->offset;
    for (x86_64_size_t i = 0; i < n; ++i)
        dst[i] = src[i];

    e->offset += n;
    return (int)n;
}

int arch_x86_64_fd_write(int fd, const void *buf, x86_64_size_t count) {
    if (buf == NULL && count > 0)
        return -1;
    if (fd != 1 && fd != 2)
        return -1;
    const char *p = (const char *)buf;
    for (x86_64_size_t i = 0; i < count; ++i)
        early_console64_putc(p[i]);
    return (int)count;
}

int arch_x86_64_fd_open_count(void) {
    int n = 0;
    for (int i = 3; i < ARCH_X86_64_FD_MAX; ++i)
        if (g_fd_table[i].node != NULL)
            ++n;
    return n;
}
