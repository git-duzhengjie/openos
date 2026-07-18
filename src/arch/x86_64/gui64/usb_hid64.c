/* ============================================================
 * usb_hid64.c —— USB HID Boot Protocol 报文解析 + GUI 输入注入
 *
 * M2.3 Step3-4：位于 xHCI 传输原语（xhci64.c）之上的平台层。
 * 职责：
 *   - 枚举 xHCI 已识别的 HID 设备（键盘 proto=1 / 鼠标 proto=2）
 *   - 配置各设备 Interrupt-IN 端点
 *   - polling 驱动：周期性取 HID boot report → 解析 → 注入 GUI
 *
 * 复用现有 GUI 输入通路（与 PS/2 键鼠一致）：
 *   键盘 → gui_post_key_code_with_modifiers(ascii, mods)
 *   鼠标 → mouse_inject_relative(dx, dy, buttons, wheel)
 *
 * boot 键盘 report（8 字节）：
 *   [0] modifier bitmap  [1] reserved  [2..7] 最多 6 个 HID Usage ID
 * boot 鼠标 report（3~4 字节）：
 *   [0] button bitmap（bit0 左 bit1 右 bit2 中）
 *   [1] dx (int8)  [2] dy (int8)  [3] wheel (int8, 可选)
 * ============================================================ */

#include "types.h"
#include "xhci64.h"
#include "mouse.h"
#include "../../kernel/include/gesture.h"
#include "../../kernel/include/hid_type_infer.h"

/* OPENOS_TOUCH_TEST 编译宏 → 运行时布尔（供 hid_type_infer 使用） */
#ifdef OPENOS_TOUCH_TEST
#define HID_FORCE_TOUCH_TEST 1
#else
#define HID_FORCE_TOUCH_TEST 0
#endif

/* 内核时基（毫秒），供手势状态机使用 */
extern uint64_t arch_x86_64_tsc_uptime_ms(void);

#define USB_HID_MAX      XHCI_MAX_DEVS

/* 串口日志（复用 early 串口通路） */
extern void early_serial64_write(const char *s);
#define HLOG(s)  early_serial64_write("[usb-hid] " s "\n")

/* GUI modifier bits（与 keyboard64.c / gui_user.h 保持一致） */
#define KBD_MOD_SHIFT 1u
#define KBD_MOD_CTRL  2u
#define KBD_MOD_ALT   4u
#define KBD_MOD_META  8u

/* HID keyboard modifier bitmap（report[0]）位定义 */
#define HID_MOD_LCTRL   0x01
#define HID_MOD_LSHIFT  0x02
#define HID_MOD_LALT    0x04
#define HID_MOD_LMETA   0x08
#define HID_MOD_RCTRL   0x10
#define HID_MOD_RSHIFT  0x20
#define HID_MOD_RALT    0x40
#define HID_MOD_RMETA   0x80

/* GUI 键盘上报接口（keyboard64.c 同款） */
extern void gui_post_key_code_with_modifiers(int key, uint32_t modifiers);
extern int  gui_should_capture_key_code_with_modifiers(int key, uint32_t modifiers);

typedef struct {
    int      used;
    uint32_t xhci_idx;              /* 对应 xhci_hid_device 序号 */
    uint32_t proto;                 /* 1=键盘 2=鼠标 0=其他 */
    uint32_t hid_type;              /* xhci_hid_type_t（M8-A 新增，细分 tablet/touchscreen）*/
    uint32_t report_len;
    /* 触屏专用状态（跟踪 Tip 按下弹起边沿）*/
    uint8_t  ts_prev_tip;           /* 上一帧 Tip Switch（接触与否）*/
    int      ts_last_x, ts_last_y;  /* Tip Up 后保持的坐标 */
    uint8_t  last[8];               /* 上一帧 report（键盘差分用）*/
} usb_hid_dev_t;

static usb_hid_dev_t g_hid[USB_HID_MAX];
static int           g_hid_inited = 0;

/* ---- HID Usage ID (boot keyboard) -> ASCII 映射 ----
 * 覆盖 0x04..0x38 常用键；未覆盖返回 0（不上报）。 */
