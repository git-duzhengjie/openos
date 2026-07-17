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

/* MVP 阶段支持的最大 USB 设备数（与 xhci64.c 保持一致） */
#ifndef XHCI_MAX_DEVS
#define XHCI_MAX_DEVS   4
#endif

/* 返回中断触发次数（调试验证：>0 证明 MSI/MSI-X 中断路径真实生效） */
uint32_t xhci_irq_count(void);

/* ============================================================
 * M2.3 Step3-4: HID 层对接口（供 usb_hid64.c 调用）
 *
 * 设备栈内部结构（xhci_dev_t / g_devs）保持 static 封装，
 * HID 层通过"设备索引"不透明句柄 + 访问器函数间接操作，
 * 避免暴露 slot/context/ring 等硬件细节。
 * ============================================================ */

/* 已成功枚举的 HID 设备数量（proto=1 键盘 / proto=2 鼠标）。 */
uint32_t xhci_hid_device_count(void);

/* 取第 idx 个 HID 设备的 boot protocol：1=键盘 2=鼠标 0=非法/其它。 */
uint32_t xhci_hid_device_proto(uint32_t idx);

/* 取第 idx 个 HID 设备的 USB VID / PID（用于识别 QEMU tablet 等非 boot 设备）。 */
uint32_t xhci_hid_device_vid(uint32_t idx);
uint32_t xhci_hid_device_pid(uint32_t idx);

/* 取第 idx 个 HID 设备的 Interrupt-IN 端点 report 长度（字节）。 */
uint32_t xhci_hid_device_report_len(uint32_t idx);

/* HID 设备细分类型（供 usb_hid64.c 手势映射层使用） */
typedef enum {
    XHCI_HID_TYPE_UNKNOWN     = 0,
    XHCI_HID_TYPE_BOOT_KEYBD  = 1,    /* proto=1 boot keyboard */
    XHCI_HID_TYPE_BOOT_MOUSE  = 2,    /* proto=2 boot mouse */
    XHCI_HID_TYPE_TABLET      = 3,    /* QEMU usb-tablet / 数位板（绝对坐标 + 按键） */
    XHCI_HID_TYPE_TOUCHSCREEN = 4,    /* 单点触屏（Digitizer / TouchScreen） */
    XHCI_HID_TYPE_MULTITOUCH  = 5,    /* 多点触屏（预留 M8-C） */
} xhci_hid_type_t;

/* 取第 idx 个 HID 设备的细分类型（在 usb_hid_init 时确定）。 */
xhci_hid_type_t xhci_hid_device_type(uint32_t idx);

/* 设置第 idx 个 HID 设备的细分类型（供 usb_hid64.c 探测后回填）。 */
void xhci_hid_device_set_type(uint32_t idx, xhci_hid_type_t type);

/* 为第 idx 个 HID 设备配置 Interrupt-IN 端点：
 *   建 Transfer Ring → Configure Endpoint 命令 → SET_PROTOCOL(boot)
 *   → 投递首个 Normal TRB 等待 report。
 * 返回 0 成功，负数失败。幂等（重复调用直接返回 0）。 */
int xhci_hid_configure(uint32_t idx);

/* 非阻塞探测第 idx 个 HID 设备的 Interrupt-IN 传输事件：
 *   命中一个 report → 拷贝到 out_buf（最多 out_cap 字节）并重新投递
 *   下一个 Normal TRB，返回实际 report 字节数（>0）。
 *   无新事件 → 返回 0。出错 → 返回负数。 */
int xhci_hid_poll(uint32_t idx, uint8_t *out_buf, uint32_t out_cap);

/* ============================================================
 * HID 平台层（usb_hid64.c）——报文解析 + input 上报
 * ============================================================ */

/* 枚举 xHCI 已识别的 HID 设备，配置端点并注册 input 设备。 */
void usb_hid_init(void);

/* polling：由内核主循环周期调用，非阻塞取 report 并上报。 */
void usb_hid_poll(void);

/* ============================================================
 * M2.3 Mass Storage(BOT) —— Bulk 端点原语（供 usb_msc.c 调用）
 * ============================================================ */

/* 已识别的 Mass Storage(BOT) 设备数量。 */
uint32_t xhci_msc_device_count(void);

/* 取第 idx 个 MSC 设备的 slot_id / 接口号 / bulk 端点地址。
 * 返回 0 成功，负数表示 idx 非法。 */
int xhci_msc_device_info(uint32_t idx, uint32_t *slot_id, uint8_t *iface,
                         uint8_t *ep_in, uint8_t *ep_out);

/* 为第 idx 个 MSC 设备配置 Bulk IN + Bulk OUT 端点：
 *   建两个 Transfer Ring → 一次 Configure Endpoint 命令加入两个 EP。
 * 返回 0 成功，负数失败。幂等。 */
int xhci_msc_configure(uint32_t idx);

/* 同步 Bulk 传输：向第 idx 个 MSC 设备的 bulk 端点发/收数据。
 *   dir_in=1 表示 Bulk IN（设备→主机），=0 表示 Bulk OUT。
 *   buf_phys 为数据缓冲物理地址（恒等映射），len 为字节数。
 * 返回实际传输字节数(>=0) 或负数错误。 */
int xhci_msc_bulk_transfer(uint32_t idx, int dir_in, uint64_t buf_phys, uint32_t len);

/* EP0 控制传输透传：供 BOT Reset / Get Max LUN 等 class 请求使用。
 * 语义同内部 xhci_ep0_transfer。返回实际字节数或负数。 */
int xhci_msc_control(uint32_t idx, uint8_t bmReqType, uint8_t bReq,
                     uint16_t wValue, uint16_t wIndex, uint16_t wLength,
                     uint64_t buf_phys);

/* USB Mass Storage 初始化：遍历 xHCI 已枚举的 MSC 接口，逐个 attach
 * （配置 Bulk 端点 + INQUIRY/READ CAPACITY）。由内核初始化调用。 */
void usb_msc_init(void);

#endif /* OPENOS_XHCI64_H */
