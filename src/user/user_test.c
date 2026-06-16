/* ============================================================
 * openos - 用户态测试程序
 * 在 ring 3 执行的用户代码
 * ============================================================ */

#include "openos.h"

/* 用户程序入口点 */
void _start(void)
{
    /* 在 VGA 屏幕底部输出 */
    const char *msg = "Hello from USER MODE!\n";

    /* 调用当前用户态 syscall wrapper */
    openos_write(STDOUT_FILENO, msg, (int)openos_strlen(msg));

    /* 获取当前 PID，验证基础进程 syscall 可用 */
    (void)openos_getpid();

    openos_exit(0);
}