static const char hid_usage_ascii[] = {
    /* 0x00 */ 0,0,0,0,
    /* 0x04 a-z */ 'a','b','c','d','e','f','g','h','i','j','k','l','m',
                   'n','o','p','q','r','s','t','u','v','w','x','y','z',
    /* 0x1E 1-0 */ '1','2','3','4','5','6','7','8','9','0',
    /* 0x28 */ 13,    /* Enter (GUI_KEY_ENTER) */
    /* 0x29 */ 0x1B,  /* Esc */
    /* 0x2A */ 0x08,  /* Backspace */
    /* 0x2B */ 0x09,  /* Tab */
    /* 0x2C */ ' ',   /* Space */
    /* 0x2D */ '-',
    /* 0x2E */ '=',
    /* 0x2F */ '[',
    /* 0x30 */ ']',
    /* 0x31 */ '\\',
    /* 0x32 */ 0,     /* non-US # */
    /* 0x33 */ ';',
    /* 0x34 */ '\'',
    /* 0x35 */ '`',
    /* 0x36 */ ',',
    /* 0x37 */ '.',
    /* 0x38 */ '/'
};
#define HID_USAGE_MAX (int)(sizeof(hid_usage_ascii))

static const char hid_usage_ascii_shift[] = {
    /* 0x00 */ 0,0,0,0,
    /* 0x04 A-Z */ 'A','B','C','D','E','F','G','H','I','J','K','L','M',
                   'N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
    /* 0x1E !-) */ '!','@','#','$','%','^','&','*','(',')',
    /* 0x28 */ 13,
    /* 0x29 */ 0x1B,
    /* 0x2A */ 0x08,
    /* 0x2B */ 0x09,
    /* 0x2C */ ' ',
    /* 0x2D */ '_',
    /* 0x2E */ '+',
    /* 0x2F */ '{',
    /* 0x30 */ '}',
    /* 0x31 */ '|',
    /* 0x32 */ 0,
    /* 0x33 */ ':',
    /* 0x34 */ '"',
    /* 0x35 */ '~',
    /* 0x36 */ '<',
    /* 0x37 */ '>',
    /* 0x38 */ '?'
};

/* HID modifier bitmap → GUI modifiers */
static uint32_t hid_mods_to_gui(uint8_t m) {
    uint32_t g = 0;
    if (m & (HID_MOD_LSHIFT | HID_MOD_RSHIFT)) g |= KBD_MOD_SHIFT;
    if (m & (HID_MOD_LCTRL  | HID_MOD_RCTRL))  g |= KBD_MOD_CTRL;
    if (m & (HID_MOD_LALT   | HID_MOD_RALT))   g |= KBD_MOD_ALT;
    if (m & (HID_MOD_LMETA  | HID_MOD_RMETA))  g |= KBD_MOD_META;
    return g;
}

/* HID Usage ID → ASCII（考虑 shift） */
static int hid_usage_to_ascii(uint8_t usage, int shift) {
    if ((int)usage >= HID_USAGE_MAX) return 0;
    char c = shift ? hid_usage_ascii_shift[usage] : hid_usage_ascii[usage];
    return (int)(unsigned char)c;
}

static int hid_kbd_contains(const uint8_t *rpt, uint8_t code) {
    for (int i = 2; i < 8; i++)
        if (rpt[i] == code) return 1;
    return 0;
}

/* 键盘 report：与上一帧差分，仅对新按下的键产生 GUI 按键。 */
static void hid_handle_keyboard(usb_hid_dev_t *h, const uint8_t *rpt, uint32_t len) {
    if (len < 8) return;
    uint32_t gmods = hid_mods_to_gui(rpt[0]);
    int shift = (rpt[0] & (HID_MOD_LSHIFT | HID_MOD_RSHIFT)) ? 1 : 0;

    for (int i = 2; i < 8; i++) {
        uint8_t code = rpt[i];
        if (code <= 1) continue;                 /* 0=空 1=ErrorRollOver */
        if (hid_kbd_contains(h->last, code)) continue;  /* 已按住，非新事件 */
        int ascii = hid_usage_to_ascii(code, shift);
        if (ascii == 0) continue;
        if (gui_should_capture_key_code_with_modifiers(ascii, gmods))
            gui_post_key_code_with_modifiers(ascii, gmods);
    }
    for (int i = 0; i < 8; i++) h->last[i] = rpt[i];
}

/* 鼠标 report：相对移动 + 按钮 + 滚轮，注入 g_mouse。 */
static void hid_handle_mouse(usb_hid_dev_t *h, const uint8_t *rpt, uint32_t len) {
    if (len < 3) return;
    uint8_t btn   = rpt[0] & 0x07;
    int dx        = (int)(signed char)rpt[1];
    int dy        = (int)(signed char)rpt[2];
    int wheel     = (len >= 4) ? (int)(signed char)rpt[3] : 0;
    mouse_inject_relative(dx, dy, btn, wheel);
    (void)h;
}

