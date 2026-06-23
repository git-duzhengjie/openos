#include "openos.h"
#include "browser_engine.h"
#include "tls_crypto.h"
#include "tls_handshake.h"
#include "tls_parser.h"
#include "tls_x509.h"

#define BROWSER_DEFAULT_HOST "example.com"
#define BROWSER_DEFAULT_PATH "/"
#define BROWSER_HTTP_PORT "80"
#define BROWSER_RECV_MAX 8192
#define BROWSER_NET_WAIT_TRIES 80
#define BROWSER_DNS_RETRIES 8
#define BROWSER_CONNECT_TIMEOUT_MS 4000
#define BROWSER_RESPONSE_TIMEOUT_MS 6000
#define BROWSER_RESPONSE_IDLE_TIMEOUT_MS 500
#define BROWSER_POLL_SLICE_MS 100
#define BROWSER_RECV_CHUNK_MAX 1400
#define BROWSER_HOST_MAX 128
#define BROWSER_PATH_MAX 256
#define BROWSER_BODY_MAX 768
#define BROWSER_TITLE_MAX 96
#define BROWSER_STATUS_MAX 96
#define BROWSER_HISTORY_MAX 8
#define BROWSER_LINK_MAX 8
#define BROWSER_CACHE_MAX 4
#define BROWSER_REDIRECT_MAX 4
#define BROWSER_VIEW_LINES 12
#define BROWSER_LINE_MAX 96
#define BROWSER_ADDRESS_MAX 256
#define BROWSER_TAB_TITLE_MAX 32
#define BROWSER_WINDOW_W 900
#define BROWSER_WINDOW_H 520
#define BROWSER_TAB_X 10
#define BROWSER_TAB_Y 0
#define BROWSER_NEW_TAB_BUTTON_W 28
#define BROWSER_NEW_TAB_BUTTON_H 24
#define BROWSER_NEW_TAB_BUTTON_MARGIN_RIGHT 10
#define BROWSER_TAB_GAP 6
#define BROWSER_TABVIEW_W (BROWSER_WINDOW_W - BROWSER_TAB_X - BROWSER_NEW_TAB_BUTTON_W - BROWSER_TAB_GAP - BROWSER_NEW_TAB_BUTTON_MARGIN_RIGHT)
#define BROWSER_NEW_TAB_BUTTON_X (BROWSER_TAB_X + BROWSER_TABVIEW_W + BROWSER_TAB_GAP)
#define BROWSER_FOCUS_LINK 1
#define BROWSER_FOCUS_FORM 2

typedef enum browser_tab_icon_kind {
    BROWSER_TAB_ICON_DEFAULT = 0,
    BROWSER_TAB_ICON_PAGE = 1,
    BROWSER_TAB_ICON_SITE = 2,
    BROWSER_TAB_ICON_FILE = 3,
    BROWSER_TAB_ICON_ERROR = 4
} browser_tab_icon_kind_t;

typedef struct browser_load_context {
    volatile int active;
    volatile int done;
    volatile int result;
    int is_file;
    int is_https;
    int window_id;
    int status_label_id;
    int body_label_id;
    char host[BROWSER_HOST_MAX];
    char path[BROWSER_PATH_MAX];
    char response[BROWSER_RECV_MAX + 1];
    char body[BROWSER_BODY_MAX];
    char title[BROWSER_TITLE_MAX];
    char http_status[BROWSER_STATUS_MAX];
    char content_type[OB_MAX_HEADER_VALUE];
    char content_length[OB_MAX_HEADER_VALUE];
    char location[OB_MAX_HEADER_VALUE];
    char status[BROWSER_STATUS_MAX];
    char links[BROWSER_LINK_MAX][OB_MAX_ATTR_VALUE];
    int tab_icon;
    int link_count;
    int selected_link;
    int focus_mode;
    int address_label_id;
    int tabview_id;
    int home_visible;
    void *tabs;
    char address_text[BROWSER_ADDRESS_MAX];
    ob_dom_document_t dom;
    ob_form_state_t form_state;
} browser_load_context_t;

typedef struct browser_history_entry {
    char host[BROWSER_HOST_MAX];
    char path[BROWSER_PATH_MAX];
    int is_file;
    int is_https;
} browser_history_entry_t;

typedef struct browser_history {
    browser_history_entry_t entries[BROWSER_HISTORY_MAX];
    int count;
    int current;
} browser_history_t;

typedef struct browser_page_cache_entry {
    int valid;
    int is_file;
    int is_https;
    int scroll_line;
    char host[BROWSER_HOST_MAX];
    char path[BROWSER_PATH_MAX];
    char title[BROWSER_TITLE_MAX];
    char body[BROWSER_BODY_MAX];
    char status[BROWSER_STATUS_MAX];
    char content_type[OB_MAX_HEADER_VALUE];
    char content_length[OB_MAX_HEADER_VALUE];
    char location[OB_MAX_HEADER_VALUE];
} browser_page_cache_entry_t;

typedef struct browser_tab_snapshot {
    int active;
    int done;
    int result;
    int is_file;
    int is_https;
    int home_visible;
    int link_count;
    int selected_link;
    int focus_mode;
    char host[BROWSER_HOST_MAX];
    char path[BROWSER_PATH_MAX];
    char body[BROWSER_BODY_MAX];
    char title[BROWSER_TITLE_MAX];
    char http_status[BROWSER_STATUS_MAX];
    char content_type[OB_MAX_HEADER_VALUE];
    char content_length[OB_MAX_HEADER_VALUE];
    char location[OB_MAX_HEADER_VALUE];
    char status[BROWSER_STATUS_MAX];
    char links[BROWSER_LINK_MAX][OB_MAX_ATTR_VALUE];
    char address_text[BROWSER_ADDRESS_MAX];
    int tab_icon;
} browser_tab_snapshot_t;

typedef struct browser_tab_entry {
    char title[BROWSER_TAB_TITLE_MAX];
    int icon_kind;
    int has_page;
    int scroll_line;
    browser_tab_snapshot_t *page;
} browser_tab_entry_t;

typedef struct browser_tabs {
    browser_tab_entry_t *items;
    int count;
    int capacity;
    int active;
} browser_tabs_t;

static browser_page_cache_entry_t g_page_cache[BROWSER_CACHE_MAX];

static void browser_load_worker(void *arg);
static void browser_render_current_view(browser_load_context_t *load, int body_label, int scroll_line);
static void browser_update_form_status(int win, int status_label, const browser_load_context_t *load);
static int browser_finish_completed_load(browser_load_context_t *load, int win, int status_label, int body_label, int tabview_id, browser_tabs_t *tabs, int *scroll_line, int *rc);

static void browser_format_ip(unsigned int ip, char *out, int out_size)
{
    if (!out || out_size <= 0) return;
    snprintf(out, out_size, "%u.%u.%u.%u",
             (ip >> 24) & 0xffU,
             (ip >> 16) & 0xffU,
             (ip >> 8) & 0xffU,
             ip & 0xffU);
}

static int browser_wait_for_network(char *out, int out_size)
{
    openos_netinfo_t info;
    int i;

    for (i = 0; i < BROWSER_NET_WAIT_TRIES; ++i) {
        if (openos_netinfo(&info) == 0 && info.ip != 0 && info.gateway != 0 && info.dns != 0)
            return 0;
        openos_sleep(5);
    }

    if (openos_netinfo(&info) == 0) {
        char ip[24];
        char gw[24];
        char dns[24];
        browser_format_ip(info.ip, ip, sizeof(ip));
        browser_format_ip(info.gateway, gw, sizeof(gw));
        browser_format_ip(info.dns, dns, sizeof(dns));
        snprintf(out, out_size, "network not ready: ip=%s gw=%s dns=%s", ip, gw, dns);
    } else {
        snprintf(out, out_size, "network not ready: no network device");
    }
    return -1;
}

static void browser_format_connect_error(char *out, int out_size, const char *host, unsigned int dst_ip, unsigned short port)
{
    openos_netinfo_t info;
    char dst[24] = "0.0.0.0";
    char ip[24] = "0.0.0.0";
    char gw[24] = "0.0.0.0";
    char dns[24] = "0.0.0.0";
    char txdst[24] = "0.0.0.0";
    char nexthop[24] = "0.0.0.0";
    unsigned int tx_result = 0xffffffffU;
    unsigned int arp_entries = 0;
    unsigned int flags = 0;

    browser_format_ip(dst_ip, dst, sizeof(dst));
    if (openos_netinfo(&info) == 0) {
        browser_format_ip(info.ip, ip, sizeof(ip));
        browser_format_ip(info.gateway, gw, sizeof(gw));
        browser_format_ip(info.dns, dns, sizeof(dns));
        browser_format_ip(info.last_ipv4_tx_dst, txdst, sizeof(txdst));
        browser_format_ip(info.last_ipv4_tx_next_hop, nexthop, sizeof(nexthop));
        tx_result = (unsigned int)info.last_ipv4_tx_result;
        arp_entries = info.arp_entries;
        flags = info.flags;
    }
    snprintf(out, out_size,
             "connect() failed host=%s dst=%s port=%u ip=%s gw=%s dns=%s arp=%u flags=0x%x last_tx_dst=%s nh=%s tx_result=%d",
             host ? host : "?", dst, (unsigned int)port,
             ip, gw, dns, arp_entries, flags, txdst, nexthop, (int)tx_result);
}

static int browser_poll_fd(int fd, unsigned int events, unsigned int timeout_ms)
{
    openos_pollfd_t pfd;
    int rc;

    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = fd;
    pfd.events = (short)events;
    pfd.revents = 0;

    rc = openos_poll(&pfd, 1, timeout_ms);
    if (rc > 0) {
        if (pfd.revents & (OPENOS_POLLERR | OPENOS_POLLHUP))
            return -1;
        if (pfd.revents & events)
            return 1;
        return -1;
    }
    if (rc < 0)
        return -1;
    return 0;
}

static const char *find_body(const char *response)
{
    const char *body = strstr(response, "\r\n\r\n");
    if (body) return body + 4;
    body = strstr(response, "\n\n");
    if (body) return body + 2;
    return response;
}

static int browser_ascii_equal_ci(char a, char b)
{
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    return a == b;
}

static int browser_match_token_ci(const char *p, const char *token)
{
    int i = 0;
    if (!p || !token) return 0;
    while (token[i]) {
        if (!p[i] || !browser_ascii_equal_ci(p[i], token[i])) return 0;
        ++i;
    }
    return 1;
}

static int browser_tag_matches(const char *p, const char *tag)
{
    int i = 0;
    if (!p || *p != '<') return 0;
    ++p;
    if (*p == '/') ++p;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    while (tag[i]) {
        if (!p[i] || !browser_ascii_equal_ci(p[i], tag[i])) return 0;
        ++i;
    }
    return p[i] == '>' || p[i] == '/' || p[i] == ' ' || p[i] == '\t' || p[i] == '\r' || p[i] == '\n';
}

static char browser_decode_entity(const char **pp)
{
    const char *p = *pp;
    if (!strncmp(p, "&amp;", 5)) { *pp = p + 4; return '&'; }
    if (!strncmp(p, "&lt;", 4)) { *pp = p + 3; return '<'; }
    if (!strncmp(p, "&gt;", 4)) { *pp = p + 3; return '>'; }
    if (!strncmp(p, "&quot;", 6)) { *pp = p + 5; return '"'; }
    if (!strncmp(p, "&#39;", 5)) { *pp = p + 4; return '\''; }
    if (!strncmp(p, "&apos;", 6)) { *pp = p + 5; return '\''; }
    if (!strncmp(p, "&nbsp;", 6)) { *pp = p + 5; return ' '; }
    return '&';
}

static void browser_extract_http_status(const char *response, char *out, int out_size)
{
    int i = 0;
    if (!out || out_size <= 0) return;
    out[0] = 0;
    if (!response) return;
    while (response[i] && response[i] != '\r' && response[i] != '\n' && i < out_size - 1) {
        out[i] = response[i];
        ++i;
    }
    out[i] = 0;
}

static int browser_http_status_code(const char *status_line)
{
    const char *p = status_line;
    int code = 0;
    int digits = 0;
    if (!p) return 0;
    if (browser_match_token_ci(p, "HTTP/")) {
        while (*p && *p != ' ' && *p != '\t') ++p;
        while (*p == ' ' || *p == '\t') ++p;
    } else if (browser_match_token_ci(p, "HTTP ")) {
        p += 5;
    }
    while (*p >= '0' && *p <= '9' && digits < 3) {
        code = code * 10 + (*p - '0');
        ++p;
        ++digits;
    }
    return digits == 3 ? code : 0;
}

