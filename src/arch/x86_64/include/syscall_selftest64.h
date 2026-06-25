#ifndef OPENOS_ARCH_X86_64_SYSCALL_SELFTEST64_H
#define OPENOS_ARCH_X86_64_SYSCALL_SELFTEST64_H

#include <stdint.h>

/*
 * 在内核空间直接驱动 dispatch_common，模拟 ring3 hello64 的调用序列：
 *   write(banner) -> open(/hello.txt) -> read -> write(echo) -> close -> write(done)
 *
 * 目的：在 OVMF/UEFI 启动链路就绪之前，先让 Step C 的 "syscall → VFS → initrd" 链路
 * 有一个确定性、可在 stdout 里抓到的烟雾测试。
 *
 * 返回 0 表示所有步骤成功（含 read>0 且 close==0），否则返回失败步骤号。
 */
int arch_x86_64_syscall_selftest_run(void);

#endif /* OPENOS_ARCH_X86_64_SYSCALL_SELFTEST64_H */
