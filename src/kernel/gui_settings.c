/*
 * gui_settings.c - Settings panel & network configuration subsystem
 *
 * Split out from gui.c. Contains:
 *   - Settings window (language / font-size / network entry)
 *   - Network device sub-window (DHCP / static IPv4 config)
 *   - gui_settings_row_* helper widget builders (public, declared in gui.h)
 *
 * Private state kept static here. Cross-file symbols declared in gui_internal.h.
 */
#include "gui.h"
#include "gui_internal.h"
#include "font.h"
#include "i18n.h"
#include "string.h"
#include "net/net.h"
#include "net/dhcp.h"
#include "net/dns.h"
#include "net/net_config.h"

/* ---- private module state ---- */
static gui_window_t *g_settings_win = 0;
static int g_settings_language_dropdown_open = 0;

static gui_window_t *g_network_win = 0;
static gui_widget_t *g_network_ip_box = 0;
static gui_widget_t *g_network_mask_box = 0;
static gui_widget_t *g_network_gateway_box = 0;
static gui_widget_t *g_network_dns_box = 0;

/* ---- forward declarations (module-internal) ---- */
static void gui_settings_build(int show_notice);
static void gui_network_build(int show_notice);
static void settings_open_network(gui_widget_t *w, void *ud);

/* ================= Settings panel (was gui.c 10999-11290) ================= */
static const char *gui_settings_language_name(void) {
    return (i18n_current() == I18N_LOCALE_ZH) ? i18n_t(I18N_KEY_SETTINGS_LANGUAGE_CHINESE)
                                             : i18n_t(I18N_KEY_SETTINGS_LANGUAGE_ENGLISH);
}

static void settings_on_close(gui_window_t *win, void *ud) {
    (void)win;
    (void)ud;
    g_settings_win = 0;
}

static void settings_toggle_language_dropdown(gui_widget_t *w, void *ud) {
    (void)w;
    (void)ud;
    g_settings_language_dropdown_open = !g_settings_language_dropdown_open;
    gui_settings_build(0);
}

static void settings_apply_language_en(gui_widget_t *w, void *ud) {
    (void)w;
    (void)ud;
    g_settings_language_dropdown_open = 0;
    i18n_set_locale(I18N_LOCALE_EN);
    gui_desktop_refresh_i18n_labels();
    gui_settings_build(1);
}

static void settings_apply_language_zh(gui_widget_t *w, void *ud) {
    (void)w;
    (void)ud;
    g_settings_language_dropdown_open = 0;
    i18n_set_locale(I18N_LOCALE_ZH);
    gui_desktop_refresh_i18n_labels();
    gui_settings_build(1);
}

static void settings_apply_font_slider(gui_widget_t *w, void *ud) {
    (void)ud;
    if (!w) return;
    if (w->value <= 0) font_set_size(FONT_SIZE_SMALL);
    else if (w->value >= 2) font_set_size(FONT_SIZE_LARGE);
    else font_set_size(FONT_SIZE_MEDIUM);
    gui_invalidate_all();
}

static void network_refresh(gui_widget_t *w, void *ud) {
    net_device_info_t info;
    (void)w;
    (void)ud;
    if (gui_get_primary_net_info(&info) == 0) net_refresh_device_status(info.name);
    gui_network_build(1);
}

static void network_toggle_admin(gui_widget_t *w, void *ud) {
    net_device_info_t info;
    (void)ud;
    if (!w) return;
    if (gui_get_primary_net_info(&info) == 0) net_set_device_admin_up(info.name, w->value ? 1 : 0);
    gui_network_build(1);
}

static void network_dhcp(gui_widget_t *w, void *ud) {
    (void)w;
    (void)ud;
    dhcp_start();
    (void)net_config_save_dhcp();
    gui_network_build(1);
}

void gui_settings_row_init(gui_settings_row_t *row,
                           gui_window_t *window,
                           int x,
                           int y,
                           int width,
                           int label_h,
                           int button_h,
                           int button_w,
                           int gap) {
    if (!row) return;
    row->window = window;
    row->x = x;
    row->y = y;
    row->width = width;
    row->label_h = label_h;
    row->button_h = button_h;
    row->button_w = button_w;
    row->gap = gap;
}

void gui_settings_row_label(const gui_settings_row_t *row, const char *label) {
    if (!row || !row->window || !label) return;
    gui_add_label(row->window, row->x, row->y, row->width, row->label_h, label);
}