static void browser_format_short_http_status(const char *status_line, char *out, int out_size)
{
    int code;
    const char *reason;
    if (!out || out_size <= 0) return;
    out[0] = 0;
    code = browser_http_status_code(status_line);
    if (code <= 0) {
        snprintf(out, out_size, "%s", status_line && status_line[0] ? status_line : "HTTP status unknown");
        return;
    }
    reason = status_line ? status_line : "";
    if (browser_match_token_ci(reason, "HTTP/")) {
        while (*reason && *reason != ' ' && *reason != '\t') ++reason;
        while (*reason == ' ' || *reason == '\t') ++reason;
        while (*reason >= '0' && *reason <= '9') ++reason;
        while (*reason == ' ' || *reason == '\t') ++reason;
    } else if (browser_match_token_ci(reason, "HTTP ")) {
        reason += 5;
        while (*reason >= '0' && *reason <= '9') ++reason;
        while (*reason == ' ' || *reason == '\t') ++reason;
    }
    snprintf(out, out_size, "HTTP %d%s%s", code, reason[0] ? " " : "", reason);
}

static void browser_target_url(const browser_load_context_t *ctx, char *out, int out_size)
{
    if (!out || out_size <= 0) return;
    if (!ctx) {
        snprintf(out, out_size, "?");
        return;
    }
    if (ctx->is_file)
        snprintf(out, out_size, "file://%s", ctx->path);
    else
        snprintf(out, out_size, "%s://%s%s", ctx->is_https ? "https" : "http", ctx->host, ctx->path);
}

static void browser_make_error_page(browser_load_context_t *ctx, const char *summary, const char *detail)
{
    char target[BROWSER_HOST_MAX + BROWSER_PATH_MAX + 16];
    if (!ctx) return;
    browser_target_url(ctx, target, sizeof(target));
    snprintf(ctx->title, sizeof(ctx->title), "OpenOS Browser");
    snprintf(ctx->status, sizeof(ctx->status), "%s", summary && summary[0] ? summary : "Load failed");
    snprintf(ctx->body, sizeof(ctx->body),
             "\n"
             "\n"
             "                              Page could not be loaded\n"
             "\n"
             "                 ----------------------------------------\n"
             "                 URL: %s\n"
             "                 Reason: %s\n"
             "                 ----------------------------------------\n"
             "\n"
             "                 Edit the address above, then press Enter.",
             target,
             detail && detail[0] ? detail : (summary && summary[0] ? summary : "unknown error"));
    ctx->home_visible = 0;
}

static void browser_extract_title(char *dst, int dst_size, const char *html)
{
    const char *p = html;
    int out = 0;
    int in_title = 0;
    int pending_space = 0;
    if (!dst || dst_size <= 0) return;
    dst[0] = 0;
    if (!html) return;
    while (*p && out < dst_size - 1) {
        if (!in_title) {
            if (*p == '<' && browser_tag_matches(p, "title")) {
                const char *end = strchr(p, '>');
                if (!end) break;
                p = end + 1;
                in_title = 1;
                continue;
            }
            ++p;
            continue;
        }
        if (*p == '<' && p[1] == '/' && browser_tag_matches(p, "title")) break;
        char c = *p;
        if (c == '&') c = browser_decode_entity(&p);
        if (c == '\r' || c == '\n' || c == '\t' || c == ' ') { pending_space = 1; ++p; continue; }
        if (pending_space && out > 0) dst[out++] = ' ';
        pending_space = 0;
        dst[out++] = c;
        ++p;
    }
    dst[out] = 0;
}

static int browser_parse_url_arg(const char *url, char *host, int host_size, char *path, int path_size, int *is_https)
{
    ob_url_parts_t parts;
    char error[64];
    if (!url || !host || !path || host_size <= 0 || path_size <= 0) return -1;
    if (ob_url_parse_address(url, 0, &parts, error, sizeof(error)) != 0 || parts.is_file) return -1;
    snprintf(host, host_size, "%s", parts.host);
    snprintf(path, path_size, "%s", parts.path);
    if (is_https) *is_https = parts.is_https;
    return 0;
}

static void browser_format_address(char *out, int out_size, const char *host, const char *path, int is_file, int is_https)
{
    if (!out || out_size <= 0) return;
    if (is_file) snprintf(out, out_size, "file://%s", path && path[0] ? path : "/");
    else snprintf(out, out_size, "%s://%s%s", is_https ? "https" : "http", host && host[0] ? host : BROWSER_DEFAULT_HOST, path && path[0] ? path : "/");
}

static void browser_update_address_label(browser_load_context_t *ctx)
{
    const char *text;
    if (!ctx || ctx->window_id <= 0 || ctx->address_label_id <= 0) return;
    text = ctx->address_text[0] ? ctx->address_text : "Search OpenOS or type a URL";
    openos_gui_set_text_cursor(ctx->window_id, ctx->address_label_id, text, (int)strlen(text));
}

static void browser_sync_address_from_target(browser_load_context_t *ctx, const char *host, const char *path, int is_file, int is_https)
{
    if (!ctx) return;
    browser_format_address(ctx->address_text, sizeof(ctx->address_text), host, path, is_file, is_https);
    browser_update_address_label(ctx);
}

static int browser_address_submit_text(browser_load_context_t *ctx, const char *address, char *host, int host_size, char *path, int path_size, int *is_file, int *is_https, char *error, int error_size)
{
    ob_url_parts_t parts;
    const char *base_host;
    if (!ctx || !address) return -1;
    base_host = host && host[0] ? host : BROWSER_DEFAULT_HOST;
    if (ob_url_parse_address(address, base_host, &parts, error, error_size) != 0) return -1;
    if (host && host_size > 0) snprintf(host, host_size, "%s", parts.host);
    if (path && path_size > 0) snprintf(path, path_size, "%s", parts.path);
    if (is_file) *is_file = parts.is_file;
    if (is_https) *is_https = parts.is_https;
    browser_sync_address_from_target(ctx, parts.host, parts.path, parts.is_file, parts.is_https);
    return 1;
}

static void browser_make_home_view(char *out, int out_size, const char *address)
{
    if (!out || out_size <= 0) return;
    snprintf(out, out_size,
             "\n"
             "\n"
             "                                  OpenOS\n"
             "                              Browser Search\n"
             "\n"
             "                 ----------------------------------------\n"
             "                 |  %s  |\n"
             "                 ----------------------------------------\n"
             "\n"
             "                   GitHub      Docs      Samples      Network\n"
             "\n"
             "              Type an address above, then press Enter to load.",
             address && address[0] ? address : "Search OpenOS or type a URL");
}

static int browser_parse_file_arg(const char *url, char *path, int path_size)
{
    const char *p = url;
    if (!url || !path || path_size <= 0) return -1;
    if (browser_match_token_ci(p, "file://")) p += 7;
    if (!p[0]) return -1;
    snprintf(path, path_size, "%s", p);
    return 0;
}

static void browser_history_init(browser_history_t *history, const char *host, const char *path, int is_file, int is_https)
{
    if (!history) return;
    memset(history, 0, sizeof(*history));
    snprintf(history->entries[0].host, sizeof(history->entries[0].host), "%s", host ? host : BROWSER_DEFAULT_HOST);
    snprintf(history->entries[0].path, sizeof(history->entries[0].path), "%s", path ? path : BROWSER_DEFAULT_PATH);
    history->entries[0].is_file = is_file;
    history->entries[0].is_https = is_https;
    history->count = 1;
    history->current = 0;
}

static const browser_history_entry_t *browser_history_current(const browser_history_t *history)
{
    if (!history || history->count <= 0 || history->current < 0 || history->current >= history->count) return 0;
    return &history->entries[history->current];
}

static int browser_history_go(browser_history_t *history, int delta)
{
    int next;
    if (!history) return -1;
    next = history->current + delta;
    if (next < 0 || next >= history->count) return -1;
    history->current = next;
    return 0;
}

static void browser_history_push(browser_history_t *history, const char *host, const char *path, int is_file, int is_https)
{
    int i;
    if (!history || !path || !path[0]) return;
    if (!is_file && (!host || !host[0])) return;
    if (history->current < history->count - 1) history->count = history->current + 1;
    if (history->count >= BROWSER_HISTORY_MAX) {
        for (i = 1; i < history->count; ++i) history->entries[i - 1] = history->entries[i];
        history->count = BROWSER_HISTORY_MAX - 1;
        history->current = history->count - 1;
    }
    ++history->current;
    if (history->current < 0) history->current = 0;
    if (history->current >= BROWSER_HISTORY_MAX) history->current = BROWSER_HISTORY_MAX - 1;
    snprintf(history->entries[history->current].host, sizeof(history->entries[history->current].host), "%s", is_file ? "" : host);
    snprintf(history->entries[history->current].path, sizeof(history->entries[history->current].path), "%s", path);
    history->entries[history->current].is_file = is_file;
    history->entries[history->current].is_https = is_https;
    history->count = history->current + 1;
}

static const char *browser_basename_dir_end(const char *path)
{
    const char *last = 0;
    const char *p = path;
    if (!path) return 0;
    while (*p) {
        if (*p == '/') last = p;
        ++p;
    }
    return last;
}

static void browser_join_relative_path(char *out, int out_size, const char *base_path, const char *href)
{
    ob_url_join_relative_path(out, out_size, base_path, href);
}

static int browser_resolve_link(const browser_history_entry_t *base, const char *href, char *host, int host_size, char *path, int path_size, int *is_file, int *is_https, char *error, int error_size)
{
    if (!base || !href || !href[0] || !host || !path || !is_file || !is_https) return -1;
    host[0] = 0;
    path[0] = 0;
    *is_file = base->is_file;
    *is_https = base->is_https;
    if (browser_match_token_ci(href, "https://") || browser_match_token_ci(href, "http://")) {
        *is_file = 0;
        return browser_parse_url_arg(href, host, host_size, path, path_size, is_https);
    }
    if (browser_match_token_ci(href, "file://")) {
        *is_file = 1;
        *is_https = 0;
        host[0] = 0;
        return browser_parse_file_arg(href, path, path_size);
    }
    if (strstr(href, "://")) {
        if (error && error_size > 0) snprintf(error, error_size, "Unsupported link scheme");
        return -1;
    }
    if (base->is_file) {
        host[0] = 0;
        browser_join_relative_path(path, path_size, base->path, href);
        *is_file = 1;
        *is_https = 0;
    } else {
        snprintf(host, host_size, "%s", base->host);
        browser_join_relative_path(path, path_size, base->path, href);
        *is_file = 0;
        *is_https = base->is_https;
    }
    return path[0] ? 0 : -1;
}

static void browser_update_link_status(int win, int status_label, const browser_load_context_t *load)
{
    char status[128];
    if (!load || load->link_count <= 0) {
        openos_gui_set_text(win, status_label, "No links");
        return;
    }
    snprintf(status, sizeof(status), "Link %d/%d: %s", load->selected_link + 1, load->link_count, load->links[load->selected_link]);
    openos_gui_set_text(win, status_label, status);
}

static int browser_tabs_reserve(browser_tabs_t *tabs, int needed)
{
    browser_tab_entry_t *next;
    int new_capacity;

    if (!tabs || needed <= 0) return -1;
    if (tabs->capacity >= needed) return 0;

    new_capacity = tabs->capacity > 0 ? tabs->capacity : 1;
    while (new_capacity < needed) {
        if (new_capacity > 1024 * 1024) return -1;
        ++new_capacity;
    }

    next = (browser_tab_entry_t *)realloc(tabs->items, new_capacity * (int)sizeof(browser_tab_entry_t));
    if (!next) return -1;
    memset(next + tabs->capacity, 0, (new_capacity - tabs->capacity) * sizeof(browser_tab_entry_t));
    tabs->items = next;
    tabs->capacity = new_capacity;
    return 0;
}

static const char *browser_tabs_title_at(const browser_tabs_t *tabs, int index)
{
    if (!tabs || !tabs->items || index < 0 || index >= tabs->count) return "New Tab";
    return tabs->items[index].title[0] ? tabs->items[index].title : "New Tab";
}

static int browser_tabs_icon_at(const browser_tabs_t *tabs, int index)
{
    if (!tabs || !tabs->items || index < 0 || index >= tabs->count) return BROWSER_TAB_ICON_DEFAULT;
    if (tabs->items[index].icon_kind < BROWSER_TAB_ICON_DEFAULT || tabs->items[index].icon_kind > BROWSER_TAB_ICON_ERROR)
        return BROWSER_TAB_ICON_DEFAULT;
    return tabs->items[index].icon_kind;
}

