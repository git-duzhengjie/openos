# OpenOS 系统服务用户态化路线（A7）

## 目标

OpenOS 坚持“内核提供机制，用户态服务提供策略”。内核保持小而稳定，负责进程、内存、IPC、权限、安全审计、资源限制和最小设备机制；设备管理、网络、文件策略、显示输入、通知、包管理、电源管理和 AI OS 能力放到用户态系统服务中演进。

这样可以降低内核复杂度，让策略可热更新、可隔离、可审计，也便于 PC 与 Mobile 共享同一套服务模型。

## 内核 / 用户态边界

### 内核负责机制

- 进程、线程、调度和上下文切换。
- 虚拟内存、地址空间隔离、页权限和共享内存映射。
- IPC 原语：端口、消息队列、事件、共享内存句柄和同步对象。
- 权限校验：进程身份、服务身份、能力令牌和资源访问控制。
- 安全审计：系统调用、权限授权、跨进程句柄传递和敏感设备访问记录。
- 资源限制：CPU、内存、句柄数、IPC 队列、设备带宽和后台执行限制。
- 最小设备机制：中断、MMIO/DMA 授权、driver model 和故障隔离入口。

### 用户态服务负责策略

- 设备枚举、驱动绑定、热插拔和设备权限授权。
- 网络连接、DNS、代理、防火墙和连接状态上报。
- 文件系统挂载、外部存储、用户数据目录和包数据目录管理。
- 显示、输入、通知、权限弹窗、桌面 / 移动 shell 等交互策略。
- 包管理、应用安装 / 卸载 / 更新、Manifest 校验和生命周期管理。
- 日志采集、崩溃报告、电源管理、温控、电池策略和后台限制。
- AI Agent、AI Skill Runtime、模型 / 工具权限、安全沙箱和系统能力代理。

## 核心系统服务

| 服务 | 建议路径 | 主要职责 |
| --- | --- | --- |
| init | /sbin/init | 第一个用户态进程；挂载根命名空间；启动 servicemgr 和基础服务；进入降级 shell。 |
| servicemgr | /sbin/servicemgr | 服务注册、发现、依赖排序、重启策略和能力授予。 |
| devmgr | /sbin/devmgr | 设备枚举、驱动绑定、热插拔处理和设备权限代理。 |
| netd | /sbin/netd | 网络接口、地址、路由、DNS、代理、防火墙和连接状态。 |
| fsd | /sbin/fsd | 文件系统挂载策略、外部存储、用户数据目录和包数据目录。 |
| permissiond | /sbin/permissiond | 应用权限决策、权限弹窗、授权记录和敏感能力审计。 |
| packaged | /sbin/packaged | 应用包安装、签名校验、Manifest 解析、升级回滚和卸载。 |
| logd | /sbin/logd | 内核日志、用户态日志、崩溃记录和审计事件汇聚。 |
| displayd | /sbin/displayd | 显示设备、缓冲区分配、合成器接入和显示权限策略。 |
| inputd | /sbin/inputd | 键盘、鼠标、触摸、传感器输入分发和焦点路由。 |
| notificationd | /sbin/notificationd | 通知路由、优先级、免打扰和跨设备通知同步策略。 |
| powerd | /sbin/powerd | 电源状态、休眠唤醒、温控、电池策略和后台执行预算。 |
| aid | /sbin/aid | AI Agent 会话、Skill Runtime、工具权限、模型路由和安全审计。 |

## 启动顺序

1. 内核加载 /sbin/init，传递 BootInfo、cmdline、initrd/VFS 根信息。
2. init 建立最小运行环境，挂载 /dev、/proc、/sys 的用户态视图。
3. init 启动 servicemgr，注册核心 IPC 命名空间。
4. servicemgr 按依赖启动 logd、permissiond、devmgr、fsd、netd。
5. 基础服务就绪后启动 packaged、appmgr、notificationd、powerd、aid。
6. PC 平台启动 openos-compositor 与 openos-desktop-shell；Mobile 平台启动 openos-mobile-shell。
7. 关键服务崩溃时由 servicemgr 按策略重启、降级或进入 rescue shell。

## AI OS 能力服务化

AI 能力默认不进入内核。内核只提供身份、权限、IPC、共享内存、审计和资源限制机制；用户态 aid 负责 AI Agent 会话、Skill Runtime、模型路由、工具调用授权、提示词/上下文隔离、日志脱敏和失败降级。

aid 调用系统能力时必须经过 permissiond 授权，并通过 servicemgr 解析目标服务。敏感工具调用需要记录到 logd；长时间后台 AI 任务需要由 powerd 和 appmgr 联合约束资源预算。

## PC / Mobile 差异

- PC：偏向多窗口、多显示器、键鼠输入、后台常驻服务和桌面通知。
- Mobile：偏向前台应用、手势输入、传感器、电池预算、后台冻结和权限弹窗。
- 两端共享 servicemgr、permissiond、logd、packaged、appmgr 和 aid；差异通过平台策略文件、Manifest 能力声明和用户授权记录表达。

## 阶段验收

- A7.1：文档明确内核机制与用户态策略边界。
- A7.2：文档列出核心系统服务职责、依赖和启动顺序。
- A7.3：文档明确 AI Agent / Skill Runtime 不进入内核，以 aid 用户态服务承载。
- 默认构建应继续通过，保证文档路线不会破坏现有内核构建。


