/**
 * 开机锁屏 —— 单用户密码验证门闸
 *
 * 设计：
 *   - 密码不以明文存储，编译期硬编码 SHA256(salt + 密码) 摘要。
 *   - 校验时对输入做同样的 salt 前缀哈希，再用常量时间比较，
 *     防止时序侧信道泄露"密码前几位是否正确"。
 *   - 默认密码为 "openos"（可后续通过重算哈希修改）。
 *
 * 用法：桌面拉起、鼠标/键盘 install + sti 之后、进入桌面主循环之前，
 *   调用 lockscreen_run()。该函数内部自建一个全屏密码窗口并驱动
 *   window_manager_poll()，直到用户输入正确密码才返回。
 */
#ifndef OPENOS_LOCKSCREEN_H
#define OPENOS_LOCKSCREEN_H

/* 阻塞运行锁屏，直到密码验证通过才返回。
 * 返回后锁屏窗口已销毁，调用方可继续进入桌面主循环。 */
void lockscreen_run(void);

#endif /* OPENOS_LOCKSCREEN_H */