static void browser_tabs_refresh(int win, browser_tabs_t *tabs, int tabview_id)
{
    char *joined;
    int joined_size;
    int used = 0;
    int i;

    if (win < 0 || tabview_id < 0 || !tabs) return;
    if (tabs->count <= 0) {
        openos_gui_set_tabview_tabs(win, tabview_id, "");
        openos_gui_set_tabview_active(win, tabview_id, -1);
        return;
    }

    joined_size = tabs->count * (BROWSER_TAB_TITLE_MAX + 5) + 1;
    joined = (char *)malloc(joined_size);
    if (!joined) return;
    joined[0] = 0;

    for (i = 0; i < tabs->count; ++i) {
        const char *title = browser_tabs_title_at(tabs, i);
        int icon = browser_tabs_icon_at(tabs, i);
        if (i > 0 && used < joined_size - 1) joined[used++] = '|';
        if (used < joined_size - 4) {
            joined[used++] = '';
            joined[used++] = (char)('0' + icon);
            joined[used++] = ':';
        }
        while (*title && used < joined_size - 1) joined[used++] = *title++;
        joined[used] = 0;
    }

    openos_gui_set_tabview_tabs(win, tabview_id, joined);
    free(joined);
    openos_gui_set_tabview_active(win, tabview_id, tabs->active);
}

static void browser_tabs_init(int win, browser_tabs_t *tabs, int tabview_id)
{
    if (!tabs) return;
    memset(tabs, 0, sizeof(*tabs));
    if (browser_tabs_reserve(tabs, 1) < 0) return;
    tabs->count = 1;
    tabs->active = 0;
    snprintf(tabs->items[0].title, sizeof(tabs->items[0].title), "New Tab");
    tabs->items[0].icon_kind = BROWSER_TAB_ICON_DEFAULT;
    browser_tabs_refresh(win, tabs, tabview_id);
}

static void browser_sanitize_tab_title(char *out, int out_size, const char *title)
{
    int i;
    int j = 0;

    if (!out || out_size <= 0) return;
    out[0] = 0;
    if (!title || !title[0]) title = "New Tab";
    while (title[j] == ' ' || title[j] == '\t' || title[j] == '\n' || title[j] == '\r') ++j;
    for (i = 0; title[j] && i < out_size - 1; ++j) {
        char ch = title[j];
        if (ch == '\n' || ch == '\r' || ch == '|') ch = ' ';
        if (i == 0 || ch != ' ' || out[i - 1] != ' ') out[i++] = ch;
    }
    while (i > 0 && out[i - 1] == ' ') --i;
    out[i] = 0;
    if (!out[0]) snprintf(out, out_size, "New Tab");
}

static int browser_text_contains_case_insensitive(const char *text, const char *needle)
{
    int i;
    int j;

    if (!text || !needle || !needle[0]) return 0;
    for (i = 0; text[i]; ++i) {
        for (j = 0; needle[j]; ++j) {
            char a = text[i + j];
            char b = needle[j];
            if (!a) return 0;
            if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
            if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
            if (a != b) break;
        }
        if (!needle[j]) return 1;
    }
    return 0;
}

static int browser_detect_tab_icon_kind(const browser_load_context_t *load)
{
    if (!load) return BROWSER_TAB_ICON_DEFAULT;
    if (load->result < 0) return BROWSER_TAB_ICON_ERROR;
    if (load->is_file) return BROWSER_TAB_ICON_FILE;
    if ((browser_text_contains_case_insensitive(load->response, "rel=") &&
         browser_text_contains_case_insensitive(load->response, "icon")) ||
        browser_text_contains_case_insensitive(load->response, "shortcut icon") ||
        browser_text_contains_case_insensitive(load->response, "apple-touch-icon") ||
        browser_text_contains_case_insensitive(load->response, "/favicon.ico")) {
        return BROWSER_TAB_ICON_SITE;
    }
    if (load->title[0] || load->host[0]) return BROWSER_TAB_ICON_PAGE;
    return BROWSER_TAB_ICON_DEFAULT;
}

static void browser_update_tab_title(int win, int tabview_id, browser_tabs_t *tabs, const browser_load_context_t *load)
{
    char title[BROWSER_TITLE_MAX + 16];
    const char *source = "New Tab";

    if (tabview_id < 0 || !load || !tabs || !tabs->items || tabs->active < 0 || tabs->active >= tabs->count) return;
    if (load->title[0]) {
        source = load->title;
    } else if (load->host[0]) {
        source = load->host;
    }
    browser_sanitize_tab_title(title, sizeof(title), source);
    snprintf(tabs->items[tabs->active].title, sizeof(tabs->items[tabs->active].title), "%s", title);
    tabs->items[tabs->active].icon_kind = load->tab_icon;
    if (tabs->items[tabs->active].icon_kind < BROWSER_TAB_ICON_DEFAULT || tabs->items[tabs->active].icon_kind > BROWSER_TAB_ICON_ERROR)
        tabs->items[tabs->active].icon_kind = browser_detect_tab_icon_kind(load);
    browser_tabs_refresh(win, tabs, tabview_id);
}

static void browser_copy_bytes(void *dst, const void *src, int len)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (len-- > 0) *d++ = *s++;
}

static browser_tab_snapshot_t *browser_tab_page_alloc(void)
{
    browser_tab_snapshot_t *page = (browser_tab_snapshot_t *)malloc((int)sizeof(browser_tab_snapshot_t));
    if (!page) return 0;
    memset(page, 0, sizeof(*page));
    return page;
}

static void browser_tab_entry_free_page(browser_tab_entry_t *tab)
{
    if (!tab) return;
    if (tab->page) free(tab->page);
    tab->page = 0;
    tab->has_page = 0;
}

static void browser_tabs_destroy(browser_tabs_t *tabs)
{
    int i;
    if (!tabs) return;
    if (tabs->items) {
        for (i = 0; i < tabs->count; ++i)
            browser_tab_entry_free_page(&tabs->items[i]);
        free(tabs->items);
    }
    memset(tabs, 0, sizeof(*tabs));
}

static void browser_copy_tab_entry(browser_tab_entry_t *dst, const browser_tab_entry_t *src)
{
    browser_copy_bytes(dst, src, (int)sizeof(*dst));
}

static void browser_tab_snapshot_save(browser_tab_snapshot_t *page, const browser_load_context_t *load)
{
    if (!page || !load) return;
    memset(page, 0, sizeof(*page));
    page->active = load->active;
    page->done = load->done;
    page->result = load->result;
    page->is_file = load->is_file;
    page->is_https = load->is_https;
    page->home_visible = load->home_visible;
    page->link_count = load->link_count;
    page->selected_link = load->selected_link;
    page->focus_mode = load->focus_mode;
    snprintf(page->host, sizeof(page->host), "%s", load->host);
    snprintf(page->path, sizeof(page->path), "%s", load->path);
    snprintf(page->body, sizeof(page->body), "%s", load->body);
    snprintf(page->title, sizeof(page->title), "%s", load->title);
    snprintf(page->http_status, sizeof(page->http_status), "%s", load->http_status);
    snprintf(page->content_type, sizeof(page->content_type), "%s", load->content_type);
    snprintf(page->content_length, sizeof(page->content_length), "%s", load->content_length);
    snprintf(page->location, sizeof(page->location), "%s", load->location);
    snprintf(page->status, sizeof(page->status), "%s", load->status);
    browser_copy_bytes(page->links, load->links, (int)sizeof(page->links));
    page->tab_icon = load->tab_icon;
    snprintf(page->address_text, sizeof(page->address_text), "%s", load->address_text);
}

static void browser_tab_snapshot_restore(browser_load_context_t *load, const browser_tab_snapshot_t *page)
{
    if (!load || !page) return;
    load->active = page->active;
    load->done = page->done;
    load->result = page->result;
    load->is_file = page->is_file;
    load->is_https = page->is_https;
    load->home_visible = page->home_visible;
    load->link_count = page->link_count;
    load->selected_link = page->selected_link;
    load->focus_mode = page->focus_mode;
    snprintf(load->host, sizeof(load->host), "%s", page->host);
    snprintf(load->path, sizeof(load->path), "%s", page->path);
    snprintf(load->body, sizeof(load->body), "%s", page->body);
    snprintf(load->title, sizeof(load->title), "%s", page->title);
    snprintf(load->http_status, sizeof(load->http_status), "%s", page->http_status);
    snprintf(load->content_type, sizeof(load->content_type), "%s", page->content_type);
    snprintf(load->content_length, sizeof(load->content_length), "%s", page->content_length);
    snprintf(load->location, sizeof(load->location), "%s", page->location);
    snprintf(load->status, sizeof(load->status), "%s", page->status);
    browser_copy_bytes(load->links, page->links, (int)sizeof(load->links));
    load->tab_icon = page->tab_icon;
    snprintf(load->address_text, sizeof(load->address_text), "%s", page->address_text);
}

static void browser_tab_save_current(browser_tabs_t *tabs, const browser_load_context_t *load, int scroll_line)
{
    browser_tab_entry_t *tab;
    char title[BROWSER_TAB_TITLE_MAX];

    if (!tabs || !tabs->items || !load || tabs->active < 0 || tabs->active >= tabs->count) return;
    tab = &tabs->items[tabs->active];
    snprintf(title, sizeof(title), "%s", tab->title);
    if (!tab->page) tab->page = browser_tab_page_alloc();
    if (!tab->page) return;
    browser_tab_snapshot_save(tab->page, load);
    tab->icon_kind = tab->page->tab_icon;
    tab->has_page = 1;
    tab->scroll_line = scroll_line;
    snprintf(tab->title, sizeof(tab->title), "%s", title[0] ? title : "New Tab");
}

static void browser_clear_current_view(browser_load_context_t *load, int win, int status_label, int body_label,
                                       int address_label, int tabview_id, browser_tabs_t *tabs, int *scroll_line)
{
    if (!load) return;
    memset(load, 0, sizeof(*load));
    load->window_id = win;
    load->status_label_id = status_label;
    load->body_label_id = body_label;
    load->address_label_id = address_label;
    load->tabview_id = tabview_id;
    load->tabs = tabs;
    load->done = 1;
    if (scroll_line) *scroll_line = 0;
    openos_gui_set_text(win, address_label, "");
    openos_gui_set_text(win, body_label, "");
    openos_gui_set_text(win, status_label, "");
}

static void browser_tab_restore_current(browser_tabs_t *tabs, browser_load_context_t *load, int win,
                                        int status_label, int body_label, int address_label,
                                        int tabview_id, int *scroll_line)
{
    browser_tab_entry_t *tab;

    if (!tabs || !load || !tabs->items || tabs->active < 0 || tabs->active >= tabs->count) {
        browser_clear_current_view(load, win, status_label, body_label, address_label, tabview_id, tabs, scroll_line);
        return;
    }

    tab = &tabs->items[tabs->active];
    if (!tab->has_page || !tab->page) {
        browser_clear_current_view(load, win, status_label, body_label, address_label, tabview_id, tabs, scroll_line);
        return;
    }

    memset(load, 0, sizeof(*load));
    browser_tab_snapshot_restore(load, tab->page);
    load->window_id = win;
    load->status_label_id = status_label;
    load->body_label_id = body_label;
    load->address_label_id = address_label;
    load->tabview_id = tabview_id;
    load->tabs = tabs;
    if (scroll_line) *scroll_line = tab->scroll_line;

    browser_update_address_label(load);
    if (load->home_visible) {
        openos_gui_set_text(win, body_label, load->body);
    } else if (load->body[0]) {
        browser_render_current_view(load, body_label, scroll_line ? *scroll_line : tab->scroll_line);
    } else {
        openos_gui_set_text(win, body_label, "");
    }
    openos_gui_set_text(win, status_label, load->status[0] ? load->status : (load->active ? "Loading" : ""));
}

static int browser_tabs_new(int win, browser_tabs_t *tabs, int tabview_id)
{
    if (!tabs) return -1;
    if (browser_tabs_reserve(tabs, tabs->count + 1) < 0) return -1;
    tabs->active = tabs->count;
    memset(&tabs->items[tabs->count], 0, sizeof(tabs->items[tabs->count]));
    snprintf(tabs->items[tabs->count].title, sizeof(tabs->items[tabs->count].title), "New Tab");
    tabs->items[tabs->count].icon_kind = BROWSER_TAB_ICON_DEFAULT;
    tabs->count++;
    browser_tabs_refresh(win, tabs, tabview_id);
    return 0;
}

