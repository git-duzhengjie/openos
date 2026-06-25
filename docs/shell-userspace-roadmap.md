# OpenOS Shell 用户态化路线（A6.3）

## 目标

OpenOS 后续 Shell 体系从“内核内置 GUI / window_manager”逐步迁移到用户态服务：

- `openos-compositor`：用户态合成器，负责 surface 管理、窗口合成、显示刷新和输入分发。
- `openos-desktop-shell`：PC 桌面 Shell，负责任务栏、桌面、窗口策略、文件管理入口和快捷键策略。
- `openos-mobile-shell`：Mobile Shell，负责全屏应用栈、手势、状态栏、通知中心、后台卡片和权限弹窗。

现有内核 GUI ABI v1 仅作为 PC/i386 兼容层保留，不作为 Mobile Shell 基础。

## 分层边界

### 内核负责机制

内核只提供跨平台基础机制：

1. 进程、线程、调度、内存隔离。
2. IPC / message queue / shared memory / eventfd 等通信机制。
3. `display` 抽象：显示设备、framebuffer/display buffer、present、mode 查询。
4. `input` 抽象：键盘、鼠标、触摸、手势原始事件队列。
5. 权限校验、设备访问控制、安全审计、资源限制。
6. 兼容层：保留现有 GUI ABI v1 和内核 window_manager 的旧路径。

### 用户态负责策略

用户态服务负责可演进策略：

1. 窗口布局、z-order、焦点、最小化/最大化/全屏策略。
2. 多显示器布局和 DPI/缩放策略。
3. 输入法、快捷键、手势识别、焦点路由。
4. 通知、权限弹窗、任务切换、后台卡片。
5. 桌面/移动外壳 UI、主题、动画、无障碍策略。
6. 应用生命周期、包管理集成和系统服务入口。

## `openos-compositor`

### 职责

- 连接内核 `display` / `input` 抽象。
- 管理应用 surface：创建、销毁、resize、damage、present。
- 维护 window/surface 树和 z-order。
- 合成最终 display buffer。
- 分发输入事件给拥有焦点或命中区域的客户端。
- 对 PC Shell 和 Mobile Shell 提供统一 compositor IPC。

### 最小 IPC 草案

| 消息 | 方向 | 说明 |
| --- | --- | --- |
| `COMPOSITOR_HELLO` | client -> compositor | 客户端握手，声明 ABI 版本与能力 |
| `SURFACE_CREATE` | client -> compositor | 创建 surface，返回 surface id |
| `SURFACE_DESTROY` | client -> compositor | 销毁 surface |
| `SURFACE_RESIZE` | client -> compositor | 请求 resize |
| `SURFACE_DAMAGE` | client -> compositor | 提交脏矩形 |
| `SURFACE_PRESENT` | client -> compositor | 请求合成/显示当前缓冲 |
| `INPUT_EVENT` | compositor -> client | 分发键盘、鼠标、触摸、文本事件 |
| `FOCUS_CHANGED` | compositor -> client | 通知焦点变化 |
| `DISPLAY_CHANGED` | compositor -> client | 通知显示模式、DPI、多屏变化 |

### 安全要求

- 每个客户端只能操作自己创建或被授权的 surface。
- compositor 不能允许普通应用直接访问全局 framebuffer。
- shared memory buffer 必须绑定进程权限、大小、像素格式和生命周期。
- 输入事件只投递给合法焦点/命中目标；全局快捷键由 Shell 策略服务决定。

## `openos-desktop-shell`

### PC Shell 能力目标

- 多窗口：普通窗口、对话框、工具窗口、全屏窗口。
- 任务栏：运行中应用列表、窗口切换、系统托盘入口。
- 桌面：壁纸、桌面图标、右键菜单。
- 文件管理：文件管理器入口、打开方式、拖放基础协议。
- 快捷键：Alt-Tab、Win/Super 菜单、全局快捷键、窗口管理快捷键。
- 多显示器：显示器布局、主显示器、窗口跨屏移动、DPI/缩放策略。
- 兼容：可承载现有 GUI ABI v1 应用，逐步迁移到 compositor IPC。

### 与内核旧 GUI 的关系

- 旧 `window_manager` 保持为兼容/调试路径。
- 新 PC Shell 默认应运行在用户态 compositor 之上。
- `/bin/browser` 等 GUI 程序短期可继续走 GUI ABI v1；迁移期通过 shim 适配 compositor IPC。

## `openos-mobile-shell`

### Mobile Shell 能力目标

- 全屏应用栈：单前台应用、返回栈、启动器。
- 手势：返回、主页、多任务、下拉通知、边缘滑动。
- 状态栏：时间、电量、网络、隐私指示器。
- 通知中心：通知列表、快速设置、通知权限入口。
- 后台卡片：最近任务、应用快照、后台生命周期策略。
- 权限弹窗：相机、麦克风、位置、通知、文件等运行时授权。
- 触摸优先：高 DPI、软键盘、输入法、旋转和安全区域适配。

### 禁止依赖

- 不依赖 GUI ABI v1 的窗口/控件模型。
- 不把 Mobile Shell 逻辑写进内核。
- 不让应用直接操作全局 framebuffer。

## 迁移阶段

### 阶段 0：冻结兼容层

- 已冻结 `docs/user-gui-abi.md` 中 GUI ABI v1。
- 旧 GUI/window_manager 不回退。

### 阶段 1：建立内核抽象

- 已新增最小 `display` / `input` 抽象。
- 后续补齐 shared memory / message queue / 权限控制。

### 阶段 2：新增用户态服务骨架

计划新增：

- `/sbin/openos-compositor`
- `/sbin/openos-desktop-shell`
- `/sbin/openos-mobile-shell`

初期可只实现启动日志、握手协议和空事件循环。

### 阶段 3：应用迁移

- 为 GUI ABI v1 应用提供 compositor shim。
- 新应用优先使用 compositor IPC。
- Browser/文件管理器/设置等系统应用逐步迁移。

### 阶段 4：策略服务化

- `displayd` / `inputd` / `notificationd` / `permissiond` 与 Shell 联动。
- Shell 只负责 UI 与交互策略，权限、通知、设备状态由系统服务提供。

## 回归要求

每次推进 Shell 用户态化后至少验证：

1. 默认构建通过。
2. 旧 GUI ABI v1 文档与 syscall 编号不被破坏。
3. 旧 i386 GUI/window_manager 兼容路径不回退。
4. 新 display/input 抽象不直接改变现有 framebuffer 和输入驱动行为。
5. Mobile Shell 不依赖内核 GUI ABI v1。
