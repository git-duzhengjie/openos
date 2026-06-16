# OpenOS CMake / Ninja 构建入口

OpenOS 的主构建脚本仍是 `build.sh`。本文件描述新增的 CMake / Ninja 包装入口，便于本地 IDE、CI 和统一命令调度使用。

## 配置

```bash
cmake --preset ninja-i386
```

也可以配置 x86_64 骨架：

```bash
cmake --preset ninja-x86_64
```

## 构建

默认 i386 镜像：

```bash
cmake --build --preset image
```

显式构建 i386：

```bash
cmake --build --preset i386
```

构建 x86_64 骨架：

```bash
cmake --build --preset x86_64
```

## 检查目标

校验 512 字节 boot sector 与 `0x55AA` 签名：

```bash
cmake --build --preset bootloader-check
```

编译 ARM / RISC-V 架构骨架做语法检查：

```bash
cmake --build --preset arch-syntax-check
```

## 清理

```bash
cmake --build --preset clean
```

## 设计说明

- CMake target 默认调用现有 `build.sh`，不复制内核链接逻辑，避免双构建系统漂移。
- `OPENOS_DEFAULT_ARCH` 可选 `i386` 或 `x86_64`。
- `openos-bootloader-check` 与 `openos-arch-syntax-check` 为 CI/IDE 提供细粒度检查入口。
