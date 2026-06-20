# OpenOS 用户态 GUI 应用 ABI 草案

## 目标

该 ABI 用于把当前内核内置 GUI 应用逐步迁移到用户态进程，优先目标是 `/bin/browser`。迁移后，浏览器、文件管理器等复杂应用的崩溃不应导致内核崩溃，只应表现为对应进程退出或窗口关闭。

## 基本原则

1. GUI 应用运行在用户态，通过 syscall 或消息队列请求窗口管理服务。
2. 内核/窗口管理器负责窗口生命周期、裁剪、合成、焦点和输入事件分发。
3. 用户态应用只获得自己的窗口句柄和绘制缓冲，不能直接访问全局 framebuffer。
4. ABI 必须可版本化，便于后续扩展控件、剪贴板、输入法和 GPU/加速路径。
5. 所有用户指针必须在内核侧校验，防止 GUI syscall 破坏内核地址空间。

## 对象模型

### gui_window_id

窗口句柄，由内核分配，用户态仅保存整数 ID。

### gui_surface_id

可选的绘制 surface 句柄。第一阶段可以直接使用窗口后备缓冲；后续可扩展多 surface、双缓冲或共享内存。

### gui_event

事件结构用于输入和窗口状态通知。

```c
typedef enum openos_gui_event_type {
    OPENOS_GUI_EVENT_NONE = 0,
    OPENOS_GUI_EVENT_CLOSE,
    OPENOS_GUI_EVENT_RESIZE,
    OPENOS_GUI_EVENT_MOUSE_MOVE,
    OPENOS_GUI_EVENT_MOUSE_DOWN,
    OPENOS_GUI_EVENT_MOUSE_UP,
    OPENOS_GUI_EVENT_MOUSE_WHEEL,
    OPENOS_GUI_EVENT_KEY_DOWN,
    OPENOS_GUI_EVENT_KEY_UP,
    OPENOS_GUI_EVENT_TEXT_INPUT,
    OPENOS_GUI_EVENT_TIMER,
    OPENOS_GUI_EVENT_FOCUS,
    OPENOS_GUI_EVENT_BLUR,
} openos_gui_event_type_t;

typedef struct openos_gui_event {
    uint32_t type;
    uint32_t window_id;
    int32_t x;
    int32_t y;
    int32_t button;
    int32_t key;
    uint32_t modifiers;
    uint32_t unicode;
    uint32_t width;
    uint32_t height;
    uint64_t timestamp_ms;
} openos_gui_event_t;
```

## 第一阶段 syscall 草案

| syscall | 作用 |
| --- | --- |
| `gui_create_window(title, x, y, w, h, flags)` | 创建窗口，返回 window_id |
| `gui_destroy_window(window_id)` | 销毁窗口 |
| `gui_set_title(window_id, title)` | 修改标题 |
| `gui_show_window(window_id)` | 显示窗口 |
| `gui_invalidate(window_id, x, y, w, h)` | 标记区域需要重绘 |
| `gui_poll_event(event*, timeout_ms)` | 获取 GUI 事件 |
| `gui_create_timer(window_id, interval_ms, flags)` | 创建定时器 |
| `gui_cancel_timer(timer_id)` | 取消定时器 |
| `gui_draw_rect(window_id, x, y, w, h, color)` | 填充矩形 |
| `gui_draw_text(window_id, x, y, color, text)` | 绘制 UTF-8 文本 |
| `gui_blit(window_id, x, y, w, h, pixels, stride)` | 位图 blit |
| `gui_present(window_id)` | 提交绘制结果 |

## 绘制模型

第一阶段采用简单立即模式 syscall，降低实现复杂度：

1. 应用收到 resize / expose / timer / 输入事件后重绘窗口。
2. 应用调用 draw_rect / draw_text / blit 填充内容。
3. 应用调用 present 提交。
4. 窗口管理器负责裁剪与合成。

第二阶段可切换为共享内存双缓冲：

1. `gui_map_surface(window_id)` 映射窗口后备缓冲。
2. 用户态直接写入 surface。
3. `gui_present_rect(window_id, rect)` 提交脏区。

## 输入模型

- 鼠标事件以窗口客户区坐标传递。
- 键盘事件传递 keycode、modifiers。
- 文本输入单独以 Unicode code point 传递，避免应用自行处理复杂键盘布局。
- 焦点变化通过 focus / blur 通知。

## Browser 迁移所需最小 API

`/bin/browser` 至少需要：

- 创建窗口、设置标题、销毁窗口。
- 绘制按钮、输入框、文本内容、状态栏。
- 鼠标点击、键盘输入、定时器 tick。
- UTF-8 文本测量和绘制。
- 网络通过用户态 socket/libc API 完成。

## 安全与健壮性

- 每个 GUI syscall 都必须检查 window_id 是否属于当前进程。
- 所有用户缓冲区必须做地址范围和长度校验。
- 单个进程的绘制频率应有限流，避免恶意应用拖垮桌面。
- 进程退出时内核自动销毁其窗口、surface 和 timer。
- 浏览器崩溃时，窗口管理器收到进程退出通知并关闭对应窗口。

## 后续扩展

- 控件库：按钮、输入框、滚动条、列表、菜单。
- 剪贴板：文本复制/粘贴。
- 输入法：组合文本、候选词窗口。
- 字体服务：字体枚举、fallback、文本测量。
- 图像解码服务或用户态库。
- GPU/2D 加速抽象。
