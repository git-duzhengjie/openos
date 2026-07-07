/**
 * 开机锁屏 —— 单用户密码验证门闸（全屏实现）
 *
 * 见 include/lockscreen.h 的设计说明。核心流程：
 *   1. 全屏绘制锁屏背景 + 中央密码输入区（不走窗口系统）。
 *   2. 自己消费 gui_event_pop() 的键盘事件（字符 / Backspace / Enter）。
 *   3. 回车 → SHA256(salt + 输入) → 与硬编码摘要常量时间比较。
 *   4. 命中则返回（放行进桌面）；否则清空输入并提示错误。
 *
 * 锁屏是桌面启动前的门闸：整屏遮罩，用户看不到也点不到桌面，
 * 直到密码正确才 return。
 */
#include "lockscreen.h"
#include "gui.h"
#include "sha256.h"
#include "framebuffer.h"
#include "string.h"
#include "types.h"

/* early_console64_write：把锁屏日志走串口，便于无显环境调试 */
extern void early_console64_write(const char *s);
/* gui_event_pop：从全局事件队列取一个事件（键盘/鼠标）。返回 1 表示取到 */
extern int gui_event_pop(gui_event_t *out);
/* gui_set_lockscreen_capture：锁屏门闸，置 1 时强制捕获所有按键进事件队列 */
extern void gui_set_lockscreen_capture(int on);
/* usb_hid_poll：轮询 USB HID 设备（键盘/鼠标）的中断环，把 report 注入事件队列。
 * 锁屏阻塞了主循环，必须在此手动驱动，否则 QEMU usb-kbd 环境下收不到任何按键。 */
extern void usb_hid_poll(void);

/* ------------------------------------------------------------------
 * 密码契约（编译期硬编码）
 *
 * salt   = "openos-lock-v1:"
 * 明文   = "openos"（默认开机密码）
 * digest = SHA256("openos-lock-v1:openos")
 *        = b4962b268df5206f2577d2accf79ae52153970729f518972a2a6ad1983b8219e
 *
 * 修改密码：重算 SHA256(salt + 新密码) 并替换 LOCK_HASH 即可，
 * 明文永不出现在二进制里。
 * ------------------------------------------------------------------ */
static const char LOCK_SALT[] = "openos-lock-v1:";

static const uint8_t LOCK_HASH[SHA256_DIGEST_SIZE] = {
    0xb4, 0x96, 0x2b, 0x26, 0x8d, 0xf5, 0x20, 0x6f,
    0x25, 0x77, 0xd2, 0xac, 0xcf, 0x79, 0xae, 0x52,
    0x15, 0x39, 0x70, 0x72, 0x9f, 0x51, 0x89, 0x72,
    0xa2, 0xa6, 0xad, 0x19, 0x83, 0xb8, 0x21, 0x9e
};

/* 输入缓冲（最长 63 字符） */
#define LOCK_PW_MAX 63
static char    g_lock_pw[LOCK_PW_MAX + 1];
static int     g_lock_pw_len = 0;
static int     g_lock_wrong  = 0;   /* 是否显示"密码错误"提示 */

/* 配色（0x00RRGGBB） */
#define LOCK_BG_TOP     0x0F2027u   /* 背景 */
#define LOCK_BG_BOTTOM  0x203A43u
#define LOCK_PANEL      0x1B2B34u   /* 中央面板 */
#define LOCK_PANEL_BDR  0x3A99D8u   /* 面板边框（青蓝） */
#define LOCK_BOX_BG     0x0E1A20u   /* 输入框底 */
#define LOCK_BOX_BDR    0x4AA3DFu   /* 输入框边框 */
#define LOCK_TEXT       0xE8F1F2u   /* 主文字 */
#define LOCK_HINT       0x9AB3BBu   /* 次要文字 */
#define LOCK_ERR        0xFF6B6Bu   /* 错误提示 */
#define LOCK_DOT        0xE8F1F2u   /* 密码掩码点 */