static int browser_tabs_sync_from_widget(int win, browser_tabs_t *tabs, int tabview_id, int *out_closed)
{
    char *joined;
    char token[BROWSER_TAB_TITLE_MAX];
    browser_tab_entry_t *old_items = 0;
    int joined_size;
    int old_count;
    int count = 0;
    int token_len = 0;
    int i;
    int active;
    int close_index = -1;

    if (out_closed) *out_closed = 0;
    if (win < 0 || tabview_id < 0 || !tabs) return -1;

    old_count = tabs->count;
    if (old_count > 0 && tabs->items) {
        old_items = (browser_tab_entry_t *)malloc(old_count * (int)sizeof(browser_tab_entry_t));
        if (!old_items) return -1;
        browser_copy_bytes(old_items, tabs->items, old_count * (int)sizeof(browser_tab_entry_t));
    }

    joined_size = (tabs->count > 0 ? tabs->count : 1) * (BROWSER_TAB_TITLE_MAX + 1) + 1;
    joined = (char *)malloc(joined_size);
    if (!joined) {
        if (old_items) free(old_items);
        return -1;
    }
    joined[0] = 0;
    if (openos_gui_get_text(win, tabview_id, joined, joined_size) < 0) {
        free(joined);
        if (old_items) free(old_items);
        return -1;
    }

    for (i = 0; ; ++i) {
        char ch = joined[i];
        if (ch == '|' || ch == 0) {
            if (token_len > 0) {
                if (browser_tabs_reserve(tabs, count + 1) < 0) {
                    free(joined);
                    if (old_items) free(old_items);
                    return -1;
                }
                memset(&tabs->items[count], 0, sizeof(tabs->items[count]));
                token[token_len] = 0;
                browser_sanitize_tab_title(tabs->items[count].title, sizeof(tabs->items[count].title), token);
                count++;
                token_len = 0;
            }
            if (ch == 0) break;
        } else if (token_len < (int)sizeof(token) - 1) {
            token[token_len++] = ch;
        }
    }
    free(joined);

    if (out_closed && count < old_count) *out_closed = 1;
    if (old_items && count == old_count - 1) {
        close_index = count;
        for (i = 0; i < count; ++i) {
            if (strcmp(tabs->items[i].title, old_items[i].title) != 0) {
                close_index = i;
                break;
            }
        }
        for (i = 0; i < count; ++i) {
            int old_index = i >= close_index ? i + 1 : i;
            if (old_index >= 0 && old_index < old_count) {
                char title[BROWSER_TAB_TITLE_MAX];
                snprintf(title, sizeof(title), "%s", tabs->items[i].title);
                browser_copy_tab_entry(&tabs->items[i], &old_items[old_index]);
                old_items[old_index].page = 0;
                snprintf(tabs->items[i].title, sizeof(tabs->items[i].title), "%s", title);
            }
        }
        if (close_index >= 0 && close_index < old_count)
            browser_tab_entry_free_page(&old_items[close_index]);
    } else if (old_items && count <= old_count) {
        for (i = 0; i < count; ++i) {
            char title[BROWSER_TAB_TITLE_MAX];
            snprintf(title, sizeof(title), "%s", tabs->items[i].title);
            browser_copy_tab_entry(&tabs->items[i], &old_items[i]);
            old_items[i].page = 0;
            snprintf(tabs->items[i].title, sizeof(tabs->items[i].title), "%s", title);
        }
        for (i = count; i < old_count; ++i)
            browser_tab_entry_free_page(&old_items[i]);
    }
    if (old_items) free(old_items);

    tabs->count = count;
    active = openos_gui_get_tabview_active(win, tabview_id);
    if (tabs->count <= 0) {
        active = -1;
    } else {
        if (active < 0) active = 0;
        if (active >= tabs->count) active = tabs->count - 1;
    }
    tabs->active = active;

    browser_tabs_refresh(win, tabs, tabview_id);
    return 0;
}

static void browser_make_view(char *out, int out_size, const browser_load_context_t *load, int scroll_line)
{
    int line = 0;
    int written = 0;
    const char *body;
    if (!out || out_size <= 0) return;
    out[0] = 0;
    if (!load) return;
    body = load->body[0] ? load->body : (load->result < 0 ? "Load failed" : "Empty response");
    while (*body && line < scroll_line) {
        if (*body == '\n') ++line;
        ++body;
    }
    line = 0;
    while (*body && written < out_size - 1 && line < BROWSER_VIEW_LINES) {
        out[written++] = *body;
        if (*body == '\n') ++line;
        ++body;
    }
    out[written] = 0;
}

static void browser_start_load(browser_load_context_t *load, int win, int status_label, int body_label,
                               const char *host, const char *path, int is_file, int is_https)
{
    openos_thread_t tid;
    char loading[128];
    int address_label_id;
    int tabview_id;
    void *tabs;
    char address_text[BROWSER_ADDRESS_MAX];
    if (!load || !host || !path) return;
    address_label_id = load->address_label_id;
    tabview_id = load->tabview_id;
    tabs = load->tabs;
    snprintf(address_text, sizeof(address_text), "%s", load->address_text);
    if (load->active && !load->done) {
        openos_gui_set_text(win, status_label, "Loading");
        openos_gui_set_text(win, body_label, "Load already in progress...");
        return;
    }
    memset(load, 0, sizeof(*load));
    snprintf(load->host, sizeof(load->host), "%s", host ? host : "");
    snprintf(load->path, sizeof(load->path), "%s", path);
    load->is_file = is_file;
    load->window_id = win;
    load->status_label_id = status_label;
    load->body_label_id = body_label;
    load->address_label_id = address_label_id;
    load->tabview_id = tabview_id;
    load->tabs = tabs;
    snprintf(load->address_text, sizeof(load->address_text), "%s", address_text);
    load->home_visible = 0;
    snprintf(loading, sizeof(loading), "Loading %s%s%s ...", is_file ? "file://" : (is_https ? "https://" : "http://"), is_file ? "" : load->host, load->path);
    load->active = 1;
    load->done = 0;
    load->result = -1;
    openos_gui_set_text(win, status_label, loading);
    browser_update_tab_title(win, tabview_id, (browser_tabs_t *)load->tabs, load);
    openos_gui_set_text(win, body_label, "Waiting for HTTP response...");
    if (openos_thread_create(&tid, browser_load_worker, load) != 0) {
        load->active = 0;
        load->done = 1;
        load->result = -1;
        snprintf(load->status, sizeof(load->status), "Failed");
        snprintf(load->body, sizeof(load->body), "failed to create browser loader thread");
        openos_gui_set_text(win, status_label, "Failed");
        openos_gui_set_text(win, body_label, load->body);
    }
}

static int browser_submit_address_bar(browser_load_context_t *load, browser_history_t *history,
                                      int win, int status_label, int body_label, int *scroll_line,
                                      const char **current_host, const char **current_path)
{
    const browser_history_entry_t *cur;
    char next_host[BROWSER_HOST_MAX];
    char next_path[BROWSER_PATH_MAX];
    char address[BROWSER_ADDRESS_MAX];
    char error[96];
    int next_is_file = 0;
    int next_is_https = 0;
    int submit_rc;
    if (!load || !history || !scroll_line) return -1;
    address[0] = 0;
    if (load->address_label_id >= 0)
        openos_gui_get_text(win, load->address_label_id, address, sizeof(address));
    if (!address[0]) snprintf(address, sizeof(address), "%s", load->address_text);
    if (!address[0] || strcmp(address, "Search OpenOS or type a URL") == 0) {
        openos_gui_set_text(win, status_label, "Type an address, then press Enter");
        return -1;
    }
    cur = browser_history_current(history);
    snprintf(next_host, sizeof(next_host), "%s", cur ? cur->host : (load->host[0] ? load->host : BROWSER_DEFAULT_HOST));
    snprintf(next_path, sizeof(next_path), "%s", cur ? cur->path : (load->path[0] ? load->path : BROWSER_DEFAULT_PATH));
    error[0] = 0;
    submit_rc = browser_address_submit_text(load, address, next_host, sizeof(next_host), next_path, sizeof(next_path), &next_is_file, &next_is_https, error, sizeof(error));
    if (submit_rc < 0) {
        openos_gui_set_text(win, status_label, error[0] ? error : "Invalid address");
        return -1;
    }
    snprintf(load->host, sizeof(load->host), "%s", next_is_file ? "" : next_host);
    snprintf(load->path, sizeof(load->path), "%s", next_path);
    if (current_host) *current_host = next_is_file ? "" : load->host;
    if (current_path) *current_path = load->path;
    browser_tab_save_current((browser_tabs_t *)load->tabs, load, *scroll_line);
    browser_history_push(history, next_host, next_path, next_is_file, next_is_https);
    *scroll_line = 0;
    browser_start_load(load, win, status_label, body_label, next_host, next_path, next_is_file, next_is_https);
    return 0;
}

static int browser_fetch_file(const char *path, char *out, int out_size)
{
    int fd;
    int total = 0;
    if (!path || !out || out_size <= 0) return -1;
    out[0] = 0;
    fd = openos_open(path, 0, 0);
    if (fd < 0) {
        snprintf(out, out_size, "file open failed path=%s reason=openos_open returned %d", path, fd);
        return -1;
    }
    while (total < out_size - 1) {
        int n = openos_read(fd, out + total, out_size - 1 - total);
        if (n < 0) {
            openos_close(fd);
            snprintf(out, out_size, "file read failed path=%s reason=openos_read returned %d", path, n);
            return -1;
        }
        if (n == 0) break;
        total += n;
    }
    out[total] = 0;
    if (total >= out_size - 1) {
        char extra;
        int n = openos_read(fd, &extra, 1);
        if (n > 0) {
            openos_close(fd);
            snprintf(out, out_size, "resource limit exceeded: file too large path=%s limit=%d bytes", path, out_size - 1);
            return -2;
        }
        if (n < 0) {
            openos_close(fd);
            snprintf(out, out_size, "file read failed path=%s reason=openos_read returned %d", path, n);
            return -1;
        }
    }
    openos_close(fd);
    return 0;
}

static const char *browser_find_http_body(const char *response)
{
    const char *p;
    if (!response) return 0;
    p = strstr(response, "\r\n\r\n");
    if (p) return p + 4;
    p = strstr(response, "\n\n");
    if (p) return p + 2;
    return 0;
}

static int browser_parse_positive_int(const char *s)
{
    int value = 0;
    if (!s || !s[0]) return -1;
    while (*s == ' ' || *s == '\t') ++s;
    while (*s >= '0' && *s <= '9') {
        int digit = *s - '0';
        if (value > 1000000) return -1;
        value = value * 10 + digit;
        ++s;
    }
    return value;
}

static int browser_response_has_complete_body(const char *response, int total)
{
    ob_http_headers_t parsed;
    const char *body;
    int expected;
    if (!response || total <= 0) return 0;
    body = browser_find_http_body(response);
    if (!body) return 0;
    memset(&parsed, 0, sizeof(parsed));
    if (ob_http_parse_headers(response, &parsed) < 0) return 0;
    expected = browser_parse_positive_int(parsed.content_length);
    if (expected < 0) return 0;
    return (int)(response + total - body) >= expected;
}

#define BROWSER_TLS_MAX_RECORD_PLAIN 16384
#define BROWSER_TLS_MAX_RECORD_CIPHER (BROWSER_TLS_MAX_RECORD_PLAIN + 64)
#define BROWSER_TLS_MAX_HANDSHAKE_BYTES 24576

typedef struct browser_tls_session {
    tls12_handshake_context_t hs;
    unsigned char server_hs_buf[BROWSER_TLS_MAX_HANDSHAKE_BYTES];
    int server_hs_len;
} browser_tls_session_t;

static int browser_sock_send_all(int fd, const unsigned char *buf, int len)
{
    int off = 0;
    while (off < len) {
        int n = openos_send(fd, buf + off, len - off, 0);
        if (n <= 0) return -1;
        off += n;
    }
    return 0;
}

static int browser_sock_recv_all(int fd, unsigned char *buf, int len)
{
    int off = 0;
    unsigned int waited = 0;
    while (off < len && waited < BROWSER_RESPONSE_TIMEOUT_MS) {
        int ready = browser_poll_fd(fd, OPENOS_POLLIN, BROWSER_POLL_SLICE_MS);
        waited += BROWSER_POLL_SLICE_MS;
        if (ready < 0) return -1;
        if (ready == 0) continue;
        {
            int n = openos_recv(fd, buf + off, len - off, 0);
            if (n < 0) continue;
            if (n == 0) return -1;
            off += n;
        }
    }
    return off == len ? 0 : -1;
}

static int browser_tls_read_record_raw(int fd, unsigned char *record, int record_cap, int *record_len, unsigned char *content_type)
{
    int len;
    if (!record || record_cap < 5 || !record_len || !content_type) return -1;
    if (browser_sock_recv_all(fd, record, 5) < 0) return -1;
    if (record[1] != 0x03) return -1;
    len = ((int)record[3] << 8) | (int)record[4];
    if (len < 0 || len + 5 > record_cap) return -1;
    if (browser_sock_recv_all(fd, record + 5, len) < 0) return -1;
    *content_type = record[0];
    *record_len = len + 5;
    return 0;
}

