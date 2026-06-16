# OpenOS 版本号与 Release 管理

OpenOS 使用根目录 `VERSION` 作为基础版本来源。

## 查询版本

```bash
bash scripts/version.sh --base
bash scripts/version.sh --commit
bash scripts/version.sh --dirty
bash scripts/version.sh --full
```

`--full` 输出格式：

```text
<base>+<git-short-commit>[.dirty]
```

例如：

```text
0.1.0-dev+a1b2c3d.dirty
```

## 生成内核版本头

构建时会自动生成：

```text
src/kernel/include/version.h
```

也可以手动生成：

```bash
bash scripts/gen-version-header.sh
```

头文件包含：

- `OPENOS_VERSION_BASE`
- `OPENOS_VERSION_FULL`
- `OPENOS_VERSION_GIT_COMMIT`
- `OPENOS_VERSION_GIT_DIRTY`

## 发布版本

发布脚本默认使用 `scripts/version.sh --full`，也可以显式传入版本：

```bash
bash scripts/package-release.sh
bash scripts/package-release.sh --version v0.1.0
```

正式发布建议流程：

1. 更新 `VERSION`。
2. 完整构建和测试：`bash build.sh && ./build.sh test`。
3. 打包：`bash scripts/package-release.sh --version vX.Y.Z`。
4. 校验：`cd target/release && sha256sum -c openos-vX.Y.Z.tar.gz.sha256`。
5. 创建 Git tag。
