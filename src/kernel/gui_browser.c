/* ============================================================
 * openos - GUI browser & network-tool submodule
 * Split from gui.c (browser_* / gui_nettool_* / browser_load_*)
 * Depends on gui_internal.h for shared symbols.
 * ============================================================ */
#include "gui_internal.h"
#include "types.h"
#include "gui.h"
#include "font.h"
#include "serial.h"
#include "string.h"
#include "heap.h"
#include "i18n.h"
#include "net/net.h"
#include "net/dhcp.h"
#include "net/dns.h"
#include "net/net_config.h"
#include "tls_parser.h"
#include "process.h"

/* ---- 模块内部前向声明 ---- */
static void browser_load_start(void);
static void browser_load_finish(const char *status);
static int  browser_str_starts_ci(const char *p, const char *prefix);
static int  browser_header_name_eq(const char *p, const char *name);

/* ---- browser type defs (orig gui.c 33-64) ---- */
typedef enum browser_scheme {
    BROWSER_SCHEME_HTTP = 0,
    BROWSER_SCHEME_HTTPS = 1
} browser_scheme_t;

typedef enum browser_load_state {
    BROWSER_LOAD_IDLE = 0,
    BROWSER_LOAD_RESOLVING,
    BROWSER_LOAD_CONNECTING,
    BROWSER_LOAD_HTTP_SEND,
    BROWSER_LOAD_HTTP_RECV,
    BROWSER_LOAD_TLS_SEND,
    BROWSER_LOAD_TLS_RECV
} browser_load_state_t;

typedef struct browser_load_context {
    browser_load_state_t state;
    uint32_t state_started_ms;
    char url[64];
    char host[64];
    char path[128];
    uint16_t port;
    browser_scheme_t scheme;
    uint32_t ip;
    int conn;
    char request[256];
    uint8_t response[2048];
    int total;
    uint8_t tls_record[256];
    int tls_hello_len;
    uint8_t tls_hello[256];
} browser_load_context_t;

/* ---- browser global state (orig gui.c 142,235-241) ---- */
static browser_load_context_t g_browser_load;
#define GUI_BROWSER_CONTENT_LINES 12
#define GUI_BROWSER_LINKS_MAX GUI_BROWSER_CONTENT_LINES
static gui_widget_t *g_browser_address_box = 0;
static gui_widget_t *g_browser_content_lines[GUI_BROWSER_CONTENT_LINES];
static char g_browser_line_links[GUI_BROWSER_LINKS_MAX][128];
static gui_widget_t *g_browser_status_label = 0;
static gui_window_t *g_browser_win = 0;

/* ---- browser window accessor ---- */
gui_window_t *gui_browser_window(void) { return g_browser_win; }

/* ---- browser / nettool impl (orig gui.c 12460-13903) ---- */
static void browser_on_close(gui_window_t *win, void *ud) {
    uint32_t i;
    (void)win;
    (void)ud;
    browser_load_finish(0);
    g_browser_win = 0;
    g_browser_address_box = 0;
    g_browser_status_label = 0;
    for (i = 0; i < GUI_BROWSER_CONTENT_LINES; i++) g_browser_content_lines[i] = 0;
}

static void browser_set_widget_text(gui_widget_t *widget, const char *text) {
    uint32_t i;
    if (!widget || !text) return;
    for (i = 0; i < sizeof(widget->text) - 1u && text[i]; i++) widget->text[i] = text[i];
    widget->text[i] = '\0';
}

static void browser_set_status(const char *text) {
    browser_set_widget_text(g_browser_status_label, text);
    if (text) gui_notify(text);
    gui_render();
}

static void browser_clear_content(void) {
    uint32_t i;
    for (i = 0; i < GUI_BROWSER_CONTENT_LINES; i++) {
        browser_set_widget_text(g_browser_content_lines[i], "");
        g_browser_line_links[i][0] = '\0';
        if (g_browser_content_lines[i]) {
            g_browser_content_lines[i]->type = GUI_WIDGET_LABEL;
            g_browser_content_lines[i]->on_click = 0;
            g_browser_content_lines[i]->user_data = 0;
            g_browser_content_lines[i]->fg_color = gui_rgb(20, 30, 45);
        }
    }
}

static int browser_copy_until(char *out, uint32_t cap, const char **pp, char stop) {
    uint32_t len = 0;
    const char *p;
    if (!out || cap == 0 || !pp || !*pp) return -1;
    p = *pp;
    while (*p && *p != stop) {
        if (len + 1u >= cap) return -1;
        out[len++] = *p++;
    }
    out[len] = '\0';
    *pp = p;
    return 0;
}

static int browser_parse_url(const char *url, char *host, uint32_t host_cap,
                             char *path, uint32_t path_cap, uint16_t *port,
                             browser_scheme_t *scheme) {
    const char *p;
    const char *host_start;
    uint32_t len = 0;
    uint32_t port_num = 0;
    browser_scheme_t parsed_scheme = BROWSER_SCHEME_HTTP;
    if (!url || !host || !path || !port || host_cap == 0 || path_cap == 0) return -1;
    p = url;
    if (browser_str_starts_ci(p, "http://")) {
        parsed_scheme = BROWSER_SCHEME_HTTP;
        p += 7;
    } else if (browser_str_starts_ci(p, "https://")) {
        parsed_scheme = BROWSER_SCHEME_HTTPS;
        p += 8;
    }
    host_start = p;
    while (*p && *p != '/' && *p != ':') {
        if (len + 1u >= host_cap) return -1;
        host[len++] = *p++;
    }
    host[len] = '\0';
    if (p == host_start) return -1;
    *port = (parsed_scheme == BROWSER_SCHEME_HTTPS) ? 443u : 80u;
    if (*p == ':') {
        p++;
        while (*p >= '0' && *p <= '9') {
            port_num = port_num * 10u + (uint32_t)(*p - '0');
            if (port_num > 65535u) return -1;
            p++;
        }
        if (port_num == 0) return -1;
        *port = (uint16_t)port_num;
    }
    if (*p == '/') {
        if (browser_copy_until(path, path_cap, &p, '\0') != 0) return -1;
    } else {
        path[0] = '/';
        path[1] = '\0';
    }
    if (scheme) *scheme = parsed_scheme;
    return 0;
}

static uint32_t browser_render_text_at(const char *text, uint32_t start_line) {
    uint32_t line = start_line;
    uint32_t col = 0;
    char linebuf[64];
    const char *p;
    if (line >= GUI_BROWSER_CONTENT_LINES) return line;
    if (!text || !*text) return line;
    linebuf[0] = '\0';
    p = text;
    while (*p && line < GUI_BROWSER_CONTENT_LINES) {
        char ch = *p++;
        if (ch == '\r') continue;
        if (ch == '\n' || col >= 58u) {
            linebuf[col] = '\0';
            browser_set_widget_text(g_browser_content_lines[line], linebuf);
            line++;
            col = 0;
            linebuf[0] = '\0';
            if (ch == '\n') continue;
            if (line >= GUI_BROWSER_CONTENT_LINES) break;
        }
        if ((unsigned char)ch < 32u) ch = ' ';
        linebuf[col++] = ch;
        linebuf[col] = '\0';
    }
    if (line < GUI_BROWSER_CONTENT_LINES && col > 0) {
        browser_set_widget_text(g_browser_content_lines[line], linebuf);
        line++;
    }
    return line;
}

