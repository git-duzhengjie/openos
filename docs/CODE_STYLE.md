# openos 代码规范

## C 语言规范（内核代码）

- **风格**: Linux Kernel 风格
- **缩进**: 制表符（Tab），宽度 8 字符
- **命名**: snake_case（内核函数/变量）
- **注释**: Doxygen 风格 `/** */`
- **头文件**: `#ifndef` / `#define` / `#endif` 保护

```c
/**
 * @brief 初始化内存管理器
 * @param start 起始地址
 * @param size  内存大小
 * @return 成功返回 0，失败返回 -1
 */
int mm_init(uint64_t start, size_t size)
{
        // 实现代码
}
```

## Rust 语言规范（系统服务）

- 遵循 `rustfmt` 默认格式
- 使用 `clippy` 进行代码检查
- 模块命名：snake_case
- 类型命名：UpperCamelCase

## TypeScript 规范（应用层）

- 使用 Prettier 格式化
- ESLint 配置使用推荐的 TypeScript 规则
- 变量/函数：camelCase
- 类/接口：PascalCase

## 提交规范 (Conventional Commits)

```
<type>(<scope>): <subject>

<body>
```

- `feat`: 新功能
- `fix`: 修复 Bug
- `docs`: 文档变更
- `refactor`: 重构
- `style`: 格式调整
- `test`: 测试相关
- `chore`: 构建/工具变更

示例：`feat(kernel): add physical memory allocator`