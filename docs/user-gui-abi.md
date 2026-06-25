# OpenOS 用户态 GUI ABI v1（冻结兼容层）

## 冻结状态

- ABI 名称：OpenOS User GUI ABI
- ABI 版本：v1.0.0
- 冻结日期：2026-06-24
- 冻结范围：`src/kernel/include/gui_user.h` 中的用户态 GUI 请求/事件结构，以及 `src/kernel/include/syscall.h` 中现有 `SYS_GUI_*` syscall 编号。
- syscall 编号范围：`320` ~ `457`（当前冻结 `120` 个 GUI syscall，允许中间存在非 GUI syscall 空洞）。
- 兼容目标：保持现有 i386 GUI / `window_manager` / 内核内置 GUI 应用不回退。
- 定位声明：该 ABI 是当前内核 GUI 的 **v1 兼容层**，用于迁移/承载既有 PC GUI 应用；它不是 Mobile Shell 的基础 ABI。Mobile/新 PC Shell 后续应基于 A6.2 的 `display` / `input` 抽象和用户态 compositor。

## 基本原则

1. 既有 GUI syscall 编号一经冻结不得重排、复用或修改语义。
2. 既有公开结构体字段不得删除、重排或改变宽度；只能在新结构、新 syscall 或显式 v2 ABI 中扩展。
3. 用户态应用只持有 `window_id` / `widget_id` 等整数句柄，不能直接访问全局 framebuffer。
4. 所有窗口、控件、绘制和事件操作必须校验当前进程是否拥有目标窗口。
5. 所有用户指针和用户缓冲区必须由 syscall/GUI 桥接层做长度和地址校验；当前 v1 兼容层中尚未系统化的检查点必须在后续安全加固中补齐，但不得破坏 ABI。
6. 进程退出时，内核负责回收该进程创建的窗口、控件和待投递事件。

## 对象模型

### `window_id`

窗口句柄，由内核 GUI/window manager 分配。用户态只能把它作为 syscall 参数传回内核，不应假设其内存地址或内部布局。

### `widget_id`

控件句柄，归属于某个 `window_id`。所有控件操作必须同时校验窗口所有权和控件存在性。

### `gui_user_event_t`

事件队列返回的固定 v1 事件结构。冻结字段包括：

```c
typedef struct gui_user_event {
    uint32_t owner_pid;
    uint32_t type;
    uint32_t window_id;
    uint32_t widget_id;
    int32_t x;
    int32_t y;
    int32_t key;
    int32_t button;
    uint32_t modifiers;
    char text[32];
    uint32_t text_len;
    uint32_t codepoint;
    uint32_t ime_state;
} gui_user_event_t;
```

事件类型冻结为 `GUI_USER_EVENT_*`：按钮点击、按键、文本输入/变更/提交、焦点、值变更、窗口移动/缩放、鼠标移动/按下/释放/滚轮、选择变更等。

## 绘制模型

v1 采用兼容当前内核 GUI 的立即模式绘制：

1. 用户态创建窗口和控件。
2. 用户态通过 `SYS_GUI_DRAW` 提交 `gui_user_draw_request_t`。
3. `GUI_USER_DRAW_FILL_RECT`、`GUI_USER_DRAW_TEXT`、`GUI_USER_DRAW_BLIT_RGBA32`、`GUI_USER_DRAW_SCROLL`、`GUI_USER_DRAW_PRESENT` 是冻结的 v1 绘制操作。
4. 内核 GUI/window manager 负责裁剪、失效区域和最终 framebuffer 合成。

后续共享内存 surface、双缓冲、GPU/2D 加速不得改变 v1 语义；应新增 v2 syscall 或走 A6.2 display 抽象。

## 输入模型

- 鼠标事件以窗口客户区坐标投递。
- 键盘事件携带 keycode 与 `GUI_USER_KEYMOD_*` 修饰键。
- 文本输入事件携带 UTF-8 文本片段、长度和 Unicode code point。
- 焦点、移动、缩放和值变更通过事件队列投递。

## 冻结 syscall 清单

| 编号 | 名称 |
| --- | --- |
| `320` | `SYS_GUI_CREATE_WINDOW` |
| `321` | `SYS_GUI_DESTROY_WINDOW` |
| `322` | `SYS_GUI_ADD_LABEL` |
| `323` | `SYS_GUI_ADD_BUTTON` |
| `324` | `SYS_GUI_POLL_EVENT` |
| `325` | `SYS_GUI_SET_TEXT` |
| `326` | `SYS_GUI_DRAW` |
| `327` | `SYS_GUI_ADD_TEXTBOX` |
| `344` | `SYS_GUI_RESIZE_WINDOW` |
| `345` | `SYS_GUI_GET_WINDOW_INFO` |
| `346` | `SYS_GUI_GET_DISPLAY_INFO` |
| `349` | `SYS_GUI_GET_TEXT` |
| `350` | `SYS_GUI_SET_TEXT_PLACEHOLDER` |
| `351` | `SYS_GUI_SET_TEXT_FLAGS` |
| `352` | `SYS_GUI_GET_TEXT_FLAGS` |
| `353` | `SYS_GUI_SET_ICON` |
| `354` | `SYS_GUI_SET_BUTTON_FLAGS` |
| `355` | `SYS_GUI_GET_BUTTON_FLAGS` |
| `356` | `SYS_GUI_SET_LABEL_OPTIONS` |
| `357` | `SYS_GUI_GET_LABEL_OPTIONS` |
| `358` | `SYS_GUI_ADD_PANEL` |
| `359` | `SYS_GUI_SET_PANEL_OPTIONS` |
| `360` | `SYS_GUI_ADD_SLIDER` |
| `361` | `SYS_GUI_SET_SLIDER_VALUE` |
| `362` | `SYS_GUI_GET_SLIDER_VALUE` |
| `363` | `SYS_GUI_SET_SLIDER_STEP` |
| `364` | `SYS_GUI_GET_SLIDER_STEP` |
| `365` | `SYS_GUI_ADD_CANVAS` |
| `366` | `SYS_GUI_ADD_ICON_BUTTON` |
| `367` | `SYS_GUI_ADD_TOGGLE` |
| `368` | `SYS_GUI_SET_TOGGLE_CHECKED` |
| `369` | `SYS_GUI_GET_TOGGLE_CHECKED` |
| `370` | `SYS_GUI_ADD_TEXTAREA` |
| `371` | `SYS_GUI_SET_WIDGET_ENABLED` |
| `372` | `SYS_GUI_GET_WIDGET_ENABLED` |
| `373` | `SYS_GUI_MEASURE_LABEL` |
| `374` | `SYS_GUI_ADD_SCROLLBAR` |
| `375` | `SYS_GUI_SET_SCROLLBAR_VALUE` |
| `376` | `SYS_GUI_GET_SCROLLBAR_VALUE` |
| `377` | `SYS_GUI_SET_SCROLLBAR_STEP` |
| `378` | `SYS_GUI_GET_SCROLLBAR_STEP` |
| `379` | `SYS_GUI_ADD_SCROLLVIEW` |
| `380` | `SYS_GUI_SET_SCROLLVIEW_OFFSET` |
| `381` | `SYS_GUI_GET_SCROLLVIEW_OFFSET` |
| `382` | `SYS_GUI_SET_SCROLLVIEW_CONTENT_SIZE` |
| `383` | `SYS_GUI_GET_SCROLLVIEW_CONTENT_SIZE` |
| `384` | `SYS_GUI_SET_WIDGET_PARENT` |
| `385` | `SYS_GUI_ADD_CHECKBOX` |
| `386` | `SYS_GUI_SET_CHECKBOX_CHECKED` |
| `387` | `SYS_GUI_GET_CHECKBOX_CHECKED` |
| `388` | `SYS_GUI_ADD_RADIOBUTTON` |
| `389` | `SYS_GUI_SET_RADIOBUTTON_CHECKED` |
| `390` | `SYS_GUI_GET_RADIOBUTTON_CHECKED` |
| `391` | `SYS_GUI_ADD_SELECT` |
| `392` | `SYS_GUI_ADD_COMBOBOX` |
| `393` | `SYS_GUI_SET_SELECT_INDEX` |
| `394` | `SYS_GUI_GET_SELECT_INDEX` |
| `395` | `SYS_GUI_SET_SELECT_ITEMS` |
| `396` | `SYS_GUI_ADD_LISTVIEW` |
| `397` | `SYS_GUI_SET_LISTVIEW_INDEX` |
| `398` | `SYS_GUI_GET_LISTVIEW_INDEX` |
| `399` | `SYS_GUI_SET_LISTVIEW_ITEMS` |
| `400` | `SYS_GUI_ADD_TABLEVIEW` |
| `401` | `SYS_GUI_SET_TABLEVIEW_ROW` |
| `402` | `SYS_GUI_GET_TABLEVIEW_ROW` |
| `403` | `SYS_GUI_SET_TABLEVIEW_ROWS` |
| `404` | `SYS_GUI_ADD_TREEVIEW` |
| `405` | `SYS_GUI_SET_TREEVIEW_NODE` |
| `406` | `SYS_GUI_GET_TREEVIEW_NODE` |
| `407` | `SYS_GUI_SET_TREEVIEW_NODES` |
| `408` | `SYS_GUI_ADD_MENUBAR` |
| `409` | `SYS_GUI_SET_MENUBAR_ACTIVE` |
| `410` | `SYS_GUI_GET_MENUBAR_ACTIVE` |
| `411` | `SYS_GUI_SET_MENUBAR_MENUS` |
| `412` | `SYS_GUI_ADD_CONTEXTMENU` |
| `413` | `SYS_GUI_SET_CONTEXTMENU_INDEX` |
| `414` | `SYS_GUI_GET_CONTEXTMENU_INDEX` |
| `415` | `SYS_GUI_SET_CONTEXTMENU_ITEMS` |
| `416` | `SYS_GUI_SET_CONTEXTMENU_DISABLED` |
| `417` | `SYS_GUI_SHOW_CONTEXTMENU` |
| `418` | `SYS_GUI_HIDE_CONTEXTMENU` |
| `419` | `SYS_GUI_ADD_DIALOG` |
| `420` | `SYS_GUI_SET_DIALOG_MESSAGE` |
| `421` | `SYS_GUI_SHOW_DIALOG` |
| `422` | `SYS_GUI_HIDE_DIALOG` |
| `423` | `SYS_GUI_ADD_TOAST` |
| `424` | `SYS_GUI_SHOW_TOAST` |
| `425` | `SYS_GUI_HIDE_TOAST` |
| `426` | `SYS_GUI_ADD_PROGRESSBAR` |
| `427` | `SYS_GUI_SET_PROGRESSBAR_VALUE` |
| `428` | `SYS_GUI_GET_PROGRESSBAR_VALUE` |
| `429` | `SYS_GUI_SET_PROGRESSBAR_FLAGS` |
| `430` | `SYS_GUI_ADD_SPINNER` |
| `431` | `SYS_GUI_SET_SPINNER_RUNNING` |
| `432` | `SYS_GUI_SET_SPINNER_TEXT` |
| `433` | `SYS_GUI_ADD_IMAGEVIEW` |
| `434` | `SYS_GUI_SET_IMAGEVIEW_RGBA` |
| `435` | `SYS_GUI_SET_IMAGEVIEW_BITMAP` |
| `436` | `SYS_GUI_ADD_ICONVIEW` |
| `437` | `SYS_GUI_SET_ICONVIEW_ITEMS` |
| `438` | `SYS_GUI_SET_ICONVIEW_SELECTED` |
| `439` | `SYS_GUI_GET_ICONVIEW_SELECTED` |
| `440` | `SYS_GUI_ADD_TOOLBAR` |
| `441` | `SYS_GUI_SET_TOOLBAR_ITEMS` |
| `442` | `SYS_GUI_ADD_STATUSBAR` |
| `443` | `SYS_GUI_SET_STATUSBAR_TEXT` |
| `444` | `SYS_GUI_SET_STATUSBAR_FLAGS` |
| `445` | `SYS_GUI_ADD_TABVIEW` |
| `446` | `SYS_GUI_SET_TABVIEW_TABS` |
| `447` | `SYS_GUI_SET_TABVIEW_ACTIVE` |
| `448` | `SYS_GUI_GET_TABVIEW_ACTIVE` |
| `449` | `SYS_GUI_CLOSE_TABVIEW_TAB` |
| `450` | `SYS_GUI_ADD_SPLITVIEW` |
| `451` | `SYS_GUI_SET_SPLITVIEW_RATIO` |
| `452` | `SYS_GUI_GET_SPLITVIEW_RATIO` |
| `453` | `SYS_GUI_ADD_GROUPBOX` |
| `454` | `SYS_GUI_SET_GROUPBOX_OPTIONS` |
| `455` | `SYS_GUI_ADD_FORM` |
| `456` | `SYS_GUI_ADD_FORM_FIELD` |
| `457` | `SYS_GUI_ADD_FORM_SUBMIT` |

## v1 兼容层边界

### 保留能力

- 保留 i386 当前内核 GUI / window_manager 行为。
- 保留现有 `/bin/browser` 等用户态 GUI 程序依赖的 syscall 编号和结构。
- 保留内核内置 widget 集：label、button、textbox、textarea、panel、canvas、slider、scrollbar、scrollview、checkbox、radio、select、combobox、listview、tableview、treeview、menubar、contextmenu、dialog、toast、progressbar、spinner、imageview、iconview、toolbar、statusbar、tabview、splitview、groupbox、form 等。

### 禁止事项

- 禁止把 v1 GUI ABI 作为 Mobile Shell 基础。
- 禁止在 v1 中把用户态应用直接暴露给全局 framebuffer。
- 禁止重排 `SYS_GUI_*` 编号。
- 禁止通过修改 v1 结构字段顺序来“扩展”能力。

### 后续演进

- A6.2 新增 `display` / `input` 抽象，承接跨 PC/Mobile 的显示与输入机制。
- A6.3 推动 `openos-compositor`、`openos-desktop-shell`、`openos-mobile-shell` 用户态化。
- 新增 GUI 能力应优先走用户态 compositor IPC；确需内核 syscall 时使用新编号并更新 ABI 版本文档。

## Browser 迁移最小依赖

`/bin/browser` 在 v1 兼容层中可依赖：

- 窗口创建、销毁、缩放、查询显示/窗口信息。
- 文本、按钮、输入框、面板、滚动视图、图像视图等控件。
- 鼠标、键盘、文本输入和值变更事件。
- `SYS_GUI_DRAW` 和立即模式绘制。

网络、文件、TLS、时间、共享内存、mmap 等能力不属于 GUI ABI，分别由对应 syscall/库承担。

## 回归要求

每次修改 GUI ABI 相关代码后至少验证：

1. 默认构建通过。
2. 现有 i386 GUI/window_manager 行为不回退。
3. `SYS_GUI_*` 编号表未发生重排。
4. `gui_user_event_t` 与各 `gui_user_*_request_t` 的字段顺序未破坏 v1 兼容性。