gui_widget_t *gui_settings_row_toggle(const gui_settings_row_t *row,
                                             const char *text,
                                             int checked,
                                             gui_widget_callback_t cb,
                                             void *ud) {
    int w;
    if (!row || !row->window) return 0;
    w = row->button_w * 2 + row->gap;
    return gui_add_toggle(row->window, row->x, row->y, w, row->button_h, text, checked, cb, ud);
}

gui_widget_t *gui_settings_row_button(const gui_settings_row_t *row,
                                             const char *text,
                                             gui_widget_callback_t cb,
                                             void *ud) {
    int w;
    if (!row || !row->window) return 0;
    w = row->button_w * 2 + row->gap;
    return gui_add_button(row->window, row->x, row->y, w, row->button_h, text, cb, ud);
}

gui_widget_t *gui_settings_row_slider(const gui_settings_row_t *row,
                                             int min,
                                             int max,
                                             int value,
                                             int step,
                                             gui_widget_callback_t cb,
                                             void *ud) {
    int w;
    if (!row || !row->window) return 0;
    w = row->button_w * 3 + row->gap * 2;
    return gui_add_slider(row->window, row->x, row->y, w, row->button_h, min, max, value, step, cb, ud);
}

void gui_settings_row_slider_labels(const gui_settings_row_t *row,
                                           const char *left,
                                           const char *middle,
                                           const char *right) {
    if (!row || !row->window) return;
    gui_add_label(row->window, row->x, row->y + row->button_h, row->button_w, row->label_h, left);
    gui_add_label(row->window, row->x + row->button_w + row->gap, row->y + row->button_h, row->button_w, row->label_h, middle);
    gui_add_label(row->window, row->x + (row->button_w + row->gap) * 2, row->y + row->button_h, row->button_w, row->label_h, right);
}

static int settings_parse_ipv4(const char *text, uint32_t *out) {
    uint32_t parts[4];
    int part = 0;
    uint32_t value = 0;
    int has_digit = 0;
    const char *p = text;
    if (!text || !out) return -1;
    while (*p) {
        if (*p >= '0' && *p <= '9') {
            value = value * 10u + (uint32_t)(*p - '0');
            if (value > 255u) return -1;
            has_digit = 1;
        } else if (*p == '.') {
            if (!has_digit || part >= 3) return -1;
            parts[part++] = value;
            value = 0;
            has_digit = 0;
        } else {
            return -1;
        }
        p++;
    }
    if (!has_digit || part != 3) return -1;
    parts[part] = value;
    *out = NET_IP4(parts[0], parts[1], parts[2], parts[3]);
    return 0;
}

static void network_apply_static(gui_widget_t *w, void *ud) {
    uint32_t ip;
    uint32_t mask;
    uint32_t gateway;
    uint32_t dns;
    (void)w;
    (void)ud;
    if (!g_network_ip_box || !g_network_mask_box || !g_network_gateway_box || !g_network_dns_box) return;
    if (settings_parse_ipv4(g_network_ip_box->text, &ip) != 0) return;
    if (settings_parse_ipv4(g_network_mask_box->text, &mask) != 0) return;
    if (settings_parse_ipv4(g_network_gateway_box->text, &gateway) != 0) return;
    if (settings_parse_ipv4(g_network_dns_box->text, &dns) != 0) return;
    net_config_ipv4(ip, mask, gateway, dns);
    (void)net_config_save_static(ip, mask, gateway, dns);
    gui_network_build(1);
}