static int browser_tls_process_server_handshake_record(browser_tls_session_t *sess, const unsigned char *record, int record_len, char *err, int err_size)
{
    tls_record_view_t view;
    const uint8_t *fragment;
    size_t fragment_len;
    size_t off = 0;

    if (tls_parse_record_view(record, (size_t)record_len, &view) != 0 || view.content_type != TLS_CONTENT_TYPE_HANDSHAKE) {
        snprintf(err, err_size, "TLS: expected handshake record");
        return -1;
    }
    fragment = view.payload;
    fragment_len = (size_t)view.payload_len;
    if (fragment_len > (size_t)(BROWSER_TLS_MAX_HANDSHAKE_BYTES - sess->server_hs_len)) {
        snprintf(err, err_size, "TLS: server handshake too large");
        return -1;
    }
    openos_memcpy(sess->server_hs_buf + sess->server_hs_len, fragment, (int)fragment_len);
    sess->server_hs_len += (int)fragment_len;

    while ((size_t)sess->server_hs_len - off >= 4u) {
        size_t body_len = ((size_t)sess->server_hs_buf[off + 1] << 16) |
                          ((size_t)sess->server_hs_buf[off + 2] << 8) |
                          (size_t)sess->server_hs_buf[off + 3];
        size_t message_len = 4u + body_len;
        if (message_len > (size_t)sess->server_hs_len - off) break;
        if (tls12_handshake_on_server_handshake(&sess->hs, sess->server_hs_buf + off, message_len) == 0) {
            snprintf(err, err_size, "TLS: unsupported server handshake");
            return -1;
        }
        off += message_len;
    }
    if (off > 0u) {
        int remain = sess->server_hs_len - (int)off;
        if (remain > 0) openos_memmove(sess->server_hs_buf, sess->server_hs_buf + off, remain);
        sess->server_hs_len = remain;
    }
    return 0;
}
static int browser_tls_send_plain_record(int fd, uint8_t content_type, const unsigned char *plain, int plain_len)
{
    unsigned char record[5 + BROWSER_TLS_MAX_RECORD_PLAIN];
    if (!plain || plain_len < 0 || plain_len > BROWSER_TLS_MAX_RECORD_PLAIN) return -1;
    record[0] = content_type;
    record[1] = 0x03;
    record[2] = 0x03;
    record[3] = (unsigned char)((plain_len >> 8) & 0xff);
    record[4] = (unsigned char)(plain_len & 0xff);
    openos_memcpy(record + 5, plain, plain_len);
    return browser_sock_send_all(fd, record, plain_len + 5);
}

static int browser_tls_send_encrypted_record(browser_tls_session_t *sess, int fd, uint8_t content_type, const unsigned char *plain, int plain_len)
{
    unsigned char record[BROWSER_TLS_MAX_RECORD_CIPHER + 5];
    size_t out_len = 0;
    if (!sess || !sess->hs.has_record_layer || plain_len < 0 || plain_len > BROWSER_TLS_MAX_RECORD_PLAIN) return -1;
    if (tls12_aes128_gcm_record_layer_protect(&sess->hs.record_layer, content_type, plain, (size_t)plain_len, record, sizeof(record), &out_len) != 0) return -1;
    return browser_sock_send_all(fd, record, (int)out_len);
}

static int browser_tls_read_decrypted_record(browser_tls_session_t *sess, int fd, uint8_t *content_type, unsigned char *plain, int plain_cap, int *plain_len)
{
    unsigned char record[BROWSER_TLS_MAX_RECORD_CIPHER + 5];
    int record_len = 0;
    unsigned char raw_type = 0;
    size_t out_len = 0;
    uint8_t out_type = 0;
    if (!sess || !content_type || !plain || plain_cap <= 0 || !plain_len) return -1;
    if (browser_tls_read_record_raw(fd, record, (int)sizeof(record), &record_len, &raw_type) < 0) return -1;
    if (raw_type == TLS_CONTENT_TYPE_ALERT) return -2;
    if (!sess->hs.has_record_layer) return -1;
    if (tls12_aes128_gcm_record_layer_unprotect(&sess->hs.record_layer, record, (size_t)record_len, plain, (size_t)plain_cap, &out_len, &out_type) != 0) return -1;
    *content_type = out_type;
    *plain_len = (int)out_len;
    return 0;
}

static int browser_tls_parse_leaf_certificate(browser_tls_session_t *sess, tls_x509_certificate_view_t *cert, char *err, int err_size)
{
    if (!sess || !cert) return -1;
    if (sess->hs.certificate_chain.stored_certificate_count <= 0) {
        snprintf(err, err_size, "TLS: server did not send a certificate");
        return -1;
    }
    tls_x509_certificate_view_init(cert);
    if (tls_x509_parse_certificate(sess->hs.certificate_chain.certificates[0], sess->hs.certificate_chain.certificate_lengths[0], cert) != 0) {
        snprintf(err, err_size, "TLS: failed to parse leaf certificate");
        return -1;
    }
    return 0;
}

static void browser_tls_make_random(unsigned char *out, int len, unsigned int seed)
{
    unsigned int x = seed ? seed : 0x13579bdfu;
    int i;
    for (i = 0; i < len; i++) {
        x = x * 1664525u + 1013904223u + (unsigned int)i;
        out[i] = (unsigned char)((x >> 16) & 0xff);
    }
}

static int browser_tls_send_client_key_exchange(int fd, browser_tls_session_t *sess, const tls_x509_certificate_view_t *cert, char *err, int err_size)
{
    unsigned char rnd[TLS12_RSA_PRE_MASTER_RANDOM_SIZE];
    unsigned char premaster[TLS12_RSA_PRE_MASTER_SECRET_SIZE];
    unsigned char encrypted[320];
    unsigned char padding[320];
    tls_x509_subject_public_key_info_t spki;
    tls_x509_rsa_public_key_t rsa_key;
    size_t encrypted_len = 0;
    unsigned char handshake[4 + 2 + sizeof(encrypted)];
    size_t handshake_len = 0;
    if (!sess || !cert) return -1;
    if (tls_x509_parse_subject_public_key_info(cert, &spki) != 0 ||
        tls_x509_parse_rsa_public_key_from_spki(&spki, &rsa_key) != 0) {
        snprintf(err, err_size, "TLS: certificate does not contain an RSA key");
        return -1;
    }
    browser_tls_make_random(rnd, (int)sizeof(rnd), (unsigned int)((sess->hs.state + sess->server_hs_len) * 1103515245u + 12345u));
    browser_tls_make_random(padding, (int)sizeof(padding), (unsigned int)((sess->hs.state + sess->server_hs_len) * 2654435761u + 0x10203u));
    for (int i = 0; i < (int)sizeof(padding); ++i) {
        if (padding[i] == 0) padding[i] = (unsigned char)(i + 1);
    }
    if (tls12_make_rsa_pre_master_secret(sess->hs.negotiated_version, rnd, premaster) == 0 ||
        tls12_handshake_set_rsa_pre_master_secret(&sess->hs, premaster) == 0) {
        snprintf(err, err_size, "TLS: failed to create premaster secret");
        return -1;
    }
    if (tls_x509_rsa_encrypt_pkcs1_v15(&rsa_key, premaster, sizeof(premaster), padding, sizeof(padding), encrypted, sizeof(encrypted), &encrypted_len) != 0) {
        snprintf(err, err_size, "TLS: failed to encrypt premaster secret");
        return -1;
    }
    if (tls12_build_rsa_client_key_exchange_message(encrypted, encrypted_len, handshake, sizeof(handshake), &handshake_len) == 0) {
        snprintf(err, err_size, "TLS: failed to build client key exchange");
        return -1;
    }
    if (browser_tls_send_plain_record(fd, TLS_CONTENT_TYPE_HANDSHAKE, handshake, (int)handshake_len) < 0) {
        snprintf(err, err_size, "TLS: failed to send client key exchange");
        return -1;
    }
    if (tls12_handshake_on_client_key_exchange_sent(&sess->hs, handshake, handshake_len) == 0) {
        snprintf(err, err_size, "TLS: failed to update key exchange state");
        return -1;
    }
    return 0;
}
static int browser_tls_complete_handshake(int fd, const char *host, browser_tls_session_t *sess, char *err, int err_size)
{
    unsigned char record[BROWSER_TLS_MAX_RECORD_CIPHER + 5];
    size_t record_len_sz = 0;
    int record_len = 0;
    unsigned char type = 0;
    int saw_done = 0;
    int guard;
    tls_x509_certificate_view_t cert;
    unsigned char ccs_record[8];
    size_t ccs_len = 0;
    unsigned char fin_record[BROWSER_TLS_MAX_RECORD_CIPHER + 5];
    size_t fin_len = 0;
    unsigned char client_random[TLS12_CLIENT_RANDOM_SIZE];
    unsigned char server_finished_msg[128];
    size_t server_finished_len = 0;

    openos_memset(sess, 0, sizeof(*sess));
    tls12_handshake_context_init(&sess->hs);
    browser_tls_make_random(client_random, (int)sizeof(client_random), 0x5eed1234u);

    if (tls12_build_client_hello_record(host, client_random, record, sizeof(record), &record_len_sz) == 0) {
        snprintf(err, err_size, "TLS: failed to build ClientHello");
        return -1;
    }
    record_len = (int)record_len_sz;
    if (browser_sock_send_all(fd, record, record_len) < 0 || tls12_handshake_on_client_hello_sent(&sess->hs, record + 5, record_len_sz - 5) == 0) {
        snprintf(err, err_size, "TLS: failed to send ClientHello");
        return -1;
    }

    for (guard = 0; guard < 24 && !saw_done; guard++) {
        if (browser_tls_read_record_raw(fd, record, (int)sizeof(record), &record_len, &type) < 0) {
            snprintf(err, err_size, "TLS: failed to read server handshake");
            return -1;
        }
        if (type == TLS_CONTENT_TYPE_ALERT) {
            snprintf(err, err_size, "TLS: server returned alert during handshake");
            return -1;
        }
        if (browser_tls_process_server_handshake_record(sess, record, record_len, err, err_size) < 0) return -1;
        if (sess->hs.state == TLS12_HANDSHAKE_STATE_SERVER_HELLO_DONE_RECEIVED) saw_done = 1;
    }
    if (!saw_done) {
        snprintf(err, err_size, "TLS: ServerHelloDone not received");
        return -1;
    }
    if (browser_tls_parse_leaf_certificate(sess, &cert, err, err_size) < 0) return -1;
    if (!tls_x509_certificate_matches_hostname(&cert, host)) {
        snprintf(err, err_size, "TLS: certificate hostname mismatch");
        return -1;
    }

    if (browser_tls_send_client_key_exchange(fd, sess, &cert, err, err_size) < 0) return -1;
    if (tls12_handshake_derive_aes128_gcm_key_block(&sess->hs) == 0 || tls12_handshake_configure_aes128_gcm_record_layer(&sess->hs, TLS12_ENDPOINT_CLIENT) == 0) {
        snprintf(err, err_size, "TLS: failed to configure record layer");
        return -1;
    }

    if (tls12_handshake_build_client_change_cipher_spec_record(&sess->hs, ccs_record, sizeof(ccs_record), &ccs_len) == 0 ||
        browser_sock_send_all(fd, ccs_record, (int)ccs_len) < 0) {
        snprintf(err, err_size, "TLS: failed to send ChangeCipherSpec");
        return -1;
    }
    if (tls12_handshake_build_client_finished_record(&sess->hs, fin_record, sizeof(fin_record), &fin_len) == 0 ||
        browser_sock_send_all(fd, fin_record, (int)fin_len) < 0) {
        snprintf(err, err_size, "TLS: failed to send Finished");
        return -1;
    }

    if (browser_tls_read_record_raw(fd, record, (int)sizeof(record), &record_len, &type) < 0 ||
        tls12_handshake_on_server_change_cipher_spec_record(&sess->hs, record, (size_t)record_len) == 0) {
        snprintf(err, err_size, "TLS: failed to read server ChangeCipherSpec");
        return -1;
    }
    if (browser_tls_read_record_raw(fd, record, (int)sizeof(record), &record_len, &type) < 0 ||
        tls12_handshake_on_server_finished_record(&sess->hs, record, (size_t)record_len, server_finished_msg, sizeof(server_finished_msg), &server_finished_len) == 0) {
        snprintf(err, err_size, "TLS: server Finished verification failed");
        return -1;
    }
    return 0;
}

