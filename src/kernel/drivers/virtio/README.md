# VirtIO drivers

该目录用于逐步承载跨架构 VirtIO 驱动公共实现。

计划迁移对象：

- VirtIO transport / queue / feature negotiation 公共逻辑。
- virtio-blk、virtio-net、virtio-gpu、virtio-input 等设备驱动中可复用部分。
- x86_64 QEMU 与 aarch64 QEMU virt 共享的 VirtIO 代码。

A9.2 阶段只建立目录边界，不移动现有驱动源码。
