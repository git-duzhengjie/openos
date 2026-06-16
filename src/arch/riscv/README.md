# OpenOS RISC-V 架构目录

该目录保存 OpenOS RISC-V 64 位移植骨架，目标优先面向 QEMU `virt` 平台与 RV64 裸机内核。

## 目录

- `boot/`：RISC-V 入口与启动栈占位。
- `include/`：RISC-V 基础类型、平台信息与早期控制台接口。
- `kernel/`：RISC-V 早期内核入口、平台初始化与 16550 UART 输出骨架。
- `linker_riscv.ld`：RISC-V 内核链接脚本，默认装载到 `0x80000000`。

## 当前状态

已建立 RISC-V 移植骨架：

- QEMU virt 平台常量；
- UART0 早期输出接口；
- `_start` 与启动栈占位；
- RV64 kernel main 初始化状态；
- trap、Sv39/Sv48 MMU、PLIC、CLINT、调度和用户态支持仍作为后续任务推进。

## 验证

默认 i386 构建不受影响：

```bash
bash build.sh
```

若本机安装了 RISC-V 交叉工具链，后续可扩展为：

```bash
riscv64-unknown-elf-gcc -ffreestanding -nostdlib -c src/arch/riscv/kernel/kernel_riscv.c
```
