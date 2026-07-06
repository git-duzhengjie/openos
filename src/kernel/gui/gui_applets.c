/*
 * gui_applets.c - Small window applets subsystem
 *
 * Split out from gui.c. Contains four self-contained mini-app windows plus
 * the global notification center:
 *   - About dialog          (gui_about_open)
 *   - Recycle bin window     (gui_recycle_open)
 *   - Notification center    (gui_notif_open, gui_notify + notif log state)
 *   - Wi-Fi window           (gui_wifi_open)
 *
 * Private state kept static here. Cross-file symbols declared in gui_internal.h.
 *   - gui_notify / gui_about_open / gui_recycle_open / gui_wifi_open : exported
 *   - gui_notif_open : exported (invoked from taskbar notif widget in gui.c)
 *   - gui_applets_notif_count() : accessor for taskbar unread badge
 */
#include "gui.h"
#include "gui_internal.h"
#include "font.h"
#include "i18n.h"
#include "string.h"

static gui_window_t *g_about_win = 0;
static gui_window_t *g_recycle_win = 0;
static gui_window_t *g_notif_win = 0;
static gui_window_t *g_wifi_win = 0;

#define GUI_NOTIF_MAX        16
#define GUI_NOTIF_TEXT_LEN   80

typedef struct {
    int used;
    char text[GUI_NOTIF_TEXT_LEN];
} gui_notif_entry_t;

static gui_notif_entry_t g_notif_log[GUI_NOTIF_MAX];
static uint32_t g_notif_count = 0;
static uint32_t g_notif_unread = 0;

void gui_notify(const char *text) {
    uint32_t i, j;
    if (!text) return;
    /* shift if full */
    if (g_notif_count >= GUI_NOTIF_MAX) {
        for (i = 0; i + 1 < GUI_NOTIF_MAX; i++) g_notif_log[i] = g_notif_log[i + 1];
        g_notif_count = GUI_NOTIF_MAX - 1;
    }
    g_notif_log[g_notif_count].used = 1;
    for (j = 0; j + 1 < GUI_NOTIF_TEXT_LEN && text[j]; j++) {
        g_notif_log[g_notif_count].text[j] = text[j];
    }
    g_notif_log[g_notif_count].text[j] = 0;
    g_notif_count++;
    g_notif_unread++;
}

static void about_on_close(gui_window_t *win, void *ud) {
    (void)win; (void)ud;
    g_about_win = 0;
}

static void about_on_ok(gui_widget_t *w, void *ud) {
    (void)w; (void)ud;
    if (g_about_win) {
        gui_window_t *win = g_about_win;
        g_about_win = 0;
        gui_window_set_on_close(win, 0, 0);
        gui_destroy_window(win);
        gui_render();
    }
}

void gui_about_open(void) {
    gui_widget_t *btn;
    if (g_about_win) {
        gui_window_set_on_close(g_about_win, 0, 0);
        gui_destroy_window(g_about_win);
        g_about_win = 0;
    }
    g_about_win = gui_create_window(180, 140, 360, 200, i18n_t(I18N_KEY_WIN_ABOUT));
    if (!g_about_win) return;
    gui_window_set_on_close(g_about_win, about_on_close, 0);
    gui_add_label(g_about_win, 16, 50,  328, 16, i18n_t(I18N_KEY_ABOUT_TAGLINE));
    gui_add_label(g_about_win, 16, 74,  328, 16, i18n_t(I18N_KEY_ABOUT_VERSION));
    gui_add_label(g_about_win, 16, 98,  328, 16, i18n_t(I18N_KEY_ABOUT_BUILD));
    gui_add_label(g_about_win, 16, 122, 328, 16, i18n_t(I18N_KEY_ABOUT_LICENSE));
    btn = gui_add_button(g_about_win, 140, 152, 80, 28, i18n_t(I18N_KEY_BTN_OK), about_on_ok, 0);
    (void)btn;
    gui_render();
}

static void recycle_on_close(gui_window_t *win, void *ud) {
    (void)win; (void)ud;
    g_recycle_win = 0;
}

void gui_recycle_open(void) {
    int win_w = 420;
    int win_h = 240;

    if (g_recycle_win) {
        gui_window_set_on_close(g_recycle_win, 0, 0);
        gui_destroy_window(g_recycle_win);
        g_recycle_win = 0;
    }
    g_recycle_win = gui_create_window(140, 120, win_w, win_h, i18n_t(I18N_KEY_WIN_RECYCLE_BIN));
    if (!g_recycle_win) return;
    gui_window_set_on_close(g_recycle_win, recycle_on_close, 0);
    gui_render();
}



/* === Notification Center === */
static void notif_on_close(gui_window_t *win, void *ud) {
    (void)win; (void)ud;
    g_notif_win = 0;
}

static void notif_on_close_btn(gui_widget_t *w, void *ud) {
    (void)w; (void)ud;
    if (g_notif_win) {
        gui_window_t *win = g_notif_win;
        g_notif_win = 0;
        gui_window_set_on_close(win, 0, 0);
        gui_destroy_window(win);
        gui_render();
    }
}

static void notif_on_clear(gui_widget_t *w, void *ud);

static void wifi_on_close(gui_window_t *win, void *ud) {
    (void)win;
    (void)ud;
    g_wifi_win = 0;
}