/* Tablet report：绝对坐标（0..0x7FFF）+ 按钮 + 滚轮。QEMU usb-tablet 固定布局（6字节）：
 * [0]=buttons  [1]=X_lo  [2]=X_hi  [3]=Y_lo  [4]=Y_hi  [5]=wheel
 * 需要把 tablet 的 0..0x7FFF 范围缩放到当前屏幕分辨率（由 GUI 通过 mouse_set_bounds 维护）。
 */
static void hid_handle_tablet(usb_hid_dev_t *h, const uint8_t *rpt, uint32_t len) {
    if (len < 5) return;
    uint8_t btn = rpt[0] & 0x07;
    int raw_x   = (int)rpt[1] | ((int)rpt[2] << 8);  /* 0..0x7FFF */
    int raw_y   = (int)rpt[3] | ((int)rpt[4] << 8);
    int wheel   = (len >= 6) ? (int)(signed char)rpt[5] : 0;

    /* 从鼠标状态里取当前屏幕尺寸并缩放。QEMU tablet 的坐标范围是 0..0x7FFF。 */
    mouse_state_t *ms = mouse_get_state();
    int max_x = ms->max_x > 0 ? ms->max_x : 639;
    int max_y = ms->max_y > 0 ? ms->max_y : 479;
    int sx = (raw_x * max_x) / 0x7FFF;
    int sy = (raw_y * max_y) / 0x7FFF;
    mouse_set_absolute_position_with_wheel(sx, sy, btn, wheel);
    (void)h;
}

/* ---- M8-A：单点触屏 report 解析 ----
 * 触屏与 tablet 的区别：
 *   - tablet：无 tip switch，hover 时也上报坐标，button 下→左键
 *   - touchscreen：必须 Tip Switch 为 1 才存在接触；Tip Up 后坐标保持（不回零）
 *
 * QEMU usb-tablet 可以当单点触屏用：把 rpt[0].bit0（左键）当作 Tip Switch。
 * 真机 HID Touchscreen 同样使用 6 字节布局作为 fallback（M8-C 将改为 HID Descriptor 驱动）。
 */
static void hid_handle_touchscreen(usb_hid_dev_t *h, const uint8_t *rpt, uint32_t len) {
    if (len < 5) return;
    uint8_t tip = rpt[0] & 0x01;                      /* Tip Switch */
    int raw_x   = (int)rpt[1] | ((int)rpt[2] << 8);
    int raw_y   = (int)rpt[3] | ((int)rpt[4] << 8);

    mouse_state_t *ms = mouse_get_state();
    int max_x = ms->max_x > 0 ? ms->max_x : 639;
    int max_y = ms->max_y > 0 ? ms->max_y : 479;
    int sx, sy;
    if (tip) {
        sx = (raw_x * max_x) / 0x7FFF;
        sy = (raw_y * max_y) / 0x7FFF;
        h->ts_last_x = sx;
        h->ts_last_y = sy;
    } else {
        /* Tip Up：保持上次坐标，避免光标回到 (0,0) */
        sx = h->ts_last_x;
        sy = h->ts_last_y;
    }

    /* Tip 状态→鼠标左键（方案：触屏单点直接映射为左键） */
    uint8_t btn = tip ? 0x01 : 0x00;
    mouse_set_absolute_position_with_wheel(sx, sy, btn, 0);

    /* M8-B：把触屏帧灌入手势状态机（不影响上面的鼠标注入，向下兼容） */
    {
        touch_frame_t tf;
        tf.x      = sx;
        tf.y      = sy;
        tf.tip    = tip;
        tf.now_ms = (uint32_t)arch_x86_64_tsc_uptime_ms();
        gesture_feed(&tf);
    }

    h->ts_prev_tip = tip;
}

/* 初始化：遍历 xHCI HID 设备，推断类型并配置端点。M8-A：
 *   - boot 键盘/鼠标（proto 1/2）：沿用旧逻辑
 *   - QEMU tablet（VID=0627 PID=0001，proto=0）：HID_TYPE_TABLET
 *   - 其他 HID（proto=0，class=3，HID subclass）：默认作为 HID_TYPE_TOUCHSCREEN 尝试（真机触屏兜底）
 */