static int browser_str_starts_ci(const char *p, const char *prefix) {
    uint32_t i = 0;
    if (!p || !prefix) return 0;
    while (prefix[i]) {
        char a = p[i];
        char b = prefix[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
        i++;
    }
    return 1;
}

static int browser_html_tag_break(const char *tag) {
    while (*tag == ' ' || *tag == '\t' || *tag == '/') tag++;
    return browser_str_starts_ci(tag, "br") || browser_str_starts_ci(tag, "p") ||
           browser_str_starts_ci(tag, "div") || browser_str_starts_ci(tag, "section") ||
           browser_str_starts_ci(tag, "article") || browser_str_starts_ci(tag, "header") ||
           browser_str_starts_ci(tag, "footer") || browser_str_starts_ci(tag, "main") ||
           browser_str_starts_ci(tag, "nav") || browser_str_starts_ci(tag, "blockquote") ||
           browser_str_starts_ci(tag, "ul") || browser_str_starts_ci(tag, "ol") ||
           browser_str_starts_ci(tag, "li") || browser_str_starts_ci(tag, "tr") ||
           browser_str_starts_ci(tag, "table") || browser_str_starts_ci(tag, "h1") ||
           browser_str_starts_ci(tag, "h2") || browser_str_starts_ci(tag, "h3") ||
           browser_str_starts_ci(tag, "h4") || browser_str_starts_ci(tag, "h5") ||
           browser_str_starts_ci(tag, "h6") || browser_str_starts_ci(tag, "title") ||
           browser_str_starts_ci(tag, "pre");
}

static int browser_html_tag_is_pre(const char *tag) {
    while (*tag == ' ' || *tag == '\t' || *tag == '/') tag++;
    return browser_str_starts_ci(tag, "pre") || browser_str_starts_ci(tag, "code");
}

static int browser_html_tag_is_list_item(const char *tag) {
    while (*tag == ' ' || *tag == '\t' || *tag == '/') tag++;
    return browser_str_starts_ci(tag, "li");
}

static char browser_decode_entity(const char **pp) {
    const char *p = *pp;
    if (browser_str_starts_ci(p, "amp;")) { *pp = p + 4; return '&'; }
    if (browser_str_starts_ci(p, "lt;")) { *pp = p + 3; return '<'; }
    if (browser_str_starts_ci(p, "gt;")) { *pp = p + 3; return '>'; }
    if (browser_str_starts_ci(p, "quot;")) { *pp = p + 5; return '"'; }
    if (browser_str_starts_ci(p, "apos;")) { *pp = p + 5; return '\''; }
    if (browser_str_starts_ci(p, "#39;")) { *pp = p + 4; return '\''; }
    if (browser_str_starts_ci(p, "nbsp;")) { *pp = p + 5; return ' '; }
    if (browser_str_starts_ci(p, "copy;")) { *pp = p + 5; return 'c'; }
    if (browser_str_starts_ci(p, "reg;")) { *pp = p + 4; return 'r'; }
    if (browser_str_starts_ci(p, "mdash;")) { *pp = p + 6; return '-'; }
    if (browser_str_starts_ci(p, "ndash;")) { *pp = p + 6; return '-'; }
    if (browser_str_starts_ci(p, "hellip;")) { *pp = p + 7; return '.'; }
    if (browser_str_starts_ci(p, "lsquo;")) { *pp = p + 6; return '\''; }
    if (browser_str_starts_ci(p, "rsquo;")) { *pp = p + 6; return '\''; }
    if (browser_str_starts_ci(p, "ldquo;")) { *pp = p + 6; return '"'; }
    if (browser_str_starts_ci(p, "rdquo;")) { *pp = p + 6; return '"'; }
    if (p[0] == '#') {
        uint32_t value = 0;
        const char *q = p + 1;
        int hex = 0;
        if (*q == 'x' || *q == 'X') { hex = 1; q++; }
        while (*q && *q != ';') {
            char c = *q;
            uint32_t digit;
            if (c >= '0' && c <= '9') digit = (uint32_t)(c - '0');
            else if (hex && c >= 'a' && c <= 'f') digit = 10u + (uint32_t)(c - 'a');
            else if (hex && c >= 'A' && c <= 'F') digit = 10u + (uint32_t)(c - 'A');
            else return '&';
            value = value * (hex ? 16u : 10u) + digit;
            if (value > 255u) return '?';
            q++;
        }
        if (*q == ';' && value > 0) {
            *pp = q + 1;
            return (value < 32u) ? ' ' : (char)value;
        }
    }
    return '&';
}

static int browser_html_tag_name_eq(const char *tag, const char *name) {
    uint32_t i = 0;
    if (!tag || !name) return 0;
    while (*tag == ' ' || *tag == '\t' || *tag == '\r' || *tag == '\n') tag++;
    if (*tag == '/') tag++;
    while (name[i]) {
        char a = tag[i];
        char b = name[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
        i++;
    }
    return tag[i] == '\0' || tag[i] == '>' || tag[i] == ' ' || tag[i] == '\t' ||
           tag[i] == '\r' || tag[i] == '\n' || tag[i] == '/';
}

static int browser_html_hidden_tag(const char *tag) {
    return browser_html_tag_name_eq(tag, "head") || browser_html_tag_name_eq(tag, "style") ||
           browser_html_tag_name_eq(tag, "script") || browser_html_tag_name_eq(tag, "svg") ||
           browser_html_tag_name_eq(tag, "noscript") || browser_html_tag_name_eq(tag, "template");
}

static const char *browser_html_skip_hidden(const char *p, const char *tag) {
    const char *q = p;
    const char *name = 0;
    if (browser_html_tag_name_eq(tag, "head")) name = "head";
    else if (browser_html_tag_name_eq(tag, "style")) name = "style";
    else if (browser_html_tag_name_eq(tag, "script")) name = "script";
    else if (browser_html_tag_name_eq(tag, "svg")) name = "svg";
    else if (browser_html_tag_name_eq(tag, "noscript")) name = "noscript";
    else if (browser_html_tag_name_eq(tag, "template")) name = "template";
    if (!name) return p;
    while (*q) {
        if (q[0] == '<' && q[1] == '/' && browser_html_tag_name_eq(q + 2, name)) {
            while (*q && *q != '>') q++;
            if (*q == '>') q++;
            return q;
        }
        q++;
    }
    return q;
}

static const char *browser_html_find_body_start(const char *html) {
    const char *p = html;
    if (!p) return html;
    while (*p) {
        if (*p == '<' && browser_str_starts_ci(p + 1, "body")) {
            while (*p && *p != '>') p++;
            return (*p == '>') ? p + 1 : p;
        }
        p++;
    }
    return html;
}

static void browser_html_to_text(const char *html, char *out, uint32_t cap) {
    uint32_t n = 0;
    int last_space = 1;
    int preserve_ws = 0;
    const char *p = html;
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!html) return;
    p = browser_html_find_body_start(html);
    while (*p && n + 1u < cap) {
        char ch = *p++;
        if (ch == '<') {
            const char *tag_start = p;
            const char *tag_end = p;
            int is_close = (*tag_start == '/');
            while (*tag_end && *tag_end != '>') tag_end++;
            if (!*tag_end) break;
            if (!is_close && browser_html_hidden_tag(tag_start)) {
                p = browser_html_skip_hidden(tag_end + 1, tag_start);
                continue;
            }
            if (browser_html_tag_break(tag_start) && n > 0 && out[n - 1] != '\n') {
                while (n > 0 && out[n - 1] == ' ') n--;
                if (n > 0 && n + 1u < cap) out[n++] = '\n';
                last_space = 1;
            }
            if (!is_close && browser_html_tag_is_list_item(tag_start) && n + 3u < cap) {
                out[n++] = '-';
                out[n++] = ' ';
                last_space = 0;
            }
            if (browser_html_tag_is_pre(tag_start)) preserve_ws = !is_close;
            p = tag_end + 1;
            continue;
        }
        if (ch == '&') ch = browser_decode_entity(&p);
        if (preserve_ws) {
            if (ch == '\r') continue;
            if (ch == '\t') ch = ' ';
            if ((unsigned char)ch < 32u && ch != '\n') continue;
            out[n++] = ch;
            last_space = (ch == ' ' || ch == '\n');
            continue;
        }
        if (ch == '\r' || ch == '\n' || ch == '\t') ch = ' ';
        if ((unsigned char)ch < 32u) continue;
        if (ch == ' ') {
            if (last_space) continue;
            last_space = 1;
        } else {
            last_space = 0;
        }
        out[n++] = ch;
    }
    while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\n')) n--;
    out[n] = '\0';
}

static int browser_text_contains_ci(const char *p, const char *needle, uint32_t max_scan) {
    uint32_t i = 0;
    if (!p || !needle || !needle[0]) return 0;
    while (p[i] && i < max_scan) {
        if (browser_str_starts_ci(p + i, needle)) return 1;
        i++;
    }
    return 0;
}

static const char *browser_skip_text_prefix(const char *p) {
    while (p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    return p ? p : "";
}

static int browser_body_looks_html(const char *body) {
    const char *p = browser_skip_text_prefix(body);
    if (!p || !*p) return 0;
    if (browser_str_starts_ci(p, "<!doctype") || browser_str_starts_ci(p, "<html") ||
        browser_str_starts_ci(p, "<head") || browser_str_starts_ci(p, "<body") ||
        browser_str_starts_ci(p, "<style") || browser_str_starts_ci(p, "</style") ||
        browser_str_starts_ci(p, "</head")) return 1;
    return browser_text_contains_ci(p, "<html", 512u) ||
           browser_text_contains_ci(p, "<body", 512u) ||
           browser_text_contains_ci(p, "<div", 512u) ||
           browser_text_contains_ci(p, "<p", 512u) ||
           browser_text_contains_ci(p, "<h1", 512u) ||
           browser_text_contains_ci(p, "<title", 512u);
}

static int browser_response_is_html(char *response, const char *body) {
    const char *p = response;
    while (p && *p && body && p < body) {
        if (browser_header_name_eq(p, "Content-Type")) {
            const char *v = p;
            while (*v && *v != ':' && *v != '\n') v++;
            if (*v == ':') v++;
            while (*v && *v != '\r' && *v != '\n') {
                if (browser_str_starts_ci(v, "text/html") || browser_str_starts_ci(v, "application/xhtml")) return 1;
                v++;
            }
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    return browser_body_looks_html(body);
}

static void browser_copy_url(char *dst, uint32_t cap, const char *src) {
    uint32_t i = 0;
    if (!dst || cap == 0) return;
    if (!src) src = "";
    while (src[i] && i + 1u < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void browser_current_origin(char *origin, uint32_t cap) {
    char url[64];
    char host[64];
    char path[128];
    uint16_t port;
    browser_scheme_t scheme = BROWSER_SCHEME_HTTP;
    int pos = 0;
    uint32_t i;
    char portbuf[8];
    uint16_t default_port;
    if (!origin || cap == 0) return;
    origin[0] = '\0';
    browser_copy_url(url, sizeof(url), g_browser_address_box ? g_browser_address_box->text : "http://example.com/");
    if (browser_parse_url(url, host, sizeof(host), path, sizeof(path), &port, &scheme) != 0) return;
    default_port = (scheme == BROWSER_SCHEME_HTTPS) ? 443u : 80u;
    pos = fp_str_append(origin, pos, (int)cap, scheme == BROWSER_SCHEME_HTTPS ? "https://" : "http://");
    pos = fp_str_append(origin, pos, (int)cap, host);
    if (port != default_port) {
        for (i = 0; i < sizeof(portbuf); i++) portbuf[i] = '\0';
        i = sizeof(portbuf) - 1u;
        do {
            portbuf[--i] = (char)('0' + (port % 10u));
            port /= 10u;
        } while (port && i > 0);
        pos = fp_str_append(origin, pos, (int)cap, ":");
        pos = fp_str_append(origin, pos, (int)cap, &portbuf[i]);
    }
    (void)path;
}

static void browser_resolve_link_url(const char *href, char *out, uint32_t cap) {
    char origin[96];
    int pos = 0;
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!href || !*href || href[0] == '#' || browser_str_starts_ci(href, "mailto:")) return;
    if (browser_str_starts_ci(href, "http://") || browser_str_starts_ci(href, "https://")) {
        browser_copy_url(out, cap, href);
        return;
    }
    browser_current_origin(origin, sizeof(origin));
    if (!origin[0]) return;
    pos = fp_str_append(out, pos, (int)cap, origin);
    if (href[0] == '/') {
        pos = fp_str_append(out, pos, (int)cap, href);
    } else {
        pos = fp_str_append(out, pos, (int)cap, "/");
        pos = fp_str_append(out, pos, (int)cap, href);
    }
    (void)pos;
}

static const char *browser_find_attr_value(const char *tag, const char *attr) {
    uint32_t attr_len = (uint32_t)strlen(attr);
    const char *p = tag;
    while (p && *p && *p != '>') {
        if (browser_str_starts_ci(p, attr) && p[attr_len] == '=') {
            p += attr_len + 1u;
            if (*p == '\"' || *p == '\'') p++;
            return p;
        }
        p++;
    }
    return 0;
}

static void browser_copy_attr(char *dst, uint32_t cap, const char *value) {
    uint32_t i = 0;
    if (!dst || cap == 0) return;
    dst[0] = '\0';
    if (!value) return;
    while (value[i] && value[i] != '\"' && value[i] != '\'' && value[i] != '>' &&
           value[i] != ' ' && value[i] != '\t' && i + 1u < cap) {
        dst[i] = value[i];
        i++;
    }
    dst[i] = '\0';
}

static void browser_extract_anchor_text(const char *start, const char *end, char *out, uint32_t cap) {
    uint32_t n = 0;
    int in_tag = 0;
    if (!out || cap == 0) return;
    out[0] = '\0';
    while (start && start < end && *start && n + 1u < cap) {
        char ch = *start++;
        if (in_tag) {
            if (ch == '>') in_tag = 0;
            continue;
        }
        if (ch == '<') {
            in_tag = 1;
            continue;
        }
        if (ch == '&') ch = browser_decode_entity(&start);
        if (ch == '\r' || ch == '\n' || ch == '\t') ch = ' ';
        if ((unsigned char)ch < 32u) continue;
        out[n++] = ch;
    }
    out[n] = '\0';
}

static void browser_link_on_click(gui_widget_t *w, void *ud) {
    const char *url = (const char *)ud;
    (void)w;
    if (!url || !*url || !g_browser_address_box) return;
    browser_set_widget_text(g_browser_address_box, url);
    g_browser_address_box->cursor = (uint32_t)strlen(g_browser_address_box->text);
    browser_load_start();
}

static void browser_set_line_link(uint32_t line, const char *label, const char *url) {
    gui_widget_t *wg;
    if (line >= GUI_BROWSER_LINKS_MAX) return;
    wg = g_browser_content_lines[line];
    browser_copy_url(g_browser_line_links[line], sizeof(g_browser_line_links[line]), url);
    browser_set_widget_text(wg, label && *label ? label : url);
    if (wg) {
        wg->type = GUI_WIDGET_BUTTON;
        wg->on_click = browser_link_on_click;
        wg->user_data = (void *)g_browser_line_links[line];
        wg->bg_color = gui_rgb(236, 238, 244);
        wg->fg_color = gui_rgb(20, 90, 190);
    }
}

static uint32_t browser_render_links(const char *html, uint32_t start_line) {
    const char *p = html;
    uint32_t line = start_line;
    while (p && *p && line < GUI_BROWSER_CONTENT_LINES) {
        const char *a = p;
        const char *href_v;
        const char *tag_end;
        const char *close;
        char href[128];
        char url[128];
        char label[64];
        char display[64];
        int pos = 0;
        while (*a && !browser_str_starts_ci(a, "<a")) a++;
        if (!*a) break;
        tag_end = a;
        while (*tag_end && *tag_end != '>') tag_end++;
        if (*tag_end != '>') break;
        href_v = browser_find_attr_value(a, "href");
        if (!href_v || href_v > tag_end) {
            p = tag_end + 1;
            continue;
        }
        browser_copy_attr(href, sizeof(href), href_v);
        browser_resolve_link_url(href, url, sizeof(url));
        close = tag_end + 1;
        while (*close && !browser_str_starts_ci(close, "</a")) close++;
        browser_extract_anchor_text(tag_end + 1, close, label, sizeof(label));
        if (url[0]) {
            pos = fp_str_append(display, pos, sizeof(display), "> ");
            pos = fp_str_append(display, pos, sizeof(display), label[0] ? label : url);
            (void)pos;
            browser_set_line_link(line++, display, url);
        }
        p = *close ? close + 3 : tag_end + 1;
    }
    return line;
}

static uint32_t browser_render_body_text_at(const char *body, uint32_t start_line) {
    if (!body || !*body) {
        if (start_line < GUI_BROWSER_CONTENT_LINES) browser_set_widget_text(g_browser_content_lines[start_line++], "HTTP response has no body.");
        return start_line;
    }
    return browser_render_text_at(body, start_line);
}

static uint32_t browser_render_body_at(const char *body, uint32_t start_line) {
    static char html_text[1024];
    if (!body || !*body) return browser_render_body_text_at(body, start_line);
    if (browser_body_looks_html(body)) {
        browser_html_to_text(body, html_text, sizeof(html_text));
        return browser_render_body_text_at(html_text, start_line);
    }
    return browser_render_body_text_at(body, start_line);
}

static void browser_render_body(const char *body) {
    browser_clear_content();
    (void)browser_render_body_at(body, 0);
}

static int browser_header_name_eq(const char *p, const char *name) {
    uint32_t i = 0;
    if (!p || !name) return 0;
    while (name[i]) {
        char a = p[i];
        char b = name[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
        i++;
    }
    return p[i] == ':';
}

static void browser_copy_header_line(char *out, uint32_t cap, const char *line) {
    uint32_t i = 0;
    if (!out || cap == 0) return;
    if (!line) {
        out[0] = '\0';
        return;
    }
    while (line[i] && line[i] != '\r' && line[i] != '\n' && i + 1u < cap) {
        out[i] = line[i];
        i++;
    }
    out[i] = '\0';
}

static uint32_t browser_render_response_summary(char *response, const char *body) {
    const char *p;
    uint32_t line = 0;
    char status[64];
    char header[64];
    browser_clear_content();
    if (browser_response_is_html(response, body)) {
        static char html_text[1024];
        browser_html_to_text(body, html_text, sizeof(html_text));
        line = browser_render_body_text_at(html_text, line);
        return browser_render_links(body, line);
    }
    browser_copy_header_line(status, sizeof(status), response);
    if (status[0]) browser_set_widget_text(g_browser_content_lines[line++], status);
    p = response;
    while (*p && *p != '\n') p++;
    if (*p == '\n') p++;
    while (*p && p < body && line + 1u < GUI_BROWSER_CONTENT_LINES) {
        if (*p == '\r' || *p == '\n') break;
        if (browser_header_name_eq(p, "Content-Type") ||
            browser_header_name_eq(p, "Content-Length") ||
            browser_header_name_eq(p, "Location") ||
            browser_header_name_eq(p, "Server")) {
            browser_copy_header_line(header, sizeof(header), p);
            browser_set_widget_text(g_browser_content_lines[line++], header);
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    if (line < GUI_BROWSER_CONTENT_LINES) browser_set_widget_text(g_browser_content_lines[line++], "");
    return browser_render_body_at(body, line);
}

static const char *browser_find_body(char *response) {
    char *p;
    if (!response) return "";
    for (p = response; p[0] && p[1] && p[2] && p[3]; p++) {
        if (p[0] == '\r' && p[1] == '\n' && p[2] == '\r' && p[3] == '\n') return p + 4;
        if (p[0] == '\n' && p[1] == '\n') return p + 2;
    }
    return response;
}


static int browser_tls_append_u8(uint8_t *buf, uint32_t cap, uint32_t *pos, uint8_t v) {
    if (!buf || !pos || *pos >= cap) return -1;
    buf[(*pos)++] = v;
    return 0;
}

static int browser_tls_append_u16(uint8_t *buf, uint32_t cap, uint32_t *pos, uint16_t v) {
    if (browser_tls_append_u8(buf, cap, pos, (uint8_t)(v >> 8)) != 0) return -1;
    return browser_tls_append_u8(buf, cap, pos, (uint8_t)(v & 0xffu));
}

static int browser_tls_append_bytes(uint8_t *buf, uint32_t cap, uint32_t *pos, const uint8_t *src, uint32_t len) {
    uint32_t i;
    if (!buf || !pos || !src || *pos + len > cap) return -1;
    for (i = 0; i < len; i++) buf[(*pos)++] = src[i];
    return 0;
}

static int browser_tls_build_client_hello(const char *host, uint8_t *out, uint32_t cap) {
    static const uint8_t ciphers[] = {
        0xc0, 0x2f, /* TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256 */
        0xc0, 0x30, /* TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384 */
        0xc0, 0x13, /* TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA */
        0xc0, 0x14, /* TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA */
        0x00, 0x9c, /* TLS_RSA_WITH_AES_128_GCM_SHA256 */
        0x00, 0x9d, /* TLS_RSA_WITH_AES_256_GCM_SHA384 */
        0x00, 0x2f, /* TLS_RSA_WITH_AES_128_CBC_SHA */
        0x00, 0xff  /* TLS_EMPTY_RENEGOTIATION_INFO_SCSV */
    };
    static const uint8_t groups[] = {0x00, 0x17, 0x00, 0x18, 0x00, 0x19};
    static const uint8_t ec_points[] = {0x00};
    static const uint8_t sigalgs[] = {0x04, 0x01, 0x05, 0x01, 0x02, 0x01, 0x04, 0x03, 0x05, 0x03};
    static const uint8_t alpn_http11[] = {0x00, 0x08, 0x07, 'h', 't', 't', 'p', '/', '1', '.', '1'};
    uint32_t p = 0;
    uint32_t record_len_pos;
    uint32_t handshake_len_pos;
    uint32_t handshake_start;
    uint32_t ext_len_pos;
    uint32_t ext_start;
    uint32_t sni_len = 0;
    uint32_t i;
    uint32_t seed = sched_time_ms();
    while (host && host[sni_len] && sni_len < 63u) sni_len++;
    if (!out || cap < 128u || sni_len == 0u) return -1;

    if (browser_tls_append_u8(out, cap, &p, 0x16) != 0) return -1;
    if (browser_tls_append_u16(out, cap, &p, 0x0301) != 0) return -1;
    record_len_pos = p;
    if (browser_tls_append_u16(out, cap, &p, 0) != 0) return -1;

    if (browser_tls_append_u8(out, cap, &p, 0x01) != 0) return -1;
    handshake_len_pos = p;
    if (browser_tls_append_u8(out, cap, &p, 0) != 0) return -1;
    if (browser_tls_append_u8(out, cap, &p, 0) != 0) return -1;
    if (browser_tls_append_u8(out, cap, &p, 0) != 0) return -1;
    handshake_start = p;

    if (browser_tls_append_u16(out, cap, &p, 0x0303) != 0) return -1;
    for (i = 0; i < 32u; i++) {
        seed = seed * 1103515245u + 12345u + i;
        if (browser_tls_append_u8(out, cap, &p, (uint8_t)(seed >> 16)) != 0) return -1;
    }
    if (browser_tls_append_u8(out, cap, &p, 0) != 0) return -1;
    if (browser_tls_append_u16(out, cap, &p, (uint16_t)sizeof(ciphers)) != 0) return -1;
    if (browser_tls_append_bytes(out, cap, &p, ciphers, sizeof(ciphers)) != 0) return -1;
    if (browser_tls_append_u8(out, cap, &p, 1) != 0) return -1;
    if (browser_tls_append_u8(out, cap, &p, 0) != 0) return -1;

    ext_len_pos = p;
    if (browser_tls_append_u16(out, cap, &p, 0) != 0) return -1;
    ext_start = p;

    if (browser_tls_append_u16(out, cap, &p, 0x0000) != 0) return -1;
    if (browser_tls_append_u16(out, cap, &p, (uint16_t)(5u + sni_len)) != 0) return -1;
    if (browser_tls_append_u16(out, cap, &p, (uint16_t)(3u + sni_len)) != 0) return -1;
    if (browser_tls_append_u8(out, cap, &p, 0x00) != 0) return -1;
    if (browser_tls_append_u16(out, cap, &p, (uint16_t)sni_len) != 0) return -1;
    if (browser_tls_append_bytes(out, cap, &p, (const uint8_t *)host, sni_len) != 0) return -1;

    if (browser_tls_append_u16(out, cap, &p, 0x000a) != 0) return -1;
    if (browser_tls_append_u16(out, cap, &p, (uint16_t)(2u + sizeof(groups))) != 0) return -1;
    if (browser_tls_append_u16(out, cap, &p, (uint16_t)sizeof(groups)) != 0) return -1;
    if (browser_tls_append_bytes(out, cap, &p, groups, sizeof(groups)) != 0) return -1;

    if (browser_tls_append_u16(out, cap, &p, 0x000b) != 0) return -1;
    if (browser_tls_append_u16(out, cap, &p, (uint16_t)(1u + sizeof(ec_points))) != 0) return -1;
    if (browser_tls_append_u8(out, cap, &p, (uint8_t)sizeof(ec_points)) != 0) return -1;
    if (browser_tls_append_bytes(out, cap, &p, ec_points, sizeof(ec_points)) != 0) return -1;

    if (browser_tls_append_u16(out, cap, &p, 0x000d) != 0) return -1;
    if (browser_tls_append_u16(out, cap, &p, (uint16_t)(2u + sizeof(sigalgs))) != 0) return -1;
    if (browser_tls_append_u16(out, cap, &p, (uint16_t)sizeof(sigalgs)) != 0) return -1;
    if (browser_tls_append_bytes(out, cap, &p, sigalgs, sizeof(sigalgs)) != 0) return -1;

    if (browser_tls_append_u16(out, cap, &p, 0x0010) != 0) return -1;
    if (browser_tls_append_u16(out, cap, &p, (uint16_t)sizeof(alpn_http11)) != 0) return -1;
    if (browser_tls_append_bytes(out, cap, &p, alpn_http11, sizeof(alpn_http11)) != 0) return -1;

    if (browser_tls_append_u16(out, cap, &p, 0x0016) != 0) return -1;
    if (browser_tls_append_u16(out, cap, &p, 0) != 0) return -1;

    if (browser_tls_append_u16(out, cap, &p, 0x0017) != 0) return -1;
    if (browser_tls_append_u16(out, cap, &p, 0) != 0) return -1;

    if (browser_tls_append_u16(out, cap, &p, 0xff01) != 0) return -1;
    if (browser_tls_append_u16(out, cap, &p, 1) != 0) return -1;
    if (browser_tls_append_u8(out, cap, &p, 0) != 0) return -1;

    if (browser_tls_append_u16(out, cap, &p, 0x002b) != 0) return -1;
    if (browser_tls_append_u16(out, cap, &p, 3) != 0) return -1;
    if (browser_tls_append_u8(out, cap, &p, 2) != 0) return -1;
    if (browser_tls_append_u16(out, cap, &p, 0x0303) != 0) return -1;

    out[ext_len_pos] = (uint8_t)((p - ext_start) >> 8);
    out[ext_len_pos + 1u] = (uint8_t)((p - ext_start) & 0xffu);
    out[handshake_len_pos] = (uint8_t)((p - handshake_start) >> 16);
    out[handshake_len_pos + 1u] = (uint8_t)((p - handshake_start) >> 8);
    out[handshake_len_pos + 2u] = (uint8_t)((p - handshake_start) & 0xffu);
    out[record_len_pos] = (uint8_t)((p - 5u) >> 8);
    out[record_len_pos + 1u] = (uint8_t)((p - 5u) & 0xffu);
    return (int)p;
}

static void browser_append_hex4(char *dst, int *pos, int cap, uint16_t v) {
    static const char hex[] = "0123456789ABCDEF";
    char tmp[5];
    tmp[0] = hex[(v >> 12) & 0xfu];
    tmp[1] = hex[(v >> 8) & 0xfu];
    tmp[2] = hex[(v >> 4) & 0xfu];
    tmp[3] = hex[v & 0xfu];
    tmp[4] = '\0';
    *pos = fp_str_append(dst, *pos, cap, tmp);
}

static void browser_render_https_probe(const char *host, const uint8_t *record, uint32_t len) {
    char line[160];
    int pos;
    tls_parser_summary_t summary;
    int parsed_records;
    uint8_t i;

    parsed_records = tls_parse_records(record, len, &summary);
    browser_clear_content();
    browser_set_widget_text(g_browser_content_lines[0], "HTTPS/TLS handshake summary.");

    pos = 0;
    line[0] = '\0';
    pos = fp_str_append(line, pos, sizeof(line), "Host: ");
    pos = fp_str_append(line, pos, sizeof(line), host ? host : "");
    browser_set_widget_text(g_browser_content_lines[1], line);

    pos = 0;
    line[0] = '\0';
    pos = fp_str_append(line, pos, sizeof(line), "TLS records parsed: ");
    pos = gui_append_uint(line, pos, sizeof(line), parsed_records > 0 ? (uint32_t)parsed_records : 0u);
    pos = fp_str_append(line, pos, sizeof(line), " type: ");
    pos = fp_str_append(line, pos, sizeof(line), tls_record_type_name(summary.record_type));
    browser_set_widget_text(g_browser_content_lines[2], line);

    pos = 0;
    line[0] = '\0';
    pos = fp_str_append(line, pos, sizeof(line), "Record TLS version: 0x");
    browser_append_hex4(line, &pos, sizeof(line), summary.record_version);
    pos = fp_str_append(line, pos, sizeof(line), " length: ");
    pos = gui_append_uint(line, pos, sizeof(line), summary.record_length);
    browser_set_widget_text(g_browser_content_lines[3], line);

    pos = 0;
    line[0] = '\0';
    pos = fp_str_append(line, pos, sizeof(line), "Handshake list: ");
    for (i = 0; i < summary.handshake_count && i < TLS_PARSER_MAX_HANDSHAKES; i++) {
        if (i > 0) pos = fp_str_append(line, pos, sizeof(line), ", ");
        pos = fp_str_append(line, pos, sizeof(line), tls_handshake_type_name(summary.handshake_types[i]));
    }
    if (summary.handshake_count == 0u) pos = fp_str_append(line, pos, sizeof(line), "none");
    browser_set_widget_text(g_browser_content_lines[4], line);

    pos = 0;
    line[0] = '\0';
    pos = fp_str_append(line, pos, sizeof(line), "Server: 0x");
    browser_append_hex4(line, &pos, sizeof(line), summary.server_version);
    pos = fp_str_append(line, pos, sizeof(line), " cipher: 0x");
    browser_append_hex4(line, &pos, sizeof(line), summary.cipher_suite);
    pos = fp_str_append(line, pos, sizeof(line), " extLen: ");
    pos = gui_append_uint(line, pos, sizeof(line), summary.extensions_length);
    browser_set_widget_text(g_browser_content_lines[5], line);

    pos = 0;
    line[0] = '\0';
    pos = fp_str_append(line, pos, sizeof(line), "Certificates: ");
    pos = gui_append_uint(line, pos, sizeof(line), summary.certificate_count);
    pos = fp_str_append(line, pos, sizeof(line), " bytes: ");
    pos = gui_append_uint(line, pos, sizeof(line), summary.certificate_bytes);
    browser_set_widget_text(g_browser_content_lines[6], line);

    pos = 0;
    line[0] = '\0';
    pos = fp_str_append(line, pos, sizeof(line), "ECDHE: curveType ");
    pos = gui_append_uint(line, pos, sizeof(line), summary.key_exchange_curve_type);
    pos = fp_str_append(line, pos, sizeof(line), " namedCurve 0x");
    browser_append_hex4(line, &pos, sizeof(line), summary.key_exchange_named_curve);
    pos = fp_str_append(line, pos, sizeof(line), " pubKeyLen ");
    pos = gui_append_uint(line, pos, sizeof(line), summary.key_exchange_public_key_length);
    browser_set_widget_text(g_browser_content_lines[7], line);

    pos = 0;
    line[0] = '\0';
    pos = fp_str_append(line, pos, sizeof(line), "ECDHE signature: alg 0x");
    browser_append_hex4(line, &pos, sizeof(line), summary.key_exchange_signature_algorithm);
    pos = fp_str_append(line, pos, sizeof(line), " sigLen ");
    pos = gui_append_uint(line, pos, sizeof(line), summary.key_exchange_signature_length);
    browser_set_widget_text(g_browser_content_lines[8], line);

    if (summary.alert_level || summary.alert_description) {
        pos = 0;
        line[0] = '\0';
        pos = fp_str_append(line, pos, sizeof(line), "TLS alert level: ");
        pos = gui_append_uint(line, pos, sizeof(line), summary.alert_level);
        pos = fp_str_append(line, pos, sizeof(line), " description: ");
        pos = gui_append_uint(line, pos, sizeof(line), summary.alert_description);
        browser_set_widget_text(g_browser_content_lines[9], line);
    } else {
        browser_set_widget_text(g_browser_content_lines[9], "Next: derive ECDHE secret and TLS traffic keys.");
    }
    browser_set_widget_text(g_browser_content_lines[10], "HTTPS pages still need cipher/decrypt + Finished verify.");
}

static void browser_load_reset_connection(void) {
    if (g_browser_load.state != BROWSER_LOAD_IDLE && g_browser_load.conn >= 0) {
        net_tcp_close(g_browser_load.conn);
    }
    g_browser_load.conn = -1;
}

static void browser_load_finish(const char *status) {
    browser_load_reset_connection();
    g_browser_load.state = BROWSER_LOAD_IDLE;
    if (status) browser_set_status(status);
}

static int browser_load_timed_out(uint32_t timeout_ms) {
    return (uint32_t)(sched_time_ms() - g_browser_load.state_started_ms) >= timeout_ms;
}

static void browser_load_set_state(browser_load_state_t state) {
    g_browser_load.state = state;
    g_browser_load.state_started_ms = sched_time_ms();
}

static void browser_load_begin_connect(void) {
    uint32_t local_port = (g_browser_load.scheme == BROWSER_SCHEME_HTTPS ? 45000u : 43000u) +
                          (sched_time_ms() % 2000u);
    g_browser_load.conn = net_tcp_open(0, (uint16_t)local_port,
                                       g_browser_load.ip, g_browser_load.port, 1);
    if (g_browser_load.conn < 0) {
        browser_set_widget_text(g_browser_content_lines[0],
                                g_browser_load.scheme == BROWSER_SCHEME_HTTPS ?
                                "HTTPS TCP connection failed." : "TCP connection failed.");
        browser_load_finish("Connection failed");
        return;
    }
    browser_load_set_state(BROWSER_LOAD_CONNECTING);
}

static void browser_load_start(void) {
    int parse_result;

    browser_load_reset_connection();
    memset(&g_browser_load, 0, sizeof(g_browser_load));
    g_browser_load.conn = -1;

    browser_clear_content();
    browser_set_status("Loading...");
    browser_copy_url(g_browser_load.url, sizeof(g_browser_load.url),
                     g_browser_address_box ? g_browser_address_box->text : "http://example.com/");
    parse_result = browser_parse_url(g_browser_load.url,
                                     g_browser_load.host, sizeof(g_browser_load.host),
                                     g_browser_load.path, sizeof(g_browser_load.path),
                                     &g_browser_load.port, &g_browser_load.scheme);
    if (parse_result != 0) {
        browser_set_widget_text(g_browser_content_lines[0], "Invalid URL. Use http://host/path or https://host/path.");
        browser_load_finish("Invalid URL");
        return;
    }

    if (net_parse_ipv4(g_browser_load.host, &g_browser_load.ip) == 0) {
        browser_load_begin_connect();
        return;
    }
    if (dns_query_a(g_browser_load.host) != 0) {
        browser_set_widget_text(g_browser_content_lines[0], "DNS lookup failed or host is unreachable.");
        browser_load_finish("DNS failed");
        return;
    }
    browser_load_set_state(BROWSER_LOAD_RESOLVING);
}

static void browser_load_send_http(void) {
    int pos = 0;
    g_browser_load.request[0] = '\0';
    pos = fp_str_append(g_browser_load.request, pos, sizeof(g_browser_load.request), "GET ");
    pos = fp_str_append(g_browser_load.request, pos, sizeof(g_browser_load.request), g_browser_load.path);
    pos = fp_str_append(g_browser_load.request, pos, sizeof(g_browser_load.request), " HTTP/1.0\r\nHost: ");
    pos = fp_str_append(g_browser_load.request, pos, sizeof(g_browser_load.request), g_browser_load.host);
    pos = fp_str_append(g_browser_load.request, pos, sizeof(g_browser_load.request),
                        "\r\nConnection: close\r\nUser-Agent: OpenOSBrowser/0.1\r\n\r\n");
    (void)pos;
    if (net_tcp_send(g_browser_load.conn, (const uint8_t *)g_browser_load.request,
                     (uint16_t)strlen(g_browser_load.request)) != 0) {
        browser_set_widget_text(g_browser_content_lines[0], "Failed to send HTTP request.");
        browser_load_finish("Send failed");
        return;
    }
    g_browser_load.total = 0;
    browser_load_set_state(BROWSER_LOAD_HTTP_RECV);
}

static void browser_load_send_tls(void) {
    g_browser_load.tls_hello_len = browser_tls_build_client_hello(g_browser_load.host,
                                                                  g_browser_load.tls_hello,
                                                                  sizeof(g_browser_load.tls_hello));
    if (g_browser_load.tls_hello_len <= 0) {
        browser_set_widget_text(g_browser_content_lines[0], "Failed to build TLS ClientHello.");
        browser_load_finish("TLS setup failed");
        return;
    }
    if (net_tcp_send(g_browser_load.conn, g_browser_load.tls_hello,
                     (uint16_t)g_browser_load.tls_hello_len) != 0) {
        browser_set_widget_text(g_browser_content_lines[0], "Failed to send TLS ClientHello.");
        browser_load_finish("TLS send failed");
        return;
    }
    browser_load_set_state(BROWSER_LOAD_TLS_RECV);
}

void browser_load_tick(void) {
    int state;
    int got;

    if (g_browser_load.state == BROWSER_LOAD_IDLE) return;
    net_poll();

    if (g_browser_load.state == BROWSER_LOAD_RESOLVING) {
        dns_state_t dns_state = dns_get_state();
        if (dns_state == DNS_STATE_RESOLVED) {
            g_browser_load.ip = dns_get_last_result();
            if (!g_browser_load.ip) {
                browser_set_widget_text(g_browser_content_lines[0], "DNS lookup returned no address.");
                browser_load_finish("DNS failed");
                return;
            }
            browser_load_begin_connect();
            return;
        }
        if (dns_state == DNS_STATE_FAILED || browser_load_timed_out(3000u)) {
            browser_set_widget_text(g_browser_content_lines[0], "DNS lookup failed or timed out.");
            browser_load_finish("DNS failed");
        }
        return;
    }

    if (g_browser_load.state == BROWSER_LOAD_CONNECTING) {
        state = net_tcp_state(g_browser_load.conn);
        if (state == NET_TCP_STATE_ESTABLISHED) {
            browser_load_set_state(g_browser_load.scheme == BROWSER_SCHEME_HTTPS ?
                                   BROWSER_LOAD_TLS_SEND : BROWSER_LOAD_HTTP_SEND);
        } else if (state < 0 || state == NET_TCP_STATE_CLOSED || browser_load_timed_out(5000u)) {
            browser_set_widget_text(g_browser_content_lines[0],
                                    g_browser_load.scheme == BROWSER_SCHEME_HTTPS ?
                                    "HTTPS TCP connection failed or timed out." :
                                    "TCP connection failed or timed out.");
            browser_load_finish("Connection failed");
        }
        return;
    }

    if (g_browser_load.state == BROWSER_LOAD_HTTP_SEND) {
        browser_load_send_http();
        return;
    }

    if (g_browser_load.state == BROWSER_LOAD_HTTP_RECV) {
        got = net_tcp_recv(g_browser_load.conn,
                           g_browser_load.response + g_browser_load.total,
                           (uint16_t)(sizeof(g_browser_load.response) - 1u - (uint32_t)g_browser_load.total));
        if (got > 0) {
            g_browser_load.total += got;
            g_browser_load.state_started_ms = sched_time_ms();
            if (g_browser_load.total >= (int)sizeof(g_browser_load.response) - 1) {
                g_browser_load.response[g_browser_load.total] = 0;
                (void)browser_render_response_summary((char *)g_browser_load.response,
                                                       browser_find_body((char *)g_browser_load.response));
                browser_load_finish("Done");
            }
            return;
        }
        state = net_tcp_state(g_browser_load.conn);
        if (state == NET_TCP_STATE_CLOSED || state == NET_TCP_STATE_CLOSE_WAIT) {
            g_browser_load.response[g_browser_load.total] = 0;
            if (g_browser_load.total <= 0) {
                browser_clear_content();
                browser_set_widget_text(g_browser_content_lines[0], "No HTTP response received before connection closed.");
                browser_load_finish("Timeout");
            } else {
                (void)browser_render_response_summary((char *)g_browser_load.response,
                                                       browser_find_body((char *)g_browser_load.response));
                browser_load_finish("Done");
            }
            return;
        }
        if (browser_load_timed_out(7000u)) {
            browser_clear_content();
            browser_set_widget_text(g_browser_content_lines[0], "No HTTP response received before timeout.");
            browser_load_finish("Timeout");
        }
        return;
    }

    if (g_browser_load.state == BROWSER_LOAD_TLS_SEND) {
        browser_load_send_tls();
        return;
    }

    if (g_browser_load.state == BROWSER_LOAD_TLS_RECV) {
        got = net_tcp_recv(g_browser_load.conn, g_browser_load.tls_record, sizeof(g_browser_load.tls_record));
        if (got > 0) {
            browser_render_https_probe(g_browser_load.host, g_browser_load.tls_record, (uint32_t)got);
            browser_load_finish("HTTPS TLS response received");
            return;
        }
        state = net_tcp_state(g_browser_load.conn);
        if (state == NET_TCP_STATE_CLOSED || state == NET_TCP_STATE_CLOSE_WAIT || browser_load_timed_out(6000u)) {
            browser_set_widget_text(g_browser_content_lines[0], "No TLS response received.");
            browser_load_finish("TLS no response");
        }
    }
}

/* ============================================================
 * gui_nettool：终端网络工具异步状态机（ping/nslookup/wget）
 * 不走 launch_path 同步阻塞，由 GUI 主循环每帧 tick 驱动，不卡界面。
 * ============================================================ */
/* nt_tool_t 已在文件前部前置定义 */

typedef enum {
    NT_ST_IDLE = 0,
    NT_ST_RESOLVING,   /* 等 DNS */
    NT_ST_PING_WAIT,   /* 等 ping 回包 */
    NT_ST_CONNECTING,  /* 等 TCP ESTABLISHED */
    NT_ST_SENDING,     /* 发 HTTP 请求 */
    NT_ST_RECV,        /* 收 HTTP 响应 */
    NT_ST_DONE,
} nt_state_t;

static struct {
    int          active;
    nt_tool_t    tool;
    nt_state_t   state;
    char         host[128];   /* 目标主机名或 IP 字符串 */
    char         path[128];   /* wget 路径 */
    uint32_t     ip;          /* 解析后的目标 IP */
    int          conn;        /* TCP 句柄 */
    uint32_t     start_ticks; /* 启动时间（net_ticks_ms 基准） */
    int          ping_count;  /* 已发 ping 次数 */
    int          ping_ok;     /* 成功回应次数 */
    uint32_t     recv_total;  /* wget 已收字节 */
} g_nt;

/* 终端命令执行后，异步工具未完成时提示符不能立即显示；
 * 由状态机 DONE 时回调 gui_terminal_show_prompt（定义在前面）。 */

int  gui_nettool_active(void) { return g_nt.active; }

static void nt_print_ip(uint32_t ip) {
    char buf[16];
    unsigned a = (ip >> 24) & 0xFF, b = (ip >> 16) & 0xFF;
    unsigned c = (ip >> 8) & 0xFF, d = ip & 0xFF;
    int n = 0;
    /* 手写 IP 格式化 */
    char tmp[4];
    unsigned parts[4] = { a, b, c, d };
    for (int p = 0; p < 4; p++) {
        unsigned v = parts[p]; int ti = 0;
        if (v == 0) tmp[ti++] = '0';
        else { char rev[3]; int ri = 0; while (v) { rev[ri++] = (char)('0'+v%10); v/=10; } while (ri) tmp[ti++]=rev[--ri]; }
        for (int k = 0; k < ti; k++) buf[n++] = tmp[k];
        if (p < 3) buf[n++] = '.';
    }
    buf[n] = 0;
    gui_terminal_write(buf);
}

static void nt_finish(const char *msg) {
    if (msg) { gui_terminal_write(msg); gui_terminal_write("\n"); }
    if (g_nt.conn >= 0) { net_tcp_close(g_nt.conn); g_nt.conn = -1; }
    g_nt.active = 0;
    g_nt.state = NT_ST_IDLE;
    g_nt.tool = NT_TOOL_NONE;
    gui_terminal_show_prompt();
}

static void browser_on_nav(gui_widget_t *w, void *ud) {
    (void)w;
    (void)ud;
    browser_load_start();
}

/* 已经过去多少 ms（sched_time_ms 基准） */
static int nt_timed_out(uint32_t ms) {
    return (sched_time_ms() - g_nt.start_ticks) >= ms;
}

/* 启动一个网络工具任务。tool=工具类型，args=第一个参数（主机），arg2=路径（wget 可选）
 * 返回 0=已启动（后续 tick 驱动，提示符延后显示），-1=参数错误（已即时处理完） */
int gui_nettool_start(nt_tool_t tool, const char *host, const char *path2) {
    memset(&g_nt, 0, sizeof(g_nt));
    g_nt.conn = -1;
    g_nt.tool = tool;
    g_nt.start_ticks = sched_time_ms();

    if (!host || !host[0]) {
        gui_terminal_write("usage: need a host argument\n");
        return -1;
    }
    /* 拷贝主机名 */
    int i = 0;
    while (host[i] && i < (int)sizeof(g_nt.host) - 1) { g_nt.host[i] = host[i]; i++; }
    g_nt.host[i] = 0;
    /* 路径（wget） */
    if (path2 && path2[0]) {
        int j = 0;
        while (path2[j] && j < (int)sizeof(g_nt.path) - 1) { g_nt.path[j] = path2[j]; j++; }
        g_nt.path[j] = 0;
    } else {
        g_nt.path[0] = '/'; g_nt.path[1] = 0;
    }

    if (net_dhcp_state() != 3) {
        gui_terminal_write("network is not up (no DHCP lease?)\n");
        return -1;
    }

    g_nt.active = 1;

    /* 先尝试把 host 当作 IP 直接解析；否则发 DNS 查询 */
    if (net_parse_ipv4(g_nt.host, &g_nt.ip) == 0) {
        /* 直接是 IP */
        if (tool == NT_TOOL_NSLOOKUP) {
            gui_terminal_write("Name:    ");
            gui_terminal_write(g_nt.host);
            gui_terminal_write("\nAddress: ");
            nt_print_ip(g_nt.ip);
            gui_terminal_write("\n");
            nt_finish(0);
            return 0;
        }
        if (tool == NT_TOOL_PING) {
            gui_terminal_write("PING ");
            gui_terminal_write(g_nt.host);
            gui_terminal_write("\n");
            net_ping_start(g_nt.ip);
            g_nt.ping_count = 1;
            g_nt.state = NT_ST_PING_WAIT;
            g_nt.start_ticks = sched_time_ms();
            return 0;
        }
        /* wget */
        g_nt.state = NT_ST_CONNECTING;
        {
            uint32_t lport = 43000u + (sched_time_ms() % 2000u);
            g_nt.conn = net_tcp_open(0, (uint16_t)lport, g_nt.ip, 80, 1);
        }
        if (g_nt.conn < 0) { nt_finish("wget: TCP open failed"); return 0; }
        g_nt.start_ticks = sched_time_ms();
        return 0;
    }

    /* 需要 DNS 解析 */
    if (dns_query_a(g_nt.host) != 0) {
        nt_finish("DNS query failed");
        return 0;
    }
    if (tool == NT_TOOL_NSLOOKUP) {
        gui_terminal_write("Resolving ");
        gui_terminal_write(g_nt.host);
        gui_terminal_write(" ...\n");
    }
    g_nt.state = NT_ST_RESOLVING;
    g_nt.start_ticks = sched_time_ms();
    return 0;
}

/* 向 wget 连接发送 HTTP/1.0 GET 请求 */
static void nt_send_http_get(void) {
    static char req[512];
    int pos = 0;
    req[0] = 0;
    pos = fp_str_append(req, pos, sizeof(req), "GET ");
    pos = fp_str_append(req, pos, sizeof(req), g_nt.path);
    pos = fp_str_append(req, pos, sizeof(req), " HTTP/1.0\r\nHost: ");
    pos = fp_str_append(req, pos, sizeof(req), g_nt.host);
    pos = fp_str_append(req, pos, sizeof(req),
                        "\r\nConnection: close\r\nUser-Agent: OpenOSwget/0.1\r\n\r\n");
    (void)pos;
    if (net_tcp_send(g_nt.conn, (const uint8_t *)req, (uint16_t)strlen(req)) != 0) {
        nt_finish("wget: send failed");
        return;
    }
    g_nt.recv_total = 0;
    g_nt.state = NT_ST_RECV;
    g_nt.start_ticks = sched_time_ms();
}

/* GUI 主循环每帧调用：推进网络工具状态机（不阻塞） */
void gui_nettool_tick(void) {
    if (!g_nt.active) return;
    net_poll();

    switch (g_nt.state) {
    case NT_ST_RESOLVING: {
        dns_state_t ds = dns_get_state();
        if (ds == DNS_STATE_RESOLVED) {
            g_nt.ip = dns_get_last_result();
            if (!g_nt.ip) { nt_finish("DNS: no address"); return; }
            if (g_nt.tool == NT_TOOL_NSLOOKUP) {
                gui_terminal_write("Name:    ");
                gui_terminal_write(g_nt.host);
                gui_terminal_write("\nAddress: ");
                nt_print_ip(g_nt.ip);
                gui_terminal_write("\n");
                nt_finish(0);
                return;
            }
            if (g_nt.tool == NT_TOOL_PING) {
                gui_terminal_write("PING ");
                gui_terminal_write(g_nt.host);
                gui_terminal_write(" (");
                nt_print_ip(g_nt.ip);
                gui_terminal_write(")\n");
                net_ping_start(g_nt.ip);
                g_nt.ping_count = 1;
                g_nt.state = NT_ST_PING_WAIT;
                g_nt.start_ticks = sched_time_ms();
                return;
            }
            /* wget */
            {
                uint32_t lport = 43000u + (sched_time_ms() % 2000u);
                g_nt.conn = net_tcp_open(0, (uint16_t)lport, g_nt.ip, 80, 1);
            }
            if (g_nt.conn < 0) { nt_finish("wget: TCP open failed"); return; }
            g_nt.state = NT_ST_CONNECTING;
            g_nt.start_ticks = sched_time_ms();
            return;
        }
        if (ds == DNS_STATE_FAILED || nt_timed_out(3000u)) {
            nt_finish("DNS lookup failed or timed out");
        }
        return;
    }

    case NT_ST_PING_WAIT: {
        int r = net_ping_poll();
        if (r == 1) {
            g_nt.ping_ok++;
            gui_terminal_write("reply from ");
            nt_print_ip(g_nt.ip);
            gui_terminal_write(": icmp_seq=");
            char sb[8]; int si = 0; int v = g_nt.ping_count;
            if (v == 0) sb[si++]='0'; else { char rev[8]; int ri=0; while(v){rev[ri++]=(char)('0'+v%10);v/=10;} while(ri) sb[si++]=rev[--ri]; }
            sb[si]=0; gui_terminal_write(sb);
            gui_terminal_write(" ok\n");
            if (g_nt.ping_count >= 4) {
                gui_terminal_write("--- ping done: ");
                char cb[8]; int ci=0; v=g_nt.ping_ok;
                if (v==0) cb[ci++]='0'; else { char rev[8]; int ri=0; while(v){rev[ri++]=(char)('0'+v%10);v/=10;} while(ri) cb[ci++]=rev[--ri]; }
                cb[ci]=0; gui_terminal_write(cb);
                gui_terminal_write("/4 replies ---\n");
                nt_finish(0);
                return;
            }
            /* 发下一包 */
            g_nt.ping_count++;
            net_ping_start(g_nt.ip);
            g_nt.start_ticks = sched_time_ms();
            return;
        }
        if (r < 0) {
            gui_terminal_write("request timeout icmp_seq=");
            char sb[8]; int si=0; int v=g_nt.ping_count;
            if (v==0) sb[si++]='0'; else { char rev[8]; int ri=0; while(v){rev[ri++]=(char)('0'+v%10);v/=10;} while(ri) sb[si++]=rev[--ri]; }
            sb[si]=0; gui_terminal_write(sb); gui_terminal_write("\n");
            if (g_nt.ping_count >= 4) {
                gui_terminal_write("--- ping done: ");
                char cb[8]; int ci=0; v=g_nt.ping_ok;
                if (v==0) cb[ci++]='0'; else { char rev[8]; int ri=0; while(v){rev[ri++]=(char)('0'+v%10);v/=10;} while(ri) cb[ci++]=rev[--ri]; }
                cb[ci]=0; gui_terminal_write(cb);
                gui_terminal_write("/4 replies ---\n");
                nt_finish(0);
                return;
            }
            g_nt.ping_count++;
            net_ping_start(g_nt.ip);
            g_nt.start_ticks = sched_time_ms();
            return;
        }
        return;   /* 进行中 */
    }

    case NT_ST_CONNECTING: {
        int st = net_tcp_state(g_nt.conn);
        if (st == NET_TCP_STATE_ESTABLISHED) {
            nt_send_http_get();
        } else if (st < 0 || st == NET_TCP_STATE_CLOSED || nt_timed_out(5000u)) {
            nt_finish("wget: connection failed or timed out");
        }
        return;
    }

    case NT_ST_RECV: {
        uint8_t buf[512];
        int got = net_tcp_recv(g_nt.conn, buf, (uint16_t)(sizeof(buf) - 1));
        if (got > 0) {
            buf[got] = 0;
            gui_terminal_write((const char *)buf);
            g_nt.recv_total += (uint32_t)got;
            g_nt.start_ticks = sched_time_ms();
            if (g_nt.recv_total >= 1024u * 1024u) {
                gui_terminal_write("\n[wget: 1MiB limit reached]\n");
                nt_finish(0);
            }
            return;
        }
        int st = net_tcp_state(g_nt.conn);
        if (st == NET_TCP_STATE_CLOSED || st == NET_TCP_STATE_CLOSE_WAIT) {
            gui_terminal_write("\n[wget done: ");
            char nb[12]; int ni=0; uint32_t v=g_nt.recv_total;
            if (v==0) nb[ni++]='0'; else { char rev[12]; int ri=0; while(v){rev[ri++]=(char)('0'+v%10);v/=10;} while(ri) nb[ni++]=rev[--ri]; }
            nb[ni]=0; gui_terminal_write(nb);
            gui_terminal_write(" bytes]\n");
            nt_finish(0);
            return;
        }
        if (nt_timed_out(8000u)) {
            nt_finish("\nwget: receive timed out");
        }
        return;
    }

    default:
        g_nt.active = 0;
        return;
    }
}

int browser_handle_address_enter(int key) {
    if (!g_browser_address_box || g_gui.focused_widget != g_browser_address_box) return 0;
    if (!gui_is_enter_key(key)) return 0;
    browser_load_start();
    return 1;
}

void gui_browser_open(void) {
    int win_w = 620;
    int win_h = 420;
    uint32_t i;

    if (g_browser_win) {
        browser_load_finish(0);
        gui_window_set_on_close(g_browser_win, 0, 0);
        gui_destroy_window(g_browser_win);
        g_browser_win = 0;
        g_browser_address_box = 0;
        g_browser_status_label = 0;
        for (i = 0; i < GUI_BROWSER_CONTENT_LINES; i++) g_browser_content_lines[i] = 0;
    }

    g_browser_win = gui_create_window(120, 86, win_w, win_h, i18n_t(I18N_KEY_WIN_BROWSER));
    if (!g_browser_win) return;
    gui_window_set_on_close(g_browser_win, browser_on_close, 0);

    gui_add_button(g_browser_win, 14, 18, 34, 26, i18n_t(I18N_KEY_BROWSER_BACK), browser_on_nav, 0);
    gui_add_button(g_browser_win, 54, 18, 34, 26, i18n_t(I18N_KEY_BROWSER_FORWARD), browser_on_nav, 0);
    gui_add_button(g_browser_win, 94, 18, 70, 26, i18n_t(I18N_KEY_BROWSER_REFRESH), browser_on_nav, 0);
    gui_add_label(g_browser_win, 176, 24, 62, 16, i18n_t(I18N_KEY_BROWSER_ADDRESS));
    g_browser_address_box = gui_add_textbox(g_browser_win, 238, 18, 282, 26, "http://example.com/");
    gui_add_button(g_browser_win, 530, 18, 62, 26, i18n_t(I18N_KEY_BROWSER_GO), browser_on_nav, 0);

    gui_add_panel(g_browser_win, 14, 56, win_w - 28, win_h - 104, gui_rgb(246, 249, 253));
    g_browser_content_lines[0] = gui_add_label(g_browser_win, 34, 82, win_w - 68, 18, i18n_t(I18N_KEY_BROWSER_HOME_TITLE));
    g_browser_content_lines[1] = gui_add_label(g_browser_win, 34, 104, win_w - 68, 18, i18n_t(I18N_KEY_BROWSER_HOME_HINT));
    g_browser_content_lines[2] = gui_add_label(g_browser_win, 34, 126, win_w - 68, 18, i18n_t(I18N_KEY_BROWSER_STATUS_PLACEHOLDER));
    for (i = 3; i < GUI_BROWSER_CONTENT_LINES; i++) {
        g_browser_content_lines[i] = gui_add_label(g_browser_win, 34, 82 + (int)i * 22, win_w - 68, 18, "");
    }
    g_browser_status_label = gui_add_label(g_browser_win, 14, win_h - 34, win_w - 28, 16, i18n_t(I18N_KEY_BROWSER_STATUS_READY));

    gui_render();
}