void gui_wifi_open(void) {
    net_wifi_network_info_t nets[NET_WIFI_MAX_RESULTS];
    uint32_t count;
    uint32_t i;
    int margin = (int)font_scale_value(16);
    int line_h = (int)font_get_line_height(font_get_default()) + 6;
    int row_h = line_h + 8;
    int win_w = (int)font_scale_value(420);
    int win_h = (int)font_scale_value(260);
    int x;
    int y;

    if (win_w < 360) win_w = 360;
    if (win_h < 220) win_h = 220;
    if (g_wifi_win) {
        gui_destroy_window(g_wifi_win);
        g_wifi_win = 0;
    }
    g_wifi_win = gui_create_window(230, 120, win_w, win_h, i18n_t(I18N_KEY_WIFI_AVAILABLE_NETWORKS));
    if (!g_wifi_win) return;
    gui_window_set_on_close(g_wifi_win, wifi_on_close, 0);

    x = margin;
    y = 42;
    count = net_scan_wifi(nets, NET_WIFI_MAX_RESULTS);
    if (count == 0) {
        gui_add_label(g_wifi_win, x, y, win_w - margin * 2, line_h + 6, i18n_t(I18N_KEY_WIFI_NO_NETWORKS));
        return;
    }

    for (i = 0; i < count && i < NET_WIFI_MAX_RESULTS; i++) {
        char line[96];
        int pos = 0;
        const char *ssid = nets[i].ssid[0] ? nets[i].ssid : "Wi-Fi";
        const char *state = nets[i].connected ? i18n_t(I18N_KEY_WIFI_CONNECTED) :
                            (nets[i].secured ? i18n_t(I18N_KEY_WIFI_SECURED) : i18n_t(I18N_KEY_WIFI_OPEN));
        pos = fp_str_append(line, pos, sizeof(line), ssid);
        pos = fp_str_append(line, pos, sizeof(line), "  ");
        pos = gui_append_uint(line, pos, sizeof(line), nets[i].signal_percent);
        pos = fp_str_append(line, pos, sizeof(line), "%  ");
        pos = fp_str_append(line, pos, sizeof(line), state);
        (void)pos;
        gui_add_label(g_wifi_win, x, y, win_w - margin * 2, line_h + 6, line);
        y += row_h;
        if (y + line_h > win_h - margin) break;
    }
}

void gui_notif_open(void) {
    uint32_t i;
    char header[64];
    char body[GUI_WIDGET_TEXT_CAP];
    gui_widget_t *log_view;
    int pos;
    int body_pos;
    char nbuf[16];
    if (g_notif_win) {
        gui_window_set_on_close(g_notif_win, 0, 0);
        gui_destroy_window(g_notif_win);
        g_notif_win = 0;
    }
    g_notif_win = gui_create_window(120, 100, 420, 320, i18n_t(I18N_KEY_WIN_NOTIFICATIONS));
    if (!g_notif_win) return;
    gui_window_set_on_close(g_notif_win, notif_on_close, 0);

    pos = 0;
    pos = fp_str_append(header, pos, sizeof(header), i18n_t(I18N_KEY_NOTIF_TOTAL));
    pos = fp_str_append(header, pos, sizeof(header), ": ");
    fp_itoa((int)g_notif_count, nbuf);
    pos = fp_str_append(header, pos, sizeof(header), nbuf);
    pos = fp_str_append(header, pos, sizeof(header), "  ");
    pos = fp_str_append(header, pos, sizeof(header), i18n_t(I18N_KEY_NOTIF_UNREAD));
    pos = fp_str_append(header, pos, sizeof(header), ": ");
    fp_itoa((int)g_notif_unread, nbuf);
    pos = fp_str_append(header, pos, sizeof(header), nbuf);
    (void)pos;
    gui_add_label(g_notif_win, 16, 36, 280, 16, header);

    gui_add_button(g_notif_win, 300, 32, 48, 22, i18n_t(I18N_KEY_BTN_CLEAR), notif_on_clear, 0);
    gui_add_button(g_notif_win, 354, 32, 48, 22, i18n_t(I18N_KEY_BTN_CLOSE), notif_on_close_btn, 0);

    body_pos = 0;
    if (g_notif_count > 0) {
        for (i = 0; i < g_notif_count && i < GUI_NOTIF_MAX; i++) {
            if (i > 0) body_pos = fp_str_append(body, body_pos, sizeof(body), "\n");
            body_pos = fp_str_append(body, body_pos, sizeof(body), g_notif_log[i].text);
        }
    }
    body[body_pos < (int)sizeof(body) ? body_pos : (int)sizeof(body) - 1] = 0;

    log_view = gui_add_textarea(g_notif_win, 16, 64, 388, 230, body);
    if (log_view) {
        gui_widget_set_textbox_flags(log_view,
            GUI_TEXTBOX_FLAG_READONLY | GUI_TEXTBOX_FLAG_MULTILINE | GUI_TEXTBOX_FLAG_WRAP);
        gui_widget_set_placeholder(log_view, i18n_t(I18N_KEY_WIN_NOTIFICATIONS));
    }

    g_notif_unread = 0;
    gui_render();
}

static void notif_on_clear(gui_widget_t *w, void *ud) {
    (void)w; (void)ud;
    g_notif_count = 0;
    g_notif_unread = 0;
    gui_notif_open();   /* rebuild to refresh */
}

/* accessor for taskbar unread badge (gui.c) */
uint32_t gui_applets_notif_count(void) {
    return g_notif_count;
}
