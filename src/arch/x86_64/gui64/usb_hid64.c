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
    uint32_t proto;                 /* 1=键盘 2=鼠标 */
    uint32_t report_len;
    uint8_t  last[8];               /* 上一帧 report，用于差分 */
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
    /* 0x28 */ 0x0A,  /* Enter */
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
    /* 0x28 */ 0x0A,
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

/* 初始化：遍历 xHCI HID 设备，配置端点。 */
void usb_hid_init(void) {
    if (g_hid_inited) return;
    g_hid_inited = 1;
    for (int i = 0; i < USB_HID_MAX; i++) g_hid[i].used = 0;

    uint32_t n = xhci_hid_device_count();
    HLOG("init: enumerating HID devices");
    for (uint32_t k = 0; k < n && k < USB_HID_MAX; k++) {
        uint32_t proto = xhci_hid_device_proto(k);
        if (proto != 1 && proto != 2) continue;
        if (xhci_hid_configure(k) != 0) {
            HLOG("configure FAILED");
            continue;
        }

        usb_hid_dev_t *h = &g_hid[k];
        h->used       = 1;
        h->xhci_idx   = k;
        h->proto      = proto;
        h->report_len = xhci_hid_device_report_len(k);
        for (int j = 0; j < 8; j++) h->last[j] = 0;
        if (proto == 1) HLOG("keyboard configured (Interrupt-IN armed)");
        else            HLOG("mouse configured (Interrupt-IN armed)");
    }
}

/* polling：被内核主循环周期调用，非阻塞取 report 并注入 GUI。 */
void usb_hid_poll(void) {
    if (!g_hid_inited) return;
    static int logged_kbd = 0, logged_mouse = 0;
    uint8_t buf[8];
    for (int i = 0; i < USB_HID_MAX; i++) {
        usb_hid_dev_t *h = &g_hid[i];
        if (!h->used) continue;
        for (int guard = 0; guard < 8; guard++) {
            int m = xhci_hid_poll(h->xhci_idx, buf, sizeof(buf));
            if (m <= 0) break;
            if (h->proto == 1) {
                if (!logged_kbd) { HLOG("first keyboard report received"); logged_kbd = 1; }
                hid_handle_keyboard(h, buf, (uint32_t)m);
            } else {
                if (!logged_mouse) { HLOG("first mouse report received"); logged_mouse = 1; }
                hid_handle_mouse(h, buf, (uint32_t)m);
            }
        }
    }
}
