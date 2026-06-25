# OpenOS 当前暂缓事项边界（A10）

A10 用于明确当前阶段暂不推进的事项，避免路线过早发散。现阶段优先级是：保持 i386 regression 稳定，推进 x86_64 PC 主线，建立 aarch64 QEMU virt Mobile 基线，并坚持内核机制与用户态策略分离。

## A10.1 暂不直接适配真实手机真机

真实手机涉及启动链、签名链、设备树/ACPI、显示、触摸、电源、基带和安全域等大量平台差异。当前应先完成 aarch64 QEMU virt，再考虑 ARM64 开发板，最后再考虑半开放移动设备或真实手机。

## A10.2 暂不继续把 i386 作为长期产品主线

i386 后续定位为 legacy / regression / debug。PC 产品主线转向 x86_64 + UEFI；Mobile 产品主线转向 aarch64，并从 QEMU virt 开始。

## A10.3 暂不把 Mobile Shell 塞入当前内核 GUI

Mobile Shell 应作为用户态 Shell 演进。当前 kernel GUI 只保持兼容与回归价值，不作为移动端基础架构。内核提供显示、输入、IPC、权限、隔离和资源控制等机制，Shell、compositor、notification、permission 等策略留在用户态。

## A10.4 暂不把 AI Agent 写进内核

AI Agent、Skill Runtime 和智能编排应作为用户态系统服务运行。内核只负责隔离、授权、审计、资源控制、IPC、进程与内存保护。任何 AI 自动化能力都不得绕过用户授权、应用权限和安全审计。

## 验收标准

- 真实手机适配延后到 aarch64 QEMU virt 和开发板之后。
- i386 明确降级为 legacy / regression / debug。
- Mobile Shell 明确为用户态，不进入当前内核 GUI。
- AI Agent 明确为用户态系统服务，不进入内核。