static void gui_settings_build(int show_notice) {
    const font_renderer_t *font = font_get_default();
    int line_h = (int)font_get_line_height(font);
    int margin = (int)font_scale_value(14);
    int row_h = line_h + (int)font_scale_value(14);
    int button_h = line_h + (int)font_scale_value(12);
    int button_w = (int)font_scale_value(82);
    int gap = (int)font_scale_value(8);
    int win_w = (int)font_scale_value(640);
    int win_h = margin * 2 + row_h * 18 + 56;
    int x;
    int y;
    gui_settings_row_t row;

    if (win_w < 640) win_w = 640;
    if (win_h < 560) win_h = 560;

    if (g_settings_win) {
        gui_window_set_on_close(g_settings_win, 0, 0);
        gui_destroy_window(g_settings_win);
        g_settings_win = 0;
    }
    g_settings_win = gui_create_window(190, 70, win_w, win_h, i18n_t(I18N_KEY_WIN_SETTINGS));
    if (!g_settings_win) return;
    gui_window_set_on_close(g_settings_win, settings_on_close, 0);

    x = margin;
    y = 36;
    gui_settings_row_init(&row, g_settings_win, x, y, win_w - margin * 2, line_h + 4, button_h, button_w, gap);

    gui_settings_row_label(&row, i18n_t(I18N_KEY_SETTINGS_LANGUAGE));
    y += row_h;
    row.y = y;
    {
        char language_text[64];
        int dropdown_w = button_w * 2 + gap;
        strncpy(language_text, gui_settings_language_name(), sizeof(language_text) - 4);
        language_text[sizeof(language_text) - 4] = '\0';
        {
            uint32_t n = (uint32_t)strlen(language_text);
            if (n + 2 < sizeof(language_text)) {
                language_text[n] = ' ';
                language_text[n + 1] = 'v';
                language_text[n + 2] = '\0';
            }
        }
        gui_settings_row_button(&row, language_text, settings_toggle_language_dropdown, 0);
        if (g_settings_language_dropdown_open) {
            gui_add_button(g_settings_win, x, y + button_h + 2, dropdown_w, button_h, i18n_t(I18N_KEY_SETTINGS_LANGUAGE_ENGLISH), settings_apply_language_en, 0);
            gui_add_button(g_settings_win, x, y + (button_h + 2) * 2, dropdown_w, button_h, i18n_t(I18N_KEY_SETTINGS_LANGUAGE_CHINESE), settings_apply_language_zh, 0);
        }
    }

    y += row_h + gap;
    if (g_settings_language_dropdown_open) y += (button_h + 2) * 2;
    row.y = y;
    gui_settings_row_label(&row, i18n_t(I18N_KEY_SETTINGS_TEXT_SIZE));
    y += row_h;
    row.y = y;
    {
        int value = (font_get_size() == FONT_SIZE_SMALL) ? 0 : ((font_get_size() == FONT_SIZE_LARGE) ? 2 : 1);
        gui_settings_row_slider(&row, 0, 2, value, 1, settings_apply_font_slider, 0);
        gui_settings_row_slider_labels(&row,
                                       i18n_t(I18N_KEY_BTN_FONT_SMALL),
                                       i18n_t(I18N_KEY_BTN_FONT_MEDIUM),
                                       i18n_t(I18N_KEY_BTN_FONT_LARGE));
    }

    y += button_h + line_h + 4 + gap;
    row.y = y;
    gui_settings_row_label(&row, i18n_t(I18N_KEY_SETTINGS_NETWORK));
    y += row_h;
    row.y = y;
    gui_settings_row_button(&row, i18n_t(I18N_KEY_SETTINGS_NETWORK_DEVICE), settings_open_network, 0);

    if (show_notice) gui_notify(i18n_t(I18N_KEY_SETTINGS_APPLIED));
    gui_render();
}

void gui_settings_open(void) {
    gui_settings_build(0);
}

/* ============= Network sub-window (was gui.c 767-949) ============= */
static void network_refresh(gui_widget_t *w, void *ud);
static void network_toggle_admin(gui_widget_t *w, void *ud);
static void network_dhcp(gui_widget_t *w, void *ud);
static void network_apply_static(gui_widget_t *w, void *ud);
static void network_on_close(gui_window_t *win, void *ud) {
    (void)win;
    (void)ud;
    g_network_win = 0;
}

int gui_get_primary_net_info(net_device_info_t *out) {
    net_device_t *dev;

    if (!out) {
        return -1;
    }

    dev = net_get_default_device();
    if (dev && net_get_device_info_by_name(dev->name, out) == 0) {
        return 0;
    }

    return net_get_device_info(0, out);
}

