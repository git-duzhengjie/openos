#ifndef OPENOS_ARCH_X86_64_FDTABLE64_H
#define OPENOS_ARCH_X86_64_FDTABLE64_H

/*
 * 极简 fd 表（Step B）：仅支持只读访问 initrd 内的文件。
 *
 * fd 0/1/2 保留给 stdin/stdout/stderr（write 走 early_console，read 永远返回 0）。
 * fd 3..15 用于 vfs 节点的只读视图。
 *
 * 当 x86_64 接入真正的 proc/file 模型后，本文件会被替换；
 * 现阶段先用一张静态全局表跑通 cat / hello.txt 之类的演示。
 */

#include <stdint.h>

#include "arch64_types.h"
#include "vfs64.h"

#define ARCH_X86_64_FD_MAX 16

void arch_x86_64_fd_init(void);

/* 打开一个 vfs 节点；成功返回 fd（>=3），失败返回 -1。 */
int  arch_x86_64_fd_open(const x86_64_vfs_node_t *node);

/* 关闭 fd；成功返回 0，失败返回 -1。stdin/out/err 不可关闭。 */
int  arch_x86_64_fd_close(int fd);

/* 从 fd 读最多 count 字节到 buf，返回实际字节数；EOF 返回 0；错误返回 -1。 */
int  arch_x86_64_fd_read(int fd, void *buf, x86_64_size_t count);

/* 向 fd 写 count 字节；fd=1/2 走 early_console，其它返回 -1。 */
int  arch_x86_64_fd_write(int fd, const void *buf, x86_64_size_t count);

/* 诊断：返回当前打开数。 */
int  arch_x86_64_fd_open_count(void);

#endif /* OPENOS_ARCH_X86_64_FDTABLE64_H */
