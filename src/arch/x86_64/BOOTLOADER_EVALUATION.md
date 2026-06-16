# x86_64 Bootloader 评估

## 背景

OpenOS 当前 i386 路径使用自研 BIOS boot sector + kernel loader。x86_64 引入 long mode、4 级分页、UEFI、内存地图、Framebuffer、SMP 等能力后，继续完全自研 bootloader 的复杂度会明显上升。

## 方案对比

| 方案 | 优点 | 风险 / 成本 | 初步结论 |
| --- | --- | --- | --- |
| 自研 BIOS loader | 完全可控，适合学习与最小闭环 | 需要维护实模式、保护模式、long mode、磁盘加载、内存地图等大量细节 | 保留为教学和调试路径 |
| Limine | 支持 BIOS/UEFI、x86_64、高半内核、Framebuffer、内存地图，文档和生态较成熟 | 需要引入外部 boot 协议和构建集成 | 后续优先评估 |
| BOOTBOOT | 协议简洁，能减少早期启动代码 | 生态和可定制性需要进一步确认 | 备选 |
| Multiboot2 | 标准化程度高，GRUB 支持成熟 | 高半 direct-map、Framebuffer、UEFI 体验不如 Limine 直接 | 可作为兼容方案 |

## 当前决策

1. 短期继续保留并维护自研 BIOS long mode 骨架，保证架构迁移过程可控。
2. 中期优先评估 Limine，目标是降低 UEFI、高半内核、Framebuffer 和内存地图接入成本。
3. BOOTBOOT 和 Multiboot2 暂列为备选，不阻塞当前 x86_64 内核骨架推进。
4. 默认 i386 构建路径保持不变，任何现代 bootloader 集成都必须通过独立 `ARCH=x86_64` 路径接入。

## 后续接入检查项

- 能否加载 higher-half ELF64 kernel。
- 能否提供完整内存地图。
- 能否提供 framebuffer 信息。
- 能否在 BIOS 与 UEFI 下保持一致入口。
- 构建脚本是否能在无外部网络依赖的情况下完成回归验证。
