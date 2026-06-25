# OpenOS PC / Mobile 平台能力边界（A8）

## 目标

OpenOS 同时面向 PC 与 Mobile，但两端共享同一套 kernel core、系统服务模型、应用模型和 AI OS 能力。平台差异不进入通用核心：架构相关代码放在 `src/arch/*`，平台相关能力放在 platform 层，策略放到用户态服务。

## PC 侧能力

PC 产品主线以 `x86_64 + UEFI` 为基础，i386 仅保留为 legacy / regression。

| 能力 | 边界说明 |
| --- | --- |
| x86_64 | PC 主线架构，负责长模式、页表、ring3、syscall/sysret 和中断异常。 |
| UEFI | 首选启动方式，负责加载内核、initrd、资源并生成 OpenOSBootInfo。 |
| ACPI | PC 平台发现与电源入口，提供 CPU、APIC、PCI 根和睡眠状态信息。 |
| PCIe | PC 设备总线主线，用于枚举网卡、显卡、NVMe、USB 控制器等设备。 |
| NVMe / SATA / USB | PC 存储与外设重点方向；挂载、授权和热插拔策略由用户态服务处理。 |
| 键盘鼠标 | PC 默认输入模型，由 inputd 做焦点、快捷键、权限和事件路由。 |
| 多显示器 | displayd / compositor 管理输出、缩放、窗口投递和显示权限。 |
| Desktop Shell | 用户态桌面壳，负责任务栏、多窗口、文件管理器和桌面交互。 |

## Mobile 侧能力

Mobile 主线以 `aarch64` 为基础，优先使用 QEMU virt 建立可验证路径，再迁移到真实移动 SoC。

| 能力 | 边界说明 |
| --- | --- |
| aarch64 | Mobile 主线架构，负责 EL1/EL0、异常向量、页表、SVC 和用户态运行。 |
| Device Tree / ACPI | 移动平台发现入口；QEMU virt 优先 DTB，真实设备可按平台选择 ACPI/DT。 |
| SoC 中断与定时器 | GIC、generic timer、IPI 和低功耗唤醒由 arch/platform 层封装。 |
| 触摸 / 手势 | inputd 统一分发触摸、手势、虚拟键和输入焦点。 |
| 传感器 | 加速度计、陀螺仪、光线、距离等由 devmgr 代理枚举与权限授权。 |
| 电池 / 温控 | powerd 管理电池、充电、温控、休眠唤醒和后台预算。 |
| 蜂窝 / Wi-Fi / 蓝牙 | 网络与无线策略由 netd 和权限服务处理，驱动只暴露机制。 |
| Mobile Shell | 用户态移动壳，负责全屏应用、状态栏、通知中心、权限弹窗和后台卡片。 |

## 共享能力

- Kernel core：进程、线程、调度、内存、VFS、IPC、权限、安全审计和资源限制。
- Device Model / Driver Model：统一设备对象、驱动绑定、资源描述、probe/remove 和能力暴露。
- VirtIO：作为 QEMU 与跨架构早期设备主线，优先覆盖 console、block、net、gpu/input。
- OpenOSBootInfo：统一启动信息结构，承载内存图、initrd、framebuffer、cmdline、arch/platform 标签。
- 用户态系统服务：init、servicemgr、devmgr、netd、fsd、permissiond、packaged、logd、displayd、inputd、notificationd、powerd、aid。
- 应用模型：Manifest、包签名、权限声明、生命周期、数据目录和崩溃恢复。
- GUI / Shell：内核只提供显示输入机制，PC Shell 和 Mobile Shell 均为用户态组件。
- AI OS：AI Agent 与 Skill Runtime 运行在用户态 aid 服务中，通过权限、sandbox、IPC 调用系统能力。

## 边界规则

1. 通用内核不直接依赖 PC 或 Mobile 的策略。
2. arch 层只处理 CPU 架构差异，不承载产品策略。
3. platform 层封装启动方式、中断控制器、定时器、设备发现和板级资源。
4. 用户可见策略由系统服务和 Shell 实现，不能写进硬编码内核路径。
5. 新设备优先进入 Device Model / Driver Model，再由用户态 devmgr 决定暴露给应用的能力。

## 阶段验收

- A8.1：PC 侧 x86_64、UEFI、ACPI、PCIe、NVMe/USB、键鼠、多显示器和 Desktop Shell 边界已明确。
- A8.2：Mobile 侧 aarch64、DT/ACPI、GIC/timer、触摸、传感器、电池、无线和 Mobile Shell 边界已明确。
- A8.3：共享 kernel core、系统服务、应用模型、GUI/Shell 和 AI OS 能力已明确。


