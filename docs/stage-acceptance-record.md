# OpenOS 阶段验收记录（A11）

## M1：当前主线稳定

验证时间：2026-06-25

| 门禁 | 命令 | 结果 |
| --- | --- | --- |
| 单元测试 | `bash build.sh test` | 通过，`TEST_RC=0` |
| 默认构建 | `bash build.sh` | 通过，`DEFAULT_RC=0`，生成 `target/openos.img` |
| x86_64 构建 | `ARCH=x86_64 bash build.sh` | 通过，`X86_64_RC=0`，生成 `target/x86_64/kernel64.elf`、`target/x86_64/bin/hello64.elf`、`target/x86_64/boot/BOOTX64.EFI` |

## 暂未勾选的里程碑

后续 M2-M7 需要继续以真实 QEMU/UEFI/EL0/VirtIO 冒烟结果为准逐项勾选，不能仅凭源码或文档规划标记完成。
