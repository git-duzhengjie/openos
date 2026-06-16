# OpenOS 发布打包流程

OpenOS 使用 `scripts/package-release.sh` 生成发布包。

## 快速打包

```bash
bash scripts/package-release.sh --version v0.1.0
```

默认输出目录：

```text
target/release/
├── openos-<version>/
│   ├── bin/
│   │   ├── openos.img
│   │   ├── kernel.elf
│   │   └── kernel.bin
│   ├── docs/
│   ├── scripts/
│   └── MANIFEST.txt
├── openos-<version>.tar.gz
└── openos-<version>.tar.gz.sha256
```

## 常用参数

```bash
bash scripts/package-release.sh --version nightly
bash scripts/package-release.sh --dist-dir target/release-test
bash scripts/package-release.sh --skip-build
```

环境变量：

- `OPENOS_RELEASE_VERSION`：版本号或发布标签。
- `OPENOS_RELEASE_DIR`：输出目录，默认 `target/release`。
- `OPENOS_RELEASE_SKIP_BUILD=1`：跳过构建，直接使用现有 `target/` 产物。

## 发布包内容

发布包至少包含：

- `bin/openos.img`：可启动镜像。
- `bin/kernel.elf`：带符号内核 ELF，供 GDB 使用。
- `bin/kernel.bin`：裸内核二进制。
- `docs/README.md`、`docs/TODOLIST.md`。
- `scripts/` 下的 QEMU smoke 和 GDB 调试辅助脚本。
- `MANIFEST.txt`：版本、提交、工作树状态和文件清单。

## 校验

```bash
cd target/release
sha256sum -c openos-<version>.tar.gz.sha256
```
