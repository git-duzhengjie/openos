# openos

> **开源 · 智能 · 跨端 · 优雅**

openos 是一个从零开始构建的开源操作系统，旨在打造新一代智能、跨平台的操作系统体验。

## 快速开始

```bash
# 克隆仓库
git clone https://github.com/your-org/openos.git
cd openos

# 构建
mkdir build && cd build
cmake ..
make

# 运行（需要 QEMU）
qemu-system-x86_64 -cdrom openos.iso
```

## 设计理念

- **开源** — 完全开放，社区驱动
- **智能** — AI 原生集成
- **跨端** — 一次开发，多平台运行
- **优雅** — 简洁、高效、美观

## 项目结构

```
openos/
├── docs/          # 文档
├── src/           # 源码
│   ├── kernel/    # 内核
│   ├── services/  # 系统服务
│   ├── middleware/ # 中间件
│   └── user/      # 用户程序
├── tools/         # 工具脚本
└── build/         # 构建输出
```

## 许可证

[MIT License](./LICENSE)