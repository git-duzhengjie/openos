# OpenOS CI 自动构建

OpenOS 使用 GitHub Actions 执行自动构建与基础检查。

## 工作流

配置文件：`.github/workflows/ci.yml`

触发方式：

- push 到 `main` / `master`；
- pull request 到 `main` / `master`；
- 手动 `workflow_dispatch`。

## Jobs

- `build-i386`：安装 i386 构建依赖，执行 `git diff --check` 与 `bash build.sh`，上传 `openos.img`、`kernel.elf`、`kernel.bin`。
- `bootloader-check`：用 NASM 编译 boot sector，并检查长度为 512 字节、签名为 `55aa`。
- `arch-syntax-check`：用 GCC 编译 ARM / RISC-V 架构骨架 C 文件，作为语法检查。
- `cmake-metadata-check`：安装 CMake / Ninja 并验证 `ninja-i386` preset 可配置。

## 本地等价检查

```bash
git diff --check
nasm -f bin src/boot/boot.asm -o /tmp/openos-boot.bin
bash build.sh
```