void usb_hid_init(void) {
    if (g_hid_inited) return;
    g_hid_inited = 1;
    for (int i = 0; i < USB_HID_MAX; i++) {
        g_hid[i].used = 0;
        g_hid[i].ts_prev_tip = 0;
        g_hid[i].ts_last_x = 320; g_hid[i].ts_last_y = 240;
    }

    uint32_t n = xhci_hid_device_count();
    HLOG("init: enumerating HID devices");
    for (uint32_t k = 0; k < n && k < USB_HID_MAX; k++) {
        uint32_t proto = xhci_hid_device_proto(k);
        uint32_t vid   = xhci_hid_device_vid(k);
        uint32_t pid   = xhci_hid_device_pid(k);

        /* M8-A.1：优先尝试用 Report Descriptor 判定；
         *   xhci 目前未导出 report descriptor，desc/len 传 (NULL,0) 即可，
         *   由 hid_type_infer 自动 fall-through 到 VID 白名单 → tablet → 兜底触屏。
         *   一旦后续把 desc 读入内存并加 getter，只需在此传入即可无缝启用。 */
        hid_infer_type_t inferred = hid_type_infer(
            (uint16_t)vid, (uint16_t)pid, (uint8_t)proto,
            /* desc */ (const uint8_t *)0, /* desc_len */ 0,
            HID_FORCE_TOUCH_TEST);

        /* 枚举值兼容：hid_infer_type_t 与 xhci_hid_type_t 前 5 项一一对应 */
        uint32_t type = (uint32_t)inferred;
        if (type == XHCI_HID_TYPE_UNKNOWN) {
            HLOG("skip: unknown HID (proto not boot/subclass mismatch)");
            continue;
        }

        if (xhci_hid_configure(k) != 0) {
            HLOG("configure FAILED");
            continue;
        }

        usb_hid_dev_t *h = &g_hid[k];
        h->used       = 1;
        h->xhci_idx   = k;
        h->proto      = proto;
        h->hid_type   = type;
        h->report_len = xhci_hid_device_report_len(k);
        for (int j = 0; j < 8; j++) h->last[j] = 0;
        xhci_hid_device_set_type(k, (xhci_hid_type_t)type);

        if (type == XHCI_HID_TYPE_BOOT_KEYBD) {
            HLOG("keyboard configured (Interrupt-IN armed)");
        } else if (type == XHCI_HID_TYPE_TABLET) {
            HLOG("tablet configured (absolute coords, Interrupt-IN armed)");
        } else if (type == XHCI_HID_TYPE_TOUCHSCREEN) {
            HLOG("touchscreen configured (tip-switch, Interrupt-IN armed)");
        } else {
            HLOG("mouse configured (Interrupt-IN armed)");
        }
    }
}

/* polling：被内核主循环周期调用，非阻塞取 report 并注入 GUI。 */
void usb_hid_poll(void) {
    if (!g_hid_inited) return;
    static int logged_kbd = 0, logged_ptr = 0;
    uint8_t buf[8];
    for (int i = 0; i < USB_HID_MAX; i++) {
        usb_hid_dev_t *h = &g_hid[i];
        if (!h->used) continue;
        for (int guard = 0; guard < 8; guard++) {
            int m = xhci_hid_poll(h->xhci_idx, buf, sizeof(buf));
            if (m <= 0) break;
            switch (h->hid_type) {
            case XHCI_HID_TYPE_BOOT_KEYBD:
                if (!logged_kbd) { HLOG("first keyboard report received"); logged_kbd = 1; }
                hid_handle_keyboard(h, buf, (uint32_t)m);
                break;
            case XHCI_HID_TYPE_TABLET:
                if (!logged_ptr) { HLOG("first tablet report received"); logged_ptr = 1; }
                hid_handle_tablet(h, buf, (uint32_t)m);
                break;
            case XHCI_HID_TYPE_TOUCHSCREEN:
                if (!logged_ptr) { HLOG("first touchscreen report received"); logged_ptr = 1; }
                hid_handle_touchscreen(h, buf, (uint32_t)m);
                break;
            case XHCI_HID_TYPE_BOOT_MOUSE:
            default:
                if (!logged_ptr) { HLOG("first mouse report received"); logged_ptr = 1; }
                hid_handle_mouse(h, buf, (uint32_t)m);
                break;
            }
        }
    }
}
