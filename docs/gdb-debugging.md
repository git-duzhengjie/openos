# OpenOS GDB 调试脚本

OpenOS 提供两种 i386 可启动镜像的 GDB 调试入口：

1. `scripts/debug-qemu-gdb.sh`：只启动暂停状态的 QEMU gdbstub，适合双终端调试。
2. `scripts/gdb-openos.sh`：构建镜像、启动 QEMU gdbstub，并自动打开 GDB。

## 双终端调试

终端 1：

```bash
bash build.sh
bash scripts/debug-qemu-gdb.sh
```

终端 2：

```bash
gdb -q -x scripts/gdb-openos.gdb
(gdb) openos-connect
(gdb) openos-break-boot
(gdb) continue
```

## 一键调试

```bash
bash scripts/gdb-openos.sh
```

常用参数：

```bash
bash scripts/gdb-openos.sh --no-build
bash scripts/gdb-openos.sh --port 1235
bash scripts/gdb-openos.sh --no-gdb
bash scripts/gdb-openos.sh -- --display none
```

## 环境变量

- `OPENOS_GDB_BUILD`：是否先构建，默认 `1`。
- `OPENOS_GDB_PORT`：GDB 远程端口，默认 `1234`。
- `OPENOS_GDB_IMAGE` / `OPENOS_QEMU_IMAGE`：磁盘镜像路径。
- `OPENOS_GDB_KERNEL`：带符号内核 ELF，默认 `target/kernel.elf`。
- `OPENOS_QEMU_BIN`：QEMU 可执行文件，默认 `qemu-system-i386`。
- `OPENOS_GDB_BIN`：GDB 可执行文件，默认 `gdb`。

## GDB 辅助命令

`scripts/gdb-openos.gdb` 默认提供：

- `openos-help`：显示辅助命令。
- `openos-connect`：连接 QEMU gdbstub。
- `openos-break-boot`：设置启动/异常路径常用断点。
- `openos-regs`：打印寄存器、栈和当前指令。
- `openos-panic-dump`：打印内核最近一次 panic dump 符号。

`scripts/openos.gdb` 用于 `scripts/gdb-openos.sh` 自动连接后的轻量会话配置。
