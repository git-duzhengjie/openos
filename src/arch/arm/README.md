# OpenOS ARM 架构目录

该目录保存 OpenOS ARM 32 位移植骨架，目标优先面向 QEMU `virt` 平台。

## 目录

- `boot/`：ARM 入口与异常向量占位。
- `include/`：ARM 基础类型、平台信息与早期控制台接口。
- `kernel/`：ARM 早期内核入口、平台初始化与 PL011 UART 输出骨架。
- `linker_arm.ld`：ARM 内核链接脚本，默认装载到 `0x80000000`。

## 当前状态

已建立 ARM 移植骨架：

- QEMU virt 平台常量；
- PL011 UART0 早期输出接口；
- `_start` 与异常向量占位；
- ARM kernel main 初始化状态；
- MMU、GIC、中断、进程和用户态支持仍作为后续任务推进。

## 验证

默认 i386 构建不受影响：

```bash
bash build.sh
```

若本机安装了 ARM 交叉工具链，后续可扩展为：

```bash
arm-none-eabi-gcc -ffreestanding -nostdlib -c src/arch/arm/kernel/kernel_arm.c
```
