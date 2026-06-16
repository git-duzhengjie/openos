# QEMU 自动回归测试

OpenOS 提供 `scripts/qemu-smoke.sh` 作为最小自动回归测试入口。

## 本地运行

```bash
scripts/qemu-smoke.sh
```

脚本会执行：

1. 调用 `bash build.sh` 构建 `target/openos.img`；
2. 使用 `qemu-system-i386` 启动镜像；
3. 将串口输出写入 `target/qemu-smoke.log`；
4. 在超时后检查日志中是否存在 OpenOS 启动关键字。

## 常用参数

```bash
scripts/qemu-smoke.sh --no-build --timeout 8
scripts/qemu-smoke.sh --image target/openos.img --log target/qemu-smoke.log
```

## 环境变量

- `OPENOS_QEMU_BIN`：QEMU 可执行文件，默认 `qemu-system-i386`。
- `OPENOS_QEMU_IMAGE`：镜像路径，默认 `target/openos.img`。
- `OPENOS_QEMU_LOG`：串口日志路径，默认 `target/qemu-smoke.log`。
- `OPENOS_QEMU_TIMEOUT`：运行秒数，默认 `12`。
- `OPENOS_QEMU_BUILD`：是否先构建镜像，默认 `1`。

## CI

GitHub Actions 中的 `qemu-smoke-test` job 会复用该脚本，作为启动级自动回归测试。
