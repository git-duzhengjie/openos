/**
 * 开机锁屏 —— 多用户密码验证门闸（全屏实现，M6.11.5 接线）
 *
 * 见 include/lockscreen.h 的设计说明。核心流程：
 *   1. 全屏绘制锁屏背景 + 中央"用户名 + 密码"双输入区（不走窗口系统）。
 *   2. 自己消费 gui_event_pop() 的键盘事件（字符 / Backspace / Enter / Tab）。
 *   3. Tab 在用户名/密码框间切换焦点；回车触发校验。
 *   4. 校验走 arch_x86_64_login_authenticate()：/etc/passwd 查账户 +
 *      /etc/shadow 的 SHA256(password) 与 sha256$<hex> 常量时间比对。
 *      —— 与 login(1) / SYS_LOGIN 走同一套凭证后端，真正支持多用户解锁。
 *   5. 命中则返回（放行进桌面）；否则清空密码并提示错误。
 *
 * 注意：锁屏是"门闸放行"语义，只认证不降权（不 setsid/setuid），
 * 避免污染内核 slot-0 PCB 的凭证。降权由后续真正的用户会话负责。
 *
 * 锁屏是桌面启动前的门闸：整屏遮罩，用户看不到也点不到桌面，
 * 直到密码正确才 return。默认账户 openos / openos。
 */
#include "lockscreen.h"
#include "gui.h"
#include "framebuffer.h"
#include "string.h"
#include "types.h"
#include "login64.h"

/* early_console64_write：把锁屏日志走串口，便于无显环境调试 */
extern void early_console64_write(const char *s);
/* gui_event_pop：从全局事件队列取一个事件（键盘/鼠标）。返回 1 表示取到 */
extern int gui_event_pop(gui_event_t *out);
/* gui_set_lockscreen_capture：锁屏门闸，置 1 时强制捕获所有按键进事件队列 */
extern void gui_set_lockscreen_capture(int on);
/* usb_hid_poll：轮询 USB HID 设备（键盘/鼠标）的中断环，把 report 注入事件队列。
 * 锁屏阻塞了主循环，必须在此手动驱动，否则 QEMU usb-kbd 环境下收不到任何按键。 */
extern void usb_hid_poll(void);
/* arch_x86_64_delay_ms：TSC 校准的毫秒级延时，用于锁屏轮询节流。
 * 见根因说明：本系统无 PIT 100Hz 周期中断，hlt 会一睡不醒，
 * 故改用 sti + 短延时的限速轮询替代 sti;hlt。 */
extern void arch_x86_64_delay_ms(unsigned int ms);

/* ------------------------------------------------------------------
 * 凭证后端（M6.11.5）
 *
 * 不再硬编码单一密码摘要：校验直接调 arch_x86_64_login_authenticate()，
 * 该函数查 /etc/passwd 定位账户、取 /etc/shadow 的 sha256$<hex>，
 * 重算 SHA256(输入密码) 后常量时间比对。与 login(1)/SYS_LOGIN 同源。
 *
 * 锁屏只认证不降权，避免污染内核 slot-0 PCB。
 * ------------------------------------------------------------------ */

/* 输入缓冲（用户名 / 密码，各最长 63 字符） */
#define LOCK_FIELD_MAX 63
static char    g_lock_user[LOCK_FIELD_MAX + 1];
static int     g_lock_user_len = 0;
static char    g_lock_pw[LOCK_FIELD_MAX + 1];
static int     g_lock_pw_len = 0;
static int     g_lock_focus  = 0;   /* 0=用户名框，1=密码框 */
static int     g_lock_wrong  = 0;   /* 是否显示"验证失败"提示 */

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

/* 校验输入是否为正确凭证：调用 arch_x86_64_login_authenticate()，
 * 查 /etc/passwd + /etc/shadow 后端完成账户定位与密码比对。
 * 返回 1=OK，0=失败。 */
static int lock_verify(const char *name, const char *password)
{
    x86_64_passwd_entry_t pw;
    int rc;

    rc = arch_x86_64_login_authenticate(name, password, &pw);
    memset(&pw, 0, sizeof(pw));   /* 抹掉敏感残留 */
    return (rc == X86_64_LOGIN_OK) ? 1 : 0;
}