static int browser_fetch_https_tls(int fd, const char *host, const char *path, char *out, int out_size)
{
    browser_tls_session_t sess;
    char err[160];
    char req[512];
    int req_len;
    int total = 0;
    int guard;

    snprintf(err, sizeof(err), "TLS failed");
    if (out_size > 0) out[0] = 0;
    if (browser_tls_complete_handshake(fd, host, &sess, err, sizeof(err)) < 0) {
        snprintf(out, out_size,
                         "HTTP/1.0 495 TLS Error\r\nContent-Type: text/html\r\n\r\n"
                         "<html><head><title>TLS Error</title></head><body>"
                         "<h1>HTTPS TLS failed</h1><p>%s</p>"
                         "<p>OpenOS currently supports TLS 1.2 RSA/AES-GCM when the server offers it.</p>"
                         "</body></html>",
                         err);
        return -1;
    }

    req_len = snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: OpenOS-Browser/0.1\r\nAccept: text/html,*/*\r\nConnection: close\r\n\r\n", path, host);
    if (req_len <= 0 || req_len >= (int)sizeof(req) || browser_tls_send_encrypted_record(&sess, fd, TLS_CONTENT_TYPE_APPLICATION_DATA, (const unsigned char *)req, req_len) < 0) {
        snprintf(out, out_size, "HTTP/1.0 495 TLS Error\r\nContent-Type: text/html\r\n\r\n<html><body><h1>HTTPS TLS failed</h1><p>Failed to send encrypted HTTP request.</p></body></html>");
        return -1;
    }

    for (guard = 0; guard < 96 && total < out_size - 1; guard++) {
        unsigned char plain[4096];
        uint8_t content_type = 0;
        int plain_len = 0;
        int rc = browser_tls_read_decrypted_record(&sess, fd, &content_type, plain, (int)sizeof(plain), &plain_len);
        if (rc == -2) break;
        if (rc < 0) {
            if (total == 0) snprintf(out, out_size, "HTTP/1.0 495 TLS Error\r\nContent-Type: text/html\r\n\r\n<html><body><h1>HTTPS TLS failed</h1><p>Failed to decrypt HTTPS response.</p></body></html>");
            break;
        }
        if (content_type == TLS_CONTENT_TYPE_APPLICATION_DATA && plain_len > 0) {
            int copy = plain_len;
            if (copy > out_size - 1 - total) copy = out_size - 1 - total;
            openos_memcpy(out + total, plain, copy);
            total += copy;
            out[total] = 0;
        } else if (content_type == TLS_CONTENT_TYPE_ALERT) {
            break;
        }
    }
    if (total <= 0) {
        snprintf(out, out_size, "HTTP/1.0 502 Bad Gateway\r\nContent-Type: text/html\r\n\r\n<html><body><h1>HTTPS response was empty</h1></body></html>");
        return -1;
    }
    out[total] = 0;
    return total;
}

static int browser_fetch_http_once(const char *host, const char *path, int is_https, char *out, int out_size, ob_http_headers_t *headers)
{
    int fd;
    int total = 0;
    char request[256];
    unsigned int dst_ip = 0;
    unsigned short dst_port = is_https ? 443 : 80;

    if (!host || !path || !out || out_size <= 0) return -1;
    out[0] = 0;

    if (browser_wait_for_network(out, out_size) < 0)
        return -1;

    for (int attempt = 0; attempt < BROWSER_DNS_RETRIES; ++attempt) {
        if (openos_dnslookup(host, &dst_ip) == 0 && dst_ip != 0)
            break;
        dst_ip = 0;
        openos_sleep(10);
    }
    if (dst_ip == 0) {
        openos_netinfo_t info;
        char ip[24] = "0.0.0.0";
        char dns[24] = "0.0.0.0";
        if (openos_netinfo(&info) == 0) {
            browser_format_ip(info.ip, ip, sizeof(ip));
            browser_format_ip(info.dns, dns, sizeof(dns));
        }
        snprintf(out, out_size, "DNS failed for %s (ip=%s dns=%s)", host, ip, dns);
        return -1;
    }

    fd = openos_socket(OPENOS_AF_INET, OPENOS_SOCK_STREAM, 0);
    if (fd < 0) {
        snprintf(out, out_size, "socket() failed");
        return -1;
    }
    openos_fcntl(fd, F_SETFL, O_NONBLOCK);

    {
        openos_sockaddr_in_t connect_addr;
        int ready;
        memset(&connect_addr, 0, sizeof(connect_addr));
        connect_addr.sin_family = OPENOS_AF_INET;
        connect_addr.sin_port = openos_htons(dst_port);
        connect_addr.sin_addr = dst_ip;
        if (openos_connect(fd, (openos_sockaddr_t *)&connect_addr, sizeof(connect_addr)) < 0) {
            browser_format_connect_error(out, out_size, host, dst_ip, dst_port);
            openos_close(fd);
            return -1;
        }
        ready = browser_poll_fd(fd, OPENOS_POLLOUT, BROWSER_CONNECT_TIMEOUT_MS);
        if (ready <= 0) {
            browser_format_connect_error(out, out_size, host, dst_ip, dst_port);
            if (ready == 0) {
                char detail[512];
                snprintf(detail, sizeof(detail), "%s", out);
                snprintf(out, out_size, "connect timeout; %s", detail);
            }
            openos_close(fd);
            return -1;
        }
    }

    if (is_https) {
        int https_rc = browser_fetch_https_tls(fd, host, path, out, out_size);
        openos_close(fd);
        if (headers && https_rc >= 0) ob_http_parse_headers(out, headers);
        return https_rc;
    }

    snprintf(request, sizeof(request),
             "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: OpenOS-UserBrowser/0.1\r\nConnection: close\r\n\r\n",
             path, host);
    if (openos_send(fd, request, strlen(request), 0) < 0) {
        openos_close(fd);
        snprintf(out, out_size, "send() failed");
        return -1;
    }

    {
        unsigned int waited = 0;
        unsigned int idle_waited = 0;
        while (waited < BROWSER_RESPONSE_TIMEOUT_MS && total < out_size - 1) {
            int ready = browser_poll_fd(fd, OPENOS_POLLIN, BROWSER_POLL_SLICE_MS);
            if (ready < 0) {
                snprintf(out, out_size, "recv() poll failed from %s", host);
                openos_close(fd);
                return -1;
            }
            waited += BROWSER_POLL_SLICE_MS;
            if (ready == 0) {
                if (total > 0) {
                    idle_waited += BROWSER_POLL_SLICE_MS;
                    if (browser_response_has_complete_body(out, total) ||
                        idle_waited >= BROWSER_RESPONSE_IDLE_TIMEOUT_MS)
                        break;
                }
                continue;
            }

            unsigned int room = (unsigned int)(out_size - 1 - total);
            unsigned int chunk = room > BROWSER_RECV_CHUNK_MAX ? BROWSER_RECV_CHUNK_MAX : room;
            int n = openos_recv(fd, out + total, chunk, 0);
            if (n < 0) {
                if (total > 0)
                    break;
                snprintf(out, out_size, "recv() failed from %s", host);
                openos_close(fd);
                return -1;
            }
            if (n == 0)
                break;
            total += n;
            out[total] = 0;
            idle_waited = 0;
            if (browser_response_has_complete_body(out, total))
                break;
            if (total >= BROWSER_RECV_MAX)
                break;
        }
    }
    out[total] = 0;

    openos_close(fd);
    if (total <= 0) {
        snprintf(out, out_size, "receive stage timeout from %s", host);
        return -1;
    }

    if (headers) ob_http_parse_headers(out, headers);
    return 0;
}

static int browser_is_redirect_status(int code)
{
    return code == 301 || code == 302 || code == 303 || code == 307 || code == 308;
}

static int browser_fetch_http(const char *host, const char *path, int is_https, char *out, int out_size, ob_http_headers_t *headers)
{
    char cur_host[BROWSER_HOST_MAX];
    char cur_path[BROWSER_PATH_MAX];
    int cur_is_https = is_https;
    int redirects;
    if (!host || !path || !out || out_size <= 0) return -1;
    snprintf(cur_host, sizeof(cur_host), "%s", host);
    snprintf(cur_path, sizeof(cur_path), "%s", path);
    for (redirects = 0; redirects <= BROWSER_REDIRECT_MAX; ++redirects) {
        ob_http_headers_t parsed;
        int code;
        int rc = browser_fetch_http_once(cur_host, cur_path, cur_is_https, out, out_size, &parsed);
        if (rc < 0) return rc;
        code = browser_http_status_code(parsed.status_line);
        if (!browser_is_redirect_status(code)) {
            if (headers) *headers = parsed;
            return 0;
        }
        if (!parsed.location[0]) {
            snprintf(out, out_size, "redirect stage failed: HTTP %d without Location", code);
            return -1;
        }
        if (redirects >= BROWSER_REDIRECT_MAX) {
            snprintf(out, out_size, "redirect stage failed: too many redirects after Location=%s", parsed.location);
            return -1;
        }
        if (browser_match_token_ci(parsed.location, "http://") || browser_match_token_ci(parsed.location, "https://")) {
            if (browser_parse_url_arg(parsed.location, cur_host, sizeof(cur_host), cur_path, sizeof(cur_path), &cur_is_https) != 0) {
                snprintf(out, out_size, "redirect stage failed: invalid Location=%s", parsed.location);
                return -1;
            }
        } else {
            char joined[BROWSER_PATH_MAX];
            browser_join_relative_path(joined, sizeof(joined), cur_path, parsed.location);
            snprintf(cur_path, sizeof(cur_path), "%s", joined);
        }
    }
    snprintf(out, out_size, "redirect stage failed: loop detected");
    return -1;
}

static int browser_cache_key_eq(const browser_page_cache_entry_t *entry, const char *host, const char *path, int is_file, int is_https)
{
    if (!entry || !entry->valid || !path) return 0;
    if (entry->is_file != is_file || entry->is_https != is_https) return 0;
    if (strcmp(entry->path, path) != 0) return 0;
    return is_file || strcmp(entry->host, host ? host : "") == 0;
}

static void browser_cache_store(const browser_load_context_t *load, int scroll_line)
{
    int slot = 0;
    int i;
    if (!load || load->result < 0 || !load->path[0]) return;
    for (i = 0; i < BROWSER_CACHE_MAX; ++i) {
        if (browser_cache_key_eq(&g_page_cache[i], load->host, load->path, load->is_file, load->is_https)) { slot = i; break; }
        if (!g_page_cache[i].valid) slot = i;
    }
    g_page_cache[slot].valid = 1;
    g_page_cache[slot].is_file = load->is_file;
    g_page_cache[slot].is_https = load->is_https;
    g_page_cache[slot].scroll_line = scroll_line;
    snprintf(g_page_cache[slot].host, sizeof(g_page_cache[slot].host), "%s", load->host);
    snprintf(g_page_cache[slot].path, sizeof(g_page_cache[slot].path), "%s", load->path);
    snprintf(g_page_cache[slot].title, sizeof(g_page_cache[slot].title), "%s", load->title);
    snprintf(g_page_cache[slot].body, sizeof(g_page_cache[slot].body), "%s", load->body);
    snprintf(g_page_cache[slot].status, sizeof(g_page_cache[slot].status), "%s", load->status);
    snprintf(g_page_cache[slot].content_type, sizeof(g_page_cache[slot].content_type), "%s", load->content_type);
    snprintf(g_page_cache[slot].content_length, sizeof(g_page_cache[slot].content_length), "%s", load->content_length);
    snprintf(g_page_cache[slot].location, sizeof(g_page_cache[slot].location), "%s", load->location);
}

static int browser_cache_restore(browser_load_context_t *load, int win, int status_label, int body_label, int tabview_id, browser_tabs_t *tabs,
                                 const char *host, const char *path, int is_file, int is_https, int *scroll_line)
{
    int i;
    if (!load || !path) return -1;
    for (i = 0; i < BROWSER_CACHE_MAX; ++i) {
        if (browser_cache_key_eq(&g_page_cache[i], host, path, is_file, is_https)) {
            memset(load, 0, sizeof(*load));
            load->window_id = win;
            load->status_label_id = status_label;
            load->body_label_id = body_label;
            load->tabview_id = tabview_id;
            load->address_label_id = -1;
            load->tabs = tabs;
            load->is_file = is_file;
            load->is_https = is_https;
            load->done = 1;
            load->result = 0;
            snprintf(load->host, sizeof(load->host), "%s", host ? host : "");
            snprintf(load->path, sizeof(load->path), "%s", path);
            snprintf(load->title, sizeof(load->title), "%s", g_page_cache[i].title);
            snprintf(load->body, sizeof(load->body), "%s", g_page_cache[i].body);
            snprintf(load->status, sizeof(load->status), "Cache: %s", g_page_cache[i].status[0] ? g_page_cache[i].status : "hit");
            snprintf(load->content_type, sizeof(load->content_type), "%s", g_page_cache[i].content_type);
            snprintf(load->content_length, sizeof(load->content_length), "%s", g_page_cache[i].content_length);
            snprintf(load->location, sizeof(load->location), "%s", g_page_cache[i].location);
            load->tab_icon = browser_detect_tab_icon_kind(load);
            if (scroll_line) *scroll_line = g_page_cache[i].scroll_line;
            openos_gui_set_text(win, status_label, load->status);
            browser_update_tab_title(win, tabview_id, tabs, load);
            browser_render_current_view(load, body_label, scroll_line ? *scroll_line : 0);
            return 0;
        }
    }
    return -1;
}

static void browser_render_current_view(browser_load_context_t *load, int body_label, int scroll_line)
{
    char view[BROWSER_BODY_MAX + BROWSER_TITLE_MAX + 32];
    if (!load) return;
    browser_make_view(view, sizeof(view), load, scroll_line);
    openos_gui_set_text(load->window_id, body_label, view[0] ? view : "Empty response");
}

static int browser_finish_completed_load(browser_load_context_t *load, int win, int status_label, int body_label, int tabview_id, browser_tabs_t *tabs, int *scroll_line, int *rc)
{
    char view[BROWSER_BODY_MAX + BROWSER_TITLE_MAX + 32];

    if (!load || !scroll_line || !rc)
        return 0;
    if (!load->active || !load->done)
        return 0;

    __sync_synchronize();
    *rc = load->result;
    openos_gui_set_text(win, status_label, load->status[0] ? load->status : (*rc < 0 ? "Failed" : "Done"));
    load->tabview_id = tabview_id;
    browser_update_tab_title(win, tabview_id, (browser_tabs_t *)load->tabs, load);
    *scroll_line = 0;
    browser_make_view(view, sizeof(view), load, *scroll_line);
    openos_gui_set_text(win, body_label, view[0] ? view : (*rc < 0 ? "Load failed" : "Empty response"));
    browser_cache_store(load, *scroll_line);
    load->focus_mode = load->link_count > 0 ? BROWSER_FOCUS_LINK : (load->form_state.count > 0 ? BROWSER_FOCUS_FORM : 0);
    if (load->link_count > 0)
        browser_update_link_status(win, status_label, load);
    else if (load->form_state.count > 0)
        browser_update_form_status(win, status_label, load);
    load->active = 0;
    browser_tab_save_current(tabs, load, *scroll_line);
    printf("browser: gui updated result=%d status=%s\n", *rc, load->status[0] ? load->status : (*rc < 0 ? "Failed" : "Done"));
    return 1;
}

static void browser_refresh_form_body(browser_load_context_t *load, int body_label, int scroll_line)
{
    if (!load) return;
    if (load->dom.count > 0) {
        ob_dom_text_render_with_form_state(&load->dom, &load->form_state, load->body, sizeof(load->body));
    }
    browser_render_current_view(load, body_label, scroll_line);
}

static void browser_update_form_status(int win, int status_label, const browser_load_context_t *load)
{
    char status[128];
    const char *name;
    const char *kind;
    if (!load || load->form_state.count <= 0 || load->form_state.focused < 0) {
        openos_gui_set_text(win, status_label, "No form controls");
        return;
    }
    name = load->form_state.controls[load->form_state.focused].name[0] ? load->form_state.controls[load->form_state.focused].name : "control";
    kind = load->form_state.controls[load->form_state.focused].editable ? "Field" : "Submit";
    snprintf(status, sizeof(status), "%s %d/%d: %s (Tab next, Enter activate)", kind,
             load->form_state.focused + 1, load->form_state.count, name);
    openos_gui_set_text(win, status_label, status);
}

static int browser_focus_next(browser_load_context_t *load, int win, int status_label, int body_label, int scroll_line)
{
    if (!load) return -1;
    if (load->focus_mode == BROWSER_FOCUS_LINK && load->form_state.count > 0) {
        load->focus_mode = BROWSER_FOCUS_FORM;
        if (load->form_state.focused < 0) ob_form_state_focus_next(&load->form_state);
        browser_refresh_form_body(load, body_label, scroll_line);
        browser_update_form_status(win, status_label, load);
        return 0;
    }
    if (load->focus_mode == BROWSER_FOCUS_FORM && load->link_count > 0) {
        load->focus_mode = BROWSER_FOCUS_LINK;
        load->selected_link = (load->selected_link + 1) % load->link_count;
        browser_update_link_status(win, status_label, load);
        return 0;
    }
    if (load->link_count > 0) {
        load->focus_mode = BROWSER_FOCUS_LINK;
        load->selected_link = (load->selected_link + 1) % load->link_count;
        browser_update_link_status(win, status_label, load);
        return 0;
    }
    if (ob_form_state_focus_next(&load->form_state) >= 0) {
        load->focus_mode = BROWSER_FOCUS_FORM;
        browser_refresh_form_body(load, body_label, scroll_line);
        browser_update_form_status(win, status_label, load);
        return 0;
    }
    openos_gui_set_text(win, status_label, "No links or form controls");
    return -1;
}

static int browser_open_selected_link(browser_load_context_t *load, browser_history_t *history,
                                      int win, int status_label, int body_label, int *scroll_line)
{
    const browser_history_entry_t *cur;
    char selected_href[OB_MAX_ATTR_VALUE];
    char next_host[BROWSER_HOST_MAX];
    char next_path[BROWSER_PATH_MAX];
    char error[96];
    int next_is_file = 0;
    int next_is_https = 0;
    if (!load || !history || !scroll_line) return -1;
    cur = browser_history_current(history);
    selected_href[0] = 0;
    error[0] = 0;
    if (load->link_count > 0) snprintf(selected_href, sizeof(selected_href), "%s", load->links[load->selected_link]);
    if (!cur || !selected_href[0]) {
        openos_gui_set_text(win, status_label, "No link selected");
        return -1;
    }
    if (browser_resolve_link(cur, selected_href, next_host, sizeof(next_host), next_path, sizeof(next_path), &next_is_file, &next_is_https, error, sizeof(error)) != 0) {
        openos_gui_set_text(win, status_label, error[0] ? error : "Unsupported link");
        return -1;
    }
    *scroll_line = 0;
    browser_tab_save_current((browser_tabs_t *)load->tabs, load, *scroll_line);
    browser_history_push(history, next_host, next_path, next_is_file, next_is_https);
    browser_sync_address_from_target(load, next_host, next_path, next_is_file, next_is_https);
    browser_start_load(load, win, status_label, body_label, next_host, next_path, next_is_file, next_is_https);
    return 0;
}

static int browser_submit_current_form(browser_load_context_t *load, browser_history_t *history,
                                       int win, int status_label, int body_label, int *scroll_line)
{
    const browser_history_entry_t *cur;
    char action_url[BROWSER_ADDRESS_MAX];
    char next_host[BROWSER_HOST_MAX];
    char next_path[BROWSER_PATH_MAX];
    char error[96];
    int next_is_file = 0;
    int next_is_https = 0;
    int submit_id = -1;
    int build_rc;
    if (!load || !history || !scroll_line) return -1;
    cur = browser_history_current(history);
    error[0] = 0;
    if (load->form_state.count > 0 && load->form_state.focused >= 0 && load->form_state.focused < load->form_state.count)
        submit_id = load->form_state.controls[load->form_state.focused].node_id;
    build_rc = ob_form_build_get_url(&load->dom, &load->form_state, submit_id,
                                      cur ? cur->path : load->path, action_url, sizeof(action_url));
    if (!cur || build_rc < 0) {
        openos_gui_set_text(win, status_label, build_rc == -2 ? "Submit: only GET forms supported" : "Submit: no GET form target");
        return -1;
    }
    if (browser_resolve_link(cur, action_url, next_host, sizeof(next_host), next_path, sizeof(next_path), &next_is_file, &next_is_https, error, sizeof(error)) != 0) {
        openos_gui_set_text(win, status_label, error[0] ? error : "Submit: unsupported target");
        return -1;
    }
    *scroll_line = 0;
    browser_history_push(history, next_host, next_path, next_is_file, next_is_https);
    browser_sync_address_from_target(load, next_host, next_path, next_is_file, next_is_https);
    browser_start_load(load, win, status_label, body_label, next_host, next_path, next_is_file, next_is_https);
    return 0;
}

static void browser_load_worker(void *arg)
{
    browser_load_context_t *ctx = (browser_load_context_t *)arg;
    int rc;

    if (!ctx)
        return;

    {
        ob_http_headers_t headers;
        ob_http_headers_init(&headers);
        rc = ctx->is_file ? browser_fetch_file(ctx->path, ctx->response, sizeof(ctx->response))
                          : browser_fetch_http(ctx->host, ctx->path, ctx->is_https, ctx->response, sizeof(ctx->response), &headers);
        if (!ctx->is_file && rc == 0) {
            snprintf(ctx->content_type, sizeof(ctx->content_type), "%s", headers.content_type);
            snprintf(ctx->content_length, sizeof(ctx->content_length), "%s", headers.content_length);
            snprintf(ctx->location, sizeof(ctx->location), "%s", headers.location);
        }
    }
    ctx->result = rc;
    ctx->home_visible = 0;
    if (rc < 0) {
        browser_make_error_page(ctx, ctx->is_file ? "File load failed" : (ctx->is_https ? "HTTPS load failed" : "Network load failed"), ctx->response);
        printf("browser: %s\n", ctx->response);
    } else {
        const char *body = ctx->is_file ? ctx->response : find_body(ctx->response);
        int http_code = 0;
        if (ctx->is_file) {
            snprintf(ctx->http_status, sizeof(ctx->http_status), "FILE loaded");
        } else {
            char short_status[BROWSER_STATUS_MAX];
            browser_extract_http_status(ctx->response, ctx->http_status, sizeof(ctx->http_status));
            browser_format_short_http_status(ctx->http_status, short_status, sizeof(short_status));
            snprintf(ctx->http_status, sizeof(ctx->http_status), "%s", short_status);
            http_code = browser_http_status_code(ctx->http_status);
        }
        if (!ctx->is_file && !ob_http_content_is_renderable_html(ctx->content_type)) {
            char detail[BROWSER_BODY_MAX / 2];
            ctx->result = -3;
            snprintf(detail, sizeof(detail), "content is not renderable: Content-Type=%s Content-Length=%s",
                     ctx->content_type[0] ? ctx->content_type : "unknown",
                     ctx->content_length[0] ? ctx->content_length : "unknown");
            browser_make_error_page(ctx, "Unsupported content", detail);
            printf("browser: unsupported content %s://%s%s %s\n", ctx->is_https ? "https" : "http", ctx->host, ctx->path, ctx->content_type);
        } else if (!ctx->is_file && (http_code < 200 || http_code >= 300)) {
            char detail[BROWSER_BODY_MAX / 2];
            ctx->result = -2;
            snprintf(detail, sizeof(detail),
                     "server returned non-success status %s. Response preview: %.220s",
                     ctx->http_status[0] ? ctx->http_status : "HTTP status unknown",
                     body && body[0] ? body : "<empty response body>");
            browser_make_error_page(ctx, ctx->http_status[0] ? ctx->http_status : "HTTP error", detail);
            printf("browser: HTTP error %s://%s%s %s\n", ctx->is_https ? "https" : "http", ctx->host, ctx->path, ctx->status);
        } else {
            browser_extract_title(ctx->title, sizeof(ctx->title), body);
            {
                ob_html_parser_base_t parser;
                ob_html_parser_base_init(&parser);
                if (parser.iface.parse(&parser.iface, body, &ctx->dom) > 0) {
                    int i;
                    ctx->link_count = 0;
                    ctx->selected_link = 0;
                    for (i = 0; i < ctx->dom.count && ctx->link_count < BROWSER_LINK_MAX; ++i) {
                        if (ctx->dom.nodes[i].type == OB_DOM_NODE_ELEMENT && ob_token_eq_ci(ctx->dom.nodes[i].name, "a") && ctx->dom.nodes[i].href[0]) {
                            snprintf(ctx->links[ctx->link_count], sizeof(ctx->links[ctx->link_count]), "%s", ctx->dom.nodes[i].href);
                            ++ctx->link_count;
                        }
                    }
                    ob_dom_normalize_resource_urls(&ctx->dom, ctx->path);
                    ob_form_state_collect_from_dom(&ctx->form_state, &ctx->dom);
                    ob_dom_text_render_with_form_state(&ctx->dom, &ctx->form_state, ctx->body, sizeof(ctx->body));
                } else
                    ctx->body[0] = 0;
            }
            if (!ctx->body[0])
                snprintf(ctx->body, sizeof(ctx->body), "Empty response");
            if (!ctx->is_file && (ctx->content_type[0] || ctx->content_length[0] || ctx->location[0]))
                snprintf(ctx->status, sizeof(ctx->status), "%s CT=%s Len=%s%s%s",
                         ctx->http_status[0] ? ctx->http_status : "Done",
                         ctx->content_type[0] ? ctx->content_type : "?",
                         ctx->content_length[0] ? ctx->content_length : "?",
                         ctx->location[0] ? " Location=" : "", ctx->location[0] ? ctx->location : "");
            else
                snprintf(ctx->status, sizeof(ctx->status), "%s", ctx->http_status[0] ? ctx->http_status : "Done");
            printf("browser: loaded %s%s%s %s\n", ctx->is_file ? "file://" : (ctx->is_https ? "https://" : "http://"), ctx->is_file ? "" : ctx->host, ctx->path, ctx->status);
            if (ctx->title[0]) printf("title: %s\n", ctx->title);
        }
    }

    ctx->tab_icon = browser_detect_tab_icon_kind(ctx);
    __sync_synchronize();
    ctx->done = 1;
    __sync_synchronize();
    printf("browser: load complete result=%d status=%s\n", ctx->result, ctx->status[0] ? ctx->status : (ctx->result < 0 ? "Failed" : "Done"));
}

int main(int argc, char **argv)
{
    const char *host = BROWSER_DEFAULT_HOST;
    const char *path = BROWSER_DEFAULT_PATH;
    int is_file = 0;
    int is_https = 0;
    char summary[BROWSER_BODY_MAX];
    char home_address[BROWSER_ADDRESS_MAX];
    int win;
    int status_label;
    int address_label;
    int body_label;
    int tabview;
    int new_tab_button;
    int load_button;
    int back_button;
    int forward_button;
    int toolbar;
    int rc = 0;
    int scroll_line = 0;
    int address_dirty = 0;
    static browser_load_context_t load;
    static browser_history_t history;
    static browser_tabs_t tabs;

    memset(&load, 0, sizeof(load));

    if (argc > 1 && argv && argv[1] && argv[1][0]) {
        if (browser_match_token_ci(argv[1], "file://") || argv[1][0] == '/') {
            if (browser_parse_file_arg(argv[1], load.path, sizeof(load.path)) == 0) {
                host = "";
                path = load.path;
                is_file = 1;
            }
        } else if (strstr(argv[1], "://") || strchr(argv[1], '/')) {
            if (browser_parse_url_arg(argv[1], load.host, sizeof(load.host), load.path, sizeof(load.path), &is_https) == 0) {
                host = load.host;
                path = load.path;
            } else {
                host = argv[1];
            }
        } else {
            host = argv[1];
        }
    }
    if (argc > 2 && argv && argv[2] && argv[2][0]) { path = argv[2]; is_file = 0; is_https = 0; }
    if (!is_file && (!host || !host[0])) host = BROWSER_DEFAULT_HOST;
    if (!path || !path[0]) path = BROWSER_DEFAULT_PATH;

    browser_history_init(&history, host, path, is_file, is_https);

    win = openos_gui_create_window("OpenOS Browser", 54, 44, BROWSER_WINDOW_W, BROWSER_WINDOW_H);
    if (win < 0) {
        printf("browser: failed to create GUI window\n");
        return 1;
    }

    tabview = openos_gui_add_tabview(win, BROWSER_TAB_X, BROWSER_TAB_Y, BROWSER_TABVIEW_W, 30, "New Tab", 0, OPENOS_GUI_TABVIEW_BOTTOM_BORDER | OPENOS_GUI_TABVIEW_CLOSE_BUTTONS);
    new_tab_button = openos_gui_add_button(win, BROWSER_NEW_TAB_BUTTON_X, 2, BROWSER_NEW_TAB_BUTTON_W, BROWSER_NEW_TAB_BUTTON_H, "+");
    browser_tabs_init(win, &tabs, tabview);

    toolbar = openos_gui_add_toolbar(win, 0, 30, BROWSER_WINDOW_W, 54, "", OPENOS_GUI_TOOLBAR_BOTTOM_BORDER);
    (void)toolbar;
    back_button = openos_gui_add_button(win, 18, 48, 28, 24, "");
    openos_gui_set_button_flags(win, back_button, OPENOS_GUI_BUTTON_FLAG_FLAT);
    openos_gui_set_icon(win, back_button, OPENOS_GUI_ICON_NAV_BACK);
    forward_button = openos_gui_add_button(win, 50, 48, 28, 24, "");
    openos_gui_set_button_flags(win, forward_button, OPENOS_GUI_BUTTON_FLAG_FLAT);
    openos_gui_set_icon(win, forward_button, OPENOS_GUI_ICON_NAV_FORWARD);
    load_button = openos_gui_add_button(win, 82, 48, 28, 24, "");
    openos_gui_set_button_flags(win, load_button, OPENOS_GUI_BUTTON_FLAG_FLAT);
    openos_gui_set_icon(win, load_button, OPENOS_GUI_ICON_NAV_RELOAD);
    address_label = openos_gui_add_textbox(win, 120, 48, 760, 24, "");

    if (argc > 1 && argv && argv[1] && argv[1][0])
        browser_format_address(home_address, sizeof(home_address), host, path, is_file, is_https);
    else
        snprintf(home_address, sizeof(home_address), "Search OpenOS or type a URL");
    browser_make_home_view(summary, sizeof(summary), home_address);

    body_label = openos_gui_add_label(win, 18, 96, 860, 368, summary);
    openos_gui_set_label_options(win, body_label,
                                 OPENOS_GUI_LABEL_MULTILINE | OPENOS_GUI_LABEL_SELECTABLE | OPENOS_GUI_LABEL_COPYABLE,
                                 OPENOS_GUI_LABEL_ALIGN_LEFT);

    status_label = openos_gui_add_statusbar(win, 0, 484, BROWSER_WINDOW_W, 24, "Ready - type an address and press Enter|OpenOS Browser|", OPENOS_GUI_STATUSBAR_SIZE_GRIP | OPENOS_GUI_STATUSBAR_TOP_BORDER | OPENOS_GUI_STATUSBAR_LINK_PROMPT);
    load.window_id = win;
    load.status_label_id = status_label;
    load.body_label_id = body_label;
    load.address_label_id = address_label;
    load.tabview_id = tabview;
    load.tabs = &tabs;
    load.home_visible = 1;
    if (argc > 1 && argv && argv[1] && argv[1][0]) {
        browser_sync_address_from_target(&load, host, path, is_file, is_https);
        openos_gui_set_text(win, status_label, "Ready - press Reload to open the address");
    } else {
        load.address_text[0] = 0;
        browser_update_address_label(&load);
    }
    printf("browser: ready home\n");

    for (;;) {
        openos_gui_event_t event;
        if (browser_finish_completed_load(&load, win, status_label, body_label, tabview, &tabs, &scroll_line, &rc)) {
            openos_sleep(10);
            continue;
        }
        int ev = openos_gui_poll_event(&event);
        if (ev > 0 && event.type != OPENOS_GUI_EVENT_NONE && event.window_id == (unsigned int)win) {
            if (event.type == OPENOS_GUI_EVENT_TEXT_CHANGED && event.widget_id == (unsigned int)address_label) {
                openos_gui_get_text(win, address_label, load.address_text, sizeof(load.address_text));
                address_dirty = 1;
                openos_gui_set_text(win, status_label, "Address bar selected - type a URL, then press Enter");
                continue;
            }
            if (event.type == OPENOS_GUI_EVENT_TEXT_SUBMIT) {
                if (event.widget_id == (unsigned int)address_label || load.home_visible || address_dirty) {
                    address_dirty = 0;
                    browser_submit_address_bar(&load, &history, win, status_label, body_label, &scroll_line, &host, &path);
                    continue;
                }
            }
            if (event.type == OPENOS_GUI_EVENT_VALUE_CHANGED && event.widget_id == (unsigned int)tabview) {
                int closed = 0;
                browser_tab_save_current(&tabs, &load, scroll_line);
                if (browser_tabs_sync_from_widget(win, &tabs, tabview, &closed) == 0) {
                    history.count = 0;
                    history.current = -1;
                    rc = 0;
                    address_dirty = 0;
                    browser_tab_restore_current(&tabs, &load, win, status_label, body_label, address_label, tabview, &scroll_line);
                }
                continue;
            }
            if (event.type == OPENOS_GUI_EVENT_KEY_DOWN || event.type == OPENOS_GUI_EVENT_TEXT_INPUT) {
                if (event.type == OPENOS_GUI_EVENT_KEY_DOWN && event.key == OPENOS_GUI_KEY_TAB) {
                    browser_focus_next(&load, win, status_label, body_label, scroll_line);
                    continue;
                }
                if (event.type == OPENOS_GUI_EVENT_KEY_DOWN && (event.key == OPENOS_GUI_KEY_ENTER || event.key == '\n' || event.key == '\r')) {
                    char address_snapshot[BROWSER_ADDRESS_MAX];
                    address_snapshot[0] = 0;
                    openos_gui_get_text(win, address_label, address_snapshot, sizeof(address_snapshot));
                    if (event.widget_id == (unsigned int)address_label ||
                        load.home_visible ||
                        address_dirty ||
                        address_snapshot[0]) {
                        address_dirty = 0;
                        browser_submit_address_bar(&load, &history, win, status_label, body_label, &scroll_line, &host, &path);
                    } else if (load.focus_mode == BROWSER_FOCUS_FORM) {
                        browser_submit_current_form(&load, &history, win, status_label, body_label, &scroll_line);
                    } else {
                        browser_open_selected_link(&load, &history, win, status_label, body_label, &scroll_line);
                    }
                    continue;
                }
                if (ob_form_state_handle_key(&load.form_state, event.key)) {
                    load.focus_mode = BROWSER_FOCUS_FORM;
                    browser_refresh_form_body(&load, body_label, scroll_line);
                    browser_update_form_status(win, status_label, &load);
                }
                continue;
            }
            if (event.widget_id == (unsigned int)address_label) {
                if (load.home_visible && event.type == OPENOS_GUI_EVENT_MOUSE_DOWN) {
                    snprintf(load.address_text, sizeof(load.address_text), "");
                    openos_gui_set_text_cursor(win, address_label, "", 0);
                }
                openos_gui_set_text(win, status_label, "Address bar selected - type a URL, then press Enter");
            } else if (event.widget_id == (unsigned int)new_tab_button) {
                browser_tab_save_current(&tabs, &load, scroll_line);
                if (browser_tabs_new(win, &tabs, tabview) == 0) {
                    char new_home[BROWSER_BODY_MAX];
                    history.count = 0;
                    history.current = -1;
                    scroll_line = 0;
                    rc = 0;
                    memset(&load, 0, sizeof(load));
                    load.window_id = win;
                    load.status_label_id = status_label;
                    load.body_label_id = body_label;
                    load.address_label_id = address_label;
                    load.tabview_id = tabview;
                    load.tabs = &tabs;
                    load.home_visible = 1;
                    load.address_text[0] = 0;
                    browser_update_address_label(&load);
                    browser_make_home_view(new_home, sizeof(new_home), "Search OpenOS or type a URL");
                    openos_gui_set_text(win, body_label, new_home);
                    openos_gui_set_text(win, status_label, "New tab - type an address and press Enter");
                } else {
                    openos_gui_set_text(win, status_label, "Could not create new tab");
                }
            } else if (event.widget_id == (unsigned int)load_button) {
                const browser_history_entry_t *cur = browser_history_current(&history);
                if (load.home_visible) {
                    browser_submit_address_bar(&load, &history, win, status_label, body_label, &scroll_line, &host, &path);
                } else if (cur) {
                    scroll_line = 0;
                    browser_sync_address_from_target(&load, cur->host, cur->path, cur->is_file, cur->is_https);
                    browser_start_load(&load, win, status_label, body_label, cur->host, cur->path, cur->is_file, cur->is_https);
                }
            } else if (event.widget_id == (unsigned int)back_button) {
                if (browser_history_go(&history, -1) == 0) {
                    const browser_history_entry_t *cur = browser_history_current(&history);
                    scroll_line = 0;
                    browser_sync_address_from_target(&load, cur->host, cur->path, cur->is_file, cur->is_https);
                    if (browser_cache_restore(&load, win, status_label, body_label, tabview, &tabs, cur->host, cur->path, cur->is_file, cur->is_https, &scroll_line) != 0)
                        browser_start_load(&load, win, status_label, body_label, cur->host, cur->path, cur->is_file, cur->is_https);
                } else {
                    openos_gui_set_text(win, status_label, "Back: no history");
                }
            } else if (event.widget_id == (unsigned int)forward_button) {
                if (browser_history_go(&history, 1) == 0) {
                    const browser_history_entry_t *cur = browser_history_current(&history);
                    scroll_line = 0;
                    browser_sync_address_from_target(&load, cur->host, cur->path, cur->is_file, cur->is_https);
                    if (browser_cache_restore(&load, win, status_label, body_label, tabview, &tabs, cur->host, cur->path, cur->is_file, cur->is_https, &scroll_line) != 0)
                        browser_start_load(&load, win, status_label, body_label, cur->host, cur->path, cur->is_file, cur->is_https);
                } else {
                    openos_gui_set_text(win, status_label, "Forward: no history");
                }
            }
        }
        browser_finish_completed_load(&load, win, status_label, body_label, tabview, &tabs, &scroll_line, &rc);
        openos_sleep(10);
    }

    browser_tabs_destroy(&tabs);
    openos_gui_destroy_window(win);
    return rc < 0 ? 1 : 0;
}
