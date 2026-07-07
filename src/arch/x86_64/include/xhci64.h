/*
 * xhci64.h — xHCI USB 主机控制器驱动对外接口（M2.3）
 *
 * 复用 M1.1 PCI 枚举 + pmm64 物理连续分配 + identity-map(phys==virt)
 * + M2.1/M2.2 AHCI/NVMe 打下的 MMIO 显式映射 + MSI/MSI-X 套路。
 *
 * MVP 范围：
 *   - 单控制器、polling 模式
 *   - Command Ring + Event Ring（单段 ERST）+ DCBAA
 *   - 端口复位 + 枚举 root hub 端口，识别已连接设备
 *   - 上报设备连接/速率信息（不做完整 USB 设备驱动栈）
 *
 * 对外接口风格对齐 nvme64.h：
 *   xhci_init()          探测 xHCI 控制器 + 初始化命令/事件环 + 枚举端口
 *   xhci_present()       是否探测到可用 xHCI 控制器
 *   xhci_port_count()    root hub 端口数
 *   xhci_selftest()      headless 自测：探测+端口枚举
 */
#ifndef OPENOS_XHCI64_H
#define OPENOS_XHCI64_H

#include <stdint.h>

/* 探测并初始化第一个 xHCI 控制器。返回 0 成功，负数失败。 */
int xhci_init(void);

/* 是否已成功初始化并探测到控制器。 */
int xhci_present(void);

/* root hub 端口总数。未就绪返回 0。 */
uint32_t xhci_port_count(void);

/* 已连接（CCS=1）的端口数。 */
uint32_t xhci_connected_ports(void);

/* headless 自测：探测控制器 + 端口枚举，打印连接状态。返回 0 PASS。 */
int xhci_selftest(void);

/* M2.3 MSI：延迟挂载中断。需在 LAPIC 就绪后调用（storage init 早于 LAPIC）。
 * Event Ring 已配置 interrupter 0 的 IMAN.IE；此处补装 PCI MSI enable。幂等。 */
void xhci_irq_install_late(void);

/* 返回中断触发次数（调试验证：>0 证明 MSI/MSI-X 中断路径真实生效） */
uint32_t xhci_irq_count(void);

#endif /* OPENOS_XHCI64_H */