static void gui_network_build(int show_notice) {
    const font_renderer_t *font = font_get_default();
    int line_h = (int)font_get_line_height(font);
    int margin = (int)font_scale_value(14);
    int row_h = line_h + (int)font_scale_value(14);
    int button_h = line_h + (int)font_scale_value(12);
    int button_w = (int)font_scale_value(82);
    int label_w = (int)font_scale_value(130);
    int gap = (int)font_scale_value(8);
    int win_w = (int)font_scale_value(640);
    int win_h = margin * 2 + row_h * 14 + 56;
    int x;
    int y;
    int pos;
    net_device_info_t net_info;
    net_device_info_t list_info;
    int has_net;
    int dev_index;
    char line[224];
    char ip_buf[24];
    char mask_buf[24];
    char gw_buf[24];
    char dns_buf[24];
    char mac_buf[32];
    char rx_buf[16];
    char tx_buf[16];
    const char *mode_text;
    const char *up_text;
    const char *link_text;

    if (win_w < 640) win_w = 640;
    if (win_h < 460) win_h = 460;

    if (g_network_win) {
        gui_window_set_on_close(g_network_win, 0, 0);
        gui_destroy_window(g_network_win);
        g_network_win = 0;
    }
    g_network_ip_box = 0;
    g_network_mask_box = 0;
    g_network_gateway_box = 0;
    g_network_dns_box = 0;

    g_network_win = gui_create_window(210, 90, win_w, win_h, i18n_t(I18N_KEY_SETTINGS_NETWORK));
    if (!g_network_win) return;
    gui_window_set_on_close(g_network_win, network_on_close, 0);

    x = margin;
    y = 36;
    for (dev_index = 0; dev_index < 4; dev_index++) {
        if (net_get_device_info((uint32_t)dev_index, &list_info) != 0) break;
        pos = 0;
        pos = gui_settings_append_field(line, pos, sizeof(line), I18N_KEY_SETTINGS_NETWORK_DEVICE, list_info.name);
        pos = fp_str_append(line, pos, sizeof(line), "  ");
        pos = fp_str_append(line, pos, sizeof(line), list_info.driver);
        pos = fp_str_append(line, pos, sizeof(line), "  ");
        pos = fp_str_append(line, pos, sizeof(line), (list_info.flags & NET_DEVICE_FLAG_UP) ? i18n_t(I18N_KEY_SETTINGS_NETWORK_UP) : i18n_t(I18N_KEY_SETTINGS_NETWORK_DOWN));
        pos = fp_str_append(line, pos, sizeof(line), "/");
        pos = fp_str_append(line, pos, sizeof(line), (list_info.flags & NET_DEVICE_FLAG_LINK_UP) ? i18n_t(I18N_KEY_SETTINGS_NETWORK_LINK_UP) : i18n_t(I18N_KEY_SETTINGS_NETWORK_LINK_DOWN));
        (void)pos;
        gui_add_label(g_network_win, x, y, win_w - margin * 2, line_h + 6, line);
        y += row_h;
    }

    has_net = (gui_get_primary_net_info(&net_info) == 0);
    if (!has_net) {
        gui_add_label(g_network_win, x, y, win_w - margin * 2, line_h + 6, i18n_t(I18N_KEY_SETTINGS_NETWORK_NO_DEVICE));
    } else {
        gui_format_ipv4_inline(net_info.ip, ip_buf, sizeof(ip_buf));
        gui_format_ipv4_inline(net_info.netmask, mask_buf, sizeof(mask_buf));
        gui_format_ipv4_inline(net_info.gateway, gw_buf, sizeof(gw_buf));
        gui_format_ipv4_inline(net_info.dns, dns_buf, sizeof(dns_buf));
        gui_format_mac_inline(net_info.mac, mac_buf, sizeof(mac_buf));
        mode_text = (net_info.config_mode == NET_CONFIG_MODE_DHCP) ? i18n_t(I18N_KEY_SETTINGS_NETWORK_DHCP) : i18n_t(I18N_KEY_SETTINGS_NETWORK_STATIC);
        up_text = (net_info.flags & NET_DEVICE_FLAG_UP) ? i18n_t(I18N_KEY_SETTINGS_NETWORK_UP) : i18n_t(I18N_KEY_SETTINGS_NETWORK_DOWN);
        link_text = (net_info.flags & NET_DEVICE_FLAG_LINK_UP) ? i18n_t(I18N_KEY_SETTINGS_NETWORK_LINK_UP) : i18n_t(I18N_KEY_SETTINGS_NETWORK_LINK_DOWN);

        pos = 0;
        pos = gui_settings_append_field(line, pos, sizeof(line), I18N_KEY_SETTINGS_NETWORK_DEVICE, net_info.name);
        pos = fp_str_append(line, pos, sizeof(line), "  ");
        pos = fp_str_append(line, pos, sizeof(line), net_info.driver);
        (void)pos;
        gui_add_label(g_network_win, x, y, win_w - margin * 2, line_h + 6, line);
        y += row_h;

        pos = 0;
        pos = gui_settings_append_field(line, pos, sizeof(line), I18N_KEY_SETTINGS_NETWORK_STATUS, up_text);
        pos = fp_str_append(line, pos, sizeof(line), " / ");
        pos = fp_str_append(line, pos, sizeof(line), link_text);
        (void)pos;
        gui_add_label(g_network_win, x, y, win_w - margin * 2, line_h + 6, line);
        y += row_h;

        pos = 0;
        pos = gui_settings_append_field(line, pos, sizeof(line), I18N_KEY_SETTINGS_NETWORK_MAC, mac_buf);
        pos = fp_str_append(line, pos, sizeof(line), "  ");
        pos = gui_settings_append_field(line, pos, sizeof(line), I18N_KEY_SETTINGS_NETWORK_MODE, mode_text);
        (void)pos;
        gui_add_label(g_network_win, x, y, win_w - margin * 2, line_h + 6, line);
        y += row_h;

        pos = 0;
        pos = gui_settings_append_field(line, pos, sizeof(line), I18N_KEY_SETTINGS_NETWORK_IP, ip_buf);
        pos = fp_str_append(line, pos, sizeof(line), "  ");
        pos = gui_settings_append_field(line, pos, sizeof(line), I18N_KEY_SETTINGS_NETWORK_GATEWAY, gw_buf);
        (void)pos;
        gui_add_label(g_network_win, x, y, win_w - margin * 2, line_h + 6, line);
        y += row_h;

        pos = 0;
        pos = gui_settings_append_field(line, pos, sizeof(line), I18N_KEY_SETTINGS_NETWORK_DNS, dns_buf);
        pos = fp_str_append(line, pos, sizeof(line), "  ");
        pos = gui_settings_append_field(line, pos, sizeof(line), I18N_KEY_SETTINGS_NETWORK_TRAFFIC, "RX/TX ");
        pos = gui_append_uint(line, pos, sizeof(line), net_info.rx_packets);
        pos = fp_str_append(line, pos, sizeof(line), "/");
        pos = gui_append_uint(line, pos, sizeof(line), net_info.tx_packets);
        (void)rx_buf;
        (void)tx_buf;
        (void)pos;
        gui_add_label(g_network_win, x, y, win_w - margin * 2, line_h + 6, line);
        y += row_h;

        gui_add_button(g_network_win, x, y, button_w, button_h, i18n_t(I18N_KEY_BTN_REFRESH), network_refresh, 0);
        gui_add_toggle(g_network_win, x + button_w + gap, y, button_w * 2, button_h,
                       i18n_t(I18N_KEY_SETTINGS_NETWORK_UP),
                       (net_info.flags & NET_DEVICE_FLAG_UP) ? 1 : 0,
                       network_toggle_admin, 0);
        gui_add_button(g_network_win, x + button_w * 3 + gap * 2, y, button_w, button_h, i18n_t(I18N_KEY_SETTINGS_NETWORK_DHCP), network_dhcp, 0);
        y += row_h;

        gui_add_label(g_network_win, x, y + (button_h - line_h) / 2, label_w, line_h + 4, i18n_t(I18N_KEY_SETTINGS_NETWORK_IP));
        g_network_ip_box = gui_add_textbox(g_network_win, x + label_w + gap, y, button_w * 2, button_h, ip_buf);
        gui_add_label(g_network_win, x + label_w + gap + button_w * 2 + gap, y + (button_h - line_h) / 2, label_w / 2, line_h + 4, i18n_t(I18N_KEY_SETTINGS_NETWORK_NETMASK));
        g_network_mask_box = gui_add_textbox(g_network_win, x + label_w + gap + button_w * 2 + gap + label_w / 2, y, button_w * 2, button_h, mask_buf);
        y += row_h;

        gui_add_label(g_network_win, x, y + (button_h - line_h) / 2, label_w, line_h + 4, i18n_t(I18N_KEY_SETTINGS_NETWORK_GATEWAY));
        g_network_gateway_box = gui_add_textbox(g_network_win, x + label_w + gap, y, button_w * 2, button_h, gw_buf);
        gui_add_label(g_network_win, x + label_w + gap + button_w * 2 + gap, y + (button_h - line_h) / 2, label_w / 2, line_h + 4, i18n_t(I18N_KEY_SETTINGS_NETWORK_DNS));
        g_network_dns_box = gui_add_textbox(g_network_win, x + label_w + gap + button_w * 2 + gap + label_w / 2, y, button_w * 2, button_h, dns_buf);
        y += row_h;

        gui_add_button(g_network_win, x, y, button_w * 2, button_h, i18n_t(I18N_KEY_SETTINGS_NETWORK_APPLY_STATIC), network_apply_static, 0);
    }

    if (show_notice) gui_notify(i18n_t(I18N_KEY_SETTINGS_APPLIED));
    gui_render();
}

void gui_network_open(void) {
    gui_network_build(0);
}

static void settings_open_network(gui_widget_t *w, void *ud) {
    (void)w;
    (void)ud;
    gui_network_open();
}

/* Exported: called by gui.c when a slider drag is released.
 * If the slider belongs to the settings window, rebuild it and return 1. */
int gui_settings_on_slider_released(gui_widget_t *w) {
    if (w && w->owner == g_settings_win) {
        gui_settings_build(0);
        return 1;
    }
    return 0;
}