/* strlen 的本地实现 */
static uint32_t lock_strlen(const char *s)
{
    uint32_t n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

/* 校验输入是否为正确密码：SHA256(salt + input) == LOCK_HASH */
static int lock_verify(const char *input)
{
    sha256_ctx_t ctx;
    uint8_t digest[SHA256_DIGEST_SIZE];
    int ok;

    sha256_init(&ctx);
    sha256_update(&ctx, (const uint8_t *)LOCK_SALT, lock_strlen(LOCK_SALT));
    sha256_update(&ctx, (const uint8_t *)input, lock_strlen(input));
    sha256_final(&ctx, digest);

    ok = sha256_ct_equal(digest, LOCK_HASH);
    memset(digest, 0, sizeof(digest));
    return ok;
}

/* 全屏绘制一帧锁屏画面 */
static void lock_render(void)
{
    int sw = gui_screen_width();
    int sh = gui_screen_height();
    int i;

    /* 面板尺寸 */
    int pw = 460;
    int ph = 240;
    int px = (sw - pw) / 2;
    int py = (sh - ph) / 2;

    /* 输入框 */
    int bw = pw - 80;
    int bh = 44;
    int bx = px + 40;
    int by = py + 110;

    if (sw <= 0 || sh <= 0) { sw = 1280; sh = 800; }

    /* 1) 背景竖向渐变 */
    for (i = 0; i < sh; i++) {
        uint32_t t  = (uint32_t)i * 255 / (uint32_t)(sh > 1 ? sh : 1);
        uint32_t r0 = (LOCK_BG_TOP >> 16) & 0xFF, r1 = (LOCK_BG_BOTTOM >> 16) & 0xFF;
        uint32_t g0 = (LOCK_BG_TOP >> 8)  & 0xFF, g1 = (LOCK_BG_BOTTOM >> 8)  & 0xFF;
        uint32_t b0 =  LOCK_BG_TOP        & 0xFF, b1 =  LOCK_BG_BOTTOM        & 0xFF;
        uint32_t r  = r0 + (r1 - r0) * t / 255;
        uint32_t g  = g0 + (g1 - g0) * t / 255;
        uint32_t b  = b0 + (b1 - b0) * t / 255;
        gui_screen_fill_rect(0, i, sw, 1, (r << 16) | (g << 8) | b);
    }

    /* 2) 中央面板 + 边框 */
    gui_screen_fill_rect(px, py, pw, ph, LOCK_PANEL);
    gui_screen_draw_border(px, py, pw, ph, 2, LOCK_PANEL_BDR);

    /* 3) 标题 */
    gui_draw_text(px + 40, py + 30, "OpenOS  \xe7\xb3\xbb\xe7\xbb\x9f\xe5\xb7\xb2\xe9\x94\x81\xe5\xae\x9a", LOCK_TEXT);
    gui_draw_text(px + 40, py + 60, "Enter password to unlock", LOCK_HINT);

    /* 4) 输入框 */
    gui_screen_fill_rect(bx, by, bw, bh, LOCK_BOX_BG);
    gui_screen_draw_border(bx, by, bw, bh, 2, LOCK_BOX_BDR);

    /* 5) 密码掩码点 */
    {
        int dot_r = 4;
        int gap   = 18;
        int dx    = bx + 16;
        int dy    = by + bh / 2 - dot_r;
        for (i = 0; i < g_lock_pw_len && i < LOCK_PW_MAX; i++) {
            gui_screen_fill_rect(dx, dy, dot_r * 2, dot_r * 2, LOCK_DOT);
            dx += gap;
        }
        /* 空输入时显示占位提示 */
        if (g_lock_pw_len == 0) {
            gui_draw_text(bx + 14, by + bh / 2 - 8, "password", LOCK_HINT);
        }
    }

    /* 6) 提示行 */
    if (g_lock_wrong) {
        gui_draw_text(bx, by + bh + 16, "Wrong password, try again", LOCK_ERR);
    } else {
        gui_draw_text(bx, by + bh + 16, "Default password: openos   (Enter to unlock)", LOCK_HINT);
    }

    /* 7) present */
    gui_screen_present();
}

void lockscreen_run(void)
{
    gui_event_t ev;
    int need_redraw = 1;

    g_lock_pw_len = 0;
    g_lock_wrong  = 0;
    g_lock_pw[0]  = '\0';

    early_console64_write("[lockscreen] starting (fullscreen), waiting for password\n");

    /* 开启锁屏输入门闸：否则没有焦点 widget，键盘中断会在 capture 判定时被丢弃 */
    gui_set_lockscreen_capture(1);

    for (;;) {
        if (need_redraw) {
            lock_render();
            need_redraw = 0;
        }

        /* 关键：锁屏阻塞了内核主循环，主循环里的 usb_hid_poll() 不会再跑。
         * QEMU GUI 模式挂 usb-kbd 后，键盘走 USB 轮询而非 PS/2 中断，
         * 必须在此手动轮询，否则 USB 键盘的 report 永远无人消费，事件队列恒空，
         * 表现为“锁屏界面完全无法输入密码”。 */
        usb_hid_poll();

        /* 消费所有排队事件 */
        while (gui_event_pop(&ev)) {
            if (ev.type != GUI_EVENT_KEY_DOWN) {
                continue;   /* 锁屏阶段忽略鼠标等其它事件 */
            }

            {
                int k = ev.key;

                if (k == GUI_KEY_ENTER || k == '\n' || k == '\r') {
                    /* 回车：校验（主键盘 Enter 经 scancode 表映射为 0x0A，小键盘 Enter 为 GUI_KEY_ENTER=13，两者都接受） */
                    g_lock_pw[g_lock_pw_len] = '\0';
                    if (lock_verify(g_lock_pw)) {
                        early_console64_write("[lockscreen] password OK, unlocking\n");
                        /* 抹掉明文残留 */
                        memset(g_lock_pw, 0, sizeof(g_lock_pw));
                        g_lock_pw_len = 0;
                        gui_set_lockscreen_capture(0);   /* 关闭门闸，交回焦点给桌面 */
                        return;
                    }
                    early_console64_write("[lockscreen] wrong password\n");
                    g_lock_wrong  = 1;
                    g_lock_pw_len = 0;
                    g_lock_pw[0]  = '\0';
                    need_redraw   = 1;
                } else if (k == GUI_KEY_BACKSPACE) {
                    /* 退格 */
                    if (g_lock_pw_len > 0) {
                        g_lock_pw_len--;
                        g_lock_pw[g_lock_pw_len] = '\0';
                        need_redraw = 1;
                    }
                } else if (k >= 0x20 && k < 0x7F) {
                    /* 可见字符 */
                    if (g_lock_pw_len < LOCK_PW_MAX) {
                        g_lock_pw[g_lock_pw_len++] = (char)k;
                        g_lock_pw[g_lock_pw_len] = '\0';
                        g_lock_wrong = 0;
                        need_redraw  = 1;
                    }
                }
            }
        }

        /* 不能用 hlt 睡死：USB HID 是轮询式（无中断唤醒），hlt 后 CPU 停机会
         * 导致 usb_hid_poll() 无法及时轮询，按键丢失或极度迟钝。
         * 改用 pause 保持忙轮询，既降低总线功耗又不阻断 USB 轮询。 */
        __asm__ __volatile__("pause");
    }
}
