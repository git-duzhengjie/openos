# Kernel platform implementations

该目录用于承载平台相关实现，例如 PC BIOS、PC UEFI、QEMU aarch64 virt 等 `platform_ops` 后端。

边界：

- 平台探测、平台设备枚举、平台电源/启动参数适配属于本目录。
- CPU 架构机制属于 `src/arch/<arch>/`。
- 架构无关内核核心逻辑属于 `src/kernel/core/`。