/* 全屏绘制一帧锁屏画面 */
static void lock_render(void)
{
    int sw = gui_screen_width();
    int sh = gui_screen_height();
    int i;

    /* 面板尺寸（变高以容纳双输入框） */
    int pw = 460;
    int ph = 300;
    int px = (sw - pw) / 2;
    int py = (sh - ph) / 2;

    /* 输入框公共几何 */
    int bw = pw - 80;
    int bh = 40;
    int bx = px + 40;
    int uby = py + 96;              /* 用户名框 y */
    int pby = uby + bh + 34;        /* 密码框 y */

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
    gui_draw_text(px + 40, py + 28, "OpenOS  \xe7\xb3\xbb\xe7\xbb\x9f\xe5\xb7\xb2\xe9\x94\x81\xe5\xae\x9a", LOCK_TEXT);
    gui_draw_text(px + 40, py + 56, "Sign in to unlock  (Tab to switch)", LOCK_HINT);

    /* 4) 用户名框 */
    gui_screen_fill_rect(bx, uby, bw, bh, LOCK_BOX_BG);
    gui_screen_draw_border(bx, uby, bw, bh, (g_lock_focus == 0) ? 3 : 2,
                           (g_lock_focus == 0) ? LOCK_DOT : LOCK_BOX_BDR);
    if (g_lock_user_len == 0) {
        gui_draw_text(bx + 14, uby + bh / 2 - 8, "username", LOCK_HINT);
    } else {
        gui_draw_text(bx + 14, uby + bh / 2 - 8, g_lock_user, LOCK_TEXT);
    }

    /* 5) 密码框（掩码点） */
    gui_screen_fill_rect(bx, pby, bw, bh, LOCK_BOX_BG);
    gui_screen_draw_border(bx, pby, bw, bh, (g_lock_focus == 1) ? 3 : 2,
                           (g_lock_focus == 1) ? LOCK_DOT : LOCK_BOX_BDR);
    {
        int dot_r = 4;
        int gap   = 18;
        int dx    = bx + 16;
        int dy    = pby + bh / 2 - dot_r;
        for (i = 0; i < g_lock_pw_len && i < LOCK_FIELD_MAX; i++) {
            gui_screen_fill_rect(dx, dy, dot_r * 2, dot_r * 2, LOCK_DOT);
            dx += gap;
        }
        if (g_lock_pw_len == 0) {
            gui_draw_text(bx + 14, pby + bh / 2 - 8, "password", LOCK_HINT);
        }
    }

    /* 6) 提示行 */
    if (g_lock_wrong) {
        gui_draw_text(bx, pby + bh + 16, "Login failed, try again", LOCK_ERR);
    } else {
        gui_draw_text(bx, pby + bh + 16, "Default: openos / openos   (Enter to unlock)", LOCK_HINT);
    }

    /* 7) present */
    gui_screen_present();
}

void lockscreen_run(void)
{
    gui_event_t ev;
    int need_redraw = 1;

    g_lock_pw_len   = 0;
    g_lock_wrong    = 0;
    g_lock_pw[0]    = '\0';
    /* 预填默认账户 openos，初始焦点落在密码框方便直接输入 */
    g_lock_user_len = 6;
    g_lock_user[0]='o'; g_lock_user[1]='p'; g_lock_user[2]='e';
    g_lock_user[3]='n'; g_lock_user[4]='o'; g_lock_user[5]='s'; g_lock_user[6]='\0';
    g_lock_focus    = 1;

    early_console64_write("[lockscreen] starting (fullscreen), waiting for password\n");

    /* 开启锁屏输入门闸：否则没有焦点 widget，键盘中断会在 capture 判定时被丢弃 */
    gui_set_lockscreen_capture(1);

    for (;;) {
        if (need_redraw) {
            lock_render();
            need_redraw = 0;
        }

        /* 关键：锁屏阻塞了内核主循环，主循环里的 usb_hid_poll() 不会再跑。
         * 纯 USB HID 环境下键盘/鼠标走 USB 中断端点轮询，
         * 必须在此手动轮询，否则 USB 键盘的 report 永远无人消费，事件队列恒空，
         * 表现为“锁屏界面完全无法输入密码”。
         * 注意：此循环每 5ms 跑一次，切勿在此无限流地写串口日志，
         * 否则串口 I/O 会吃满时间片、饿死 usb_hid_poll，反而导致输入进不来。 */
        usb_hid_poll();

        /* 消费所有排队事件 */
        while (gui_event_pop(&ev)) {
            if (ev.type != GUI_EVENT_KEY_DOWN) {
                continue;   /* 锁屏阶段忽略鼠标等其它事件 */
            }

            {
                int k = ev.key;

                if (k == GUI_KEY_ENTER || k == '\n' || k == '\r') {
                    /* 回车：认证（主键盘 Enter 经 scancode 表映射为 0x0A，小键盘 Enter 为 GUI_KEY_ENTER=13，两者都接受） */
                    g_lock_user[g_lock_user_len] = '\0';
                    g_lock_pw[g_lock_pw_len]     = '\0';
                    if (lock_verify(g_lock_user, g_lock_pw)) {
                        early_console64_write("[lockscreen] auth OK, unlocking\n");
                        /* 抹掉明文残留 */
                        memset(g_lock_pw, 0, sizeof(g_lock_pw));
                        g_lock_pw_len = 0;
                        gui_set_lockscreen_capture(0);   /* 关闭门闸，交回焦点给桌面 */
                        return;
                    }
                    early_console64_write("[lockscreen] auth failed\n");
                    g_lock_wrong  = 1;
                    g_lock_pw_len = 0;
                    g_lock_pw[0]  = '\0';
                    need_redraw   = 1;
                } else if (k == GUI_KEY_TAB) {
                    /* Tab：切换焦点 */
                    g_lock_focus = (g_lock_focus == 0) ? 1 : 0;
                    need_redraw  = 1;
                } else if (k == GUI_KEY_BACKSPACE) {
                    /* 退格：作用于当前焦点字段 */
                    if (g_lock_focus == 0) {
                        if (g_lock_user_len > 0) {
                            g_lock_user_len--;
                            g_lock_user[g_lock_user_len] = '\0';
                            need_redraw = 1;
                        }
                    } else {
                        if (g_lock_pw_len > 0) {
                            g_lock_pw_len--;
                            g_lock_pw[g_lock_pw_len] = '\0';
                            need_redraw = 1;
                        }
                    }
                } else if (k >= 0x20 && k < 0x7F) {
                    /* 可见字符：写入当前焦点字段 */
                    if (g_lock_focus == 0) {
                        if (g_lock_user_len < LOCK_FIELD_MAX) {
                            g_lock_user[g_lock_user_len++] = (char)k;
                            g_lock_user[g_lock_user_len] = '\0';
                            g_lock_wrong = 0;
                            need_redraw  = 1;
                        }
                    } else {
                        if (g_lock_pw_len < LOCK_FIELD_MAX) {
                            g_lock_pw[g_lock_pw_len++] = (char)k;
                            g_lock_pw[g_lock_pw_len] = '\0';
                            g_lock_wrong = 0;
                            need_redraw  = 1;
                        }
                    }
                }
            }
        }

        /* 限速轮询：本系统没有 PIT 100Hz 周期中断（只有 SMP 调度用的
         * LAPIC timer，锁屏阻塞 BSP 后不保证周期性唤醒 hlt），
         * 若用 `sti; hlt` 睡到中断，会一睡不醒 —— 表现为 poll# 卡在 1、
         * 指针不动、点击无效、密码无法输入。
         * 故改为 `sti`（保持 IF=1 让 USB/键盘/鼠标中断能进）+ 短延时节流：
         * 每轮 delay_ms(5) 约 200Hz 轮询，既不满载 CPU 饿死 QEMU UI 线程，
         * 又能持续驱动 usb_hid_poll() 与消费事件队列。 */
        __asm__ __volatile__("sti");
        arch_x86_64_delay_ms(5);
    }
}
