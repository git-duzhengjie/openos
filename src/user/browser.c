#include "openos.h"
#include "browser_engine.h"

#define BROWSER_DEFAULT_HOST "example.com"
#define BROWSER_DEFAULT_PATH "/"
#define BROWSER_HTTP_PORT "80"
#define BROWSER_RECV_MAX 1536
#define BROWSER_NET_WAIT_TRIES 80
#define BROWSER_DNS_RETRIES 8
#define BROWSER_CONNECT_TIMEOUT_MS 4000
#define BROWSER_RESPONSE_TIMEOUT_MS 6000
#define BROWSER_POLL_SLICE_MS 100
#define BROWSER_RECV_CHUNK_MAX 1400
#define BROWSER_HOST_MAX 128
#define BROWSER_PATH_MAX 256
#define BROWSER_BODY_MAX 768
#define BROWSER_TITLE_MAX 96
#define BROWSER_STATUS_MAX 96
#define BROWSER_HISTORY_MAX 8
#define BROWSER_LINK_MAX 8
#define BROWSER_VIEW_LINES 6
#define BROWSER_LINE_MAX 72
#define BROWSER_ADDRESS_MAX 256

typedef struct browser_load_context {
    volatile int active;
    volatile int done;
    volatile int result;
    int is_file;
    int window_id;
    int status_label_id;
    int body_label_id;
    char host[BROWSER_HOST_MAX];
    char path[BROWSER_PATH_MAX];
    char response[BROWSER_RECV_MAX + 1];
    char body[BROWSER_BODY_MAX];
    char title[BROWSER_TITLE_MAX];
    char http_status[BROWSER_STATUS_MAX];
    char status[64];
    char links[BROWSER_LINK_MAX][OB_MAX_ATTR_VALUE];
    int link_count;
    int selected_link;
    int address_label_id;
    int address_editing;
    char address_text[BROWSER_ADDRESS_MAX];
    ob_dom_document_t dom;
    ob_form_state_t form_state;
} browser_load_context_t;

typedef struct browser_history_entry {
    char host[BROWSER_HOST_MAX];
    char path[BROWSER_PATH_MAX];
    int is_file;
} browser_history_entry_t;

typedef struct browser_history {
    browser_history_entry_t entries[BROWSER_HISTORY_MAX];
    int count;
    int current;
} browser_history_t;


static void browser_load_worker(void *arg);

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
        snprintf(out, out_size, "http://%s%s", ctx->host, ctx->path);
}

static void browser_make_error_page(browser_load_context_t *ctx, const char *summary, const char *detail)
{
    char target[BROWSER_HOST_MAX + BROWSER_PATH_MAX + 16];
    if (!ctx) return;
    browser_target_url(ctx, target, sizeof(target));
    snprintf(ctx->title, sizeof(ctx->title), "OpenOS Browser Error");
    snprintf(ctx->status, sizeof(ctx->status), "%s", summary && summary[0] ? summary : "Load failed");
    snprintf(ctx->body, sizeof(ctx->body),
             "OpenOS Browser Error\nURL: %s\nReason: %s",
             target,
             detail && detail[0] ? detail : (summary && summary[0] ? summary : "unknown error"));
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

static int browser_parse_url_arg(const char *url, char *host, int host_size, char *path, int path_size)
{
    ob_url_parts_t parts;
    char error[64];
    if (!url || !host || !path || host_size <= 0 || path_size <= 0) return -1;
    if (ob_url_parse_address(url, 0, &parts, error, sizeof(error)) != 0 || parts.is_file) return -1;
    snprintf(host, host_size, "%s", parts.host);
    snprintf(path, path_size, "%s", parts.path);
    return 0;
}

static void browser_format_address(char *out, int out_size, const char *host, const char *path, int is_file)
{
    if (!out || out_size <= 0) return;
    if (is_file) snprintf(out, out_size, "file://%s", path && path[0] ? path : "/");
    else snprintf(out, out_size, "http://%s%s", host && host[0] ? host : BROWSER_DEFAULT_HOST, path && path[0] ? path : "/");
}

static void browser_update_address_label(browser_load_context_t *ctx)
{
    char label[BROWSER_ADDRESS_MAX + 16];
    if (!ctx || ctx->window_id <= 0 || ctx->address_label_id <= 0) return;
    snprintf(label, sizeof(label), "%s%s", ctx->address_editing ? "URL*: " : "URL: ", ctx->address_text);
    openos_gui_set_text(ctx->window_id, ctx->address_label_id, label);
}

static void browser_sync_address_from_target(browser_load_context_t *ctx, const char *host, const char *path, int is_file)
{
    if (!ctx) return;
    browser_format_address(ctx->address_text, sizeof(ctx->address_text), host, path, is_file);
    browser_update_address_label(ctx);
}

static int browser_address_handle_key(browser_load_context_t *ctx, unsigned int key, char *host, int host_size, char *path, int path_size, int *is_file, char *error, int error_size)
{
    int len;
    ob_url_parts_t parts;
    if (!ctx) return 0;
    if (!ctx->address_editing) ctx->address_editing = 1;
    if (key == OPENOS_GUI_KEY_ESCAPE) {
        ctx->address_editing = 0;
        browser_update_address_label(ctx);
        return 0;
    }
    if (key == OPENOS_GUI_KEY_BACKSPACE || key == 127u) {
        len = (int)strlen(ctx->address_text);
        if (len > 0) ctx->address_text[len - 1] = 0;
        browser_update_address_label(ctx);
        return 0;
    }
    if (key == OPENOS_GUI_KEY_ENTER || key == '\n') {
        if (ob_url_parse_address(ctx->address_text, host && host[0] ? host : BROWSER_DEFAULT_HOST, &parts, error, error_size) != 0) return -1;
        if (host && host_size > 0) snprintf(host, host_size, "%s", parts.host);
        if (path && path_size > 0) snprintf(path, path_size, "%s", parts.path);
        if (is_file) *is_file = parts.is_file;
        ctx->address_editing = 0;
        browser_sync_address_from_target(ctx, parts.host, parts.path, parts.is_file);
        return 1;
    }
    if (key >= 32u && key <= 126u) {
        if (ctx->address_editing == 1) {
            ctx->address_text[0] = 0;
            ctx->address_editing = 2;
        }
        len = (int)strlen(ctx->address_text);
        if (len < (int)sizeof(ctx->address_text) - 1) {
            ctx->address_text[len] = (char)key;
            ctx->address_text[len + 1] = 0;
        }
        browser_update_address_label(ctx);
    }
    return 0;
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

static void browser_history_init(browser_history_t *history, const char *host, const char *path, int is_file)
{
    if (!history) return;
    memset(history, 0, sizeof(*history));
    snprintf(history->entries[0].host, sizeof(history->entries[0].host), "%s", host ? host : BROWSER_DEFAULT_HOST);
    snprintf(history->entries[0].path, sizeof(history->entries[0].path), "%s", path ? path : BROWSER_DEFAULT_PATH);
    history->entries[0].is_file = is_file;
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

static void browser_history_push(browser_history_t *history, const char *host, const char *path, int is_file)
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

static int browser_resolve_link(const browser_history_entry_t *base, const char *href, char *host, int host_size, char *path, int path_size, int *is_file, char *error, int error_size)
{
    if (!base || !href || !href[0] || !host || !path || !is_file) return -1;
    host[0] = 0;
    path[0] = 0;
    *is_file = base->is_file;
    if (browser_match_token_ci(href, "https://")) {
        if (error && error_size > 0) snprintf(error, error_size, "HTTPS links are not supported yet");
        return -1;
    }
    if (browser_match_token_ci(href, "http://")) {
        *is_file = 0;
        return browser_parse_url_arg(href, host, host_size, path, path_size);
    }
    if (browser_match_token_ci(href, "file://")) {
        *is_file = 1;
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
    } else {
        snprintf(host, host_size, "%s", base->host);
        browser_join_relative_path(path, path_size, base->path, href);
        *is_file = 0;
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

static void browser_make_view(char *out, int out_size, const browser_load_context_t *load, int scroll_line)
{
    int line = 0;
    int written = 0;
    const char *body;
    if (!out || out_size <= 0) return;
    out[0] = 0;
    if (!load) return;
    if (load->title[0]) {
        written += snprintf(out + written, out_size - written, "%s\n\n", load->title);
    }
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
                               const char *host, const char *path, int is_file)
{
    openos_thread_t tid;
    char loading[128];
    if (!load || !host || !path) return;
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
    snprintf(loading, sizeof(loading), "Loading %s%s%s ...", is_file ? "file://" : "http://", is_file ? "" : load->host, load->path);
    load->active = 1;
    load->done = 0;
    load->result = -1;
    openos_gui_set_text(win, status_label, loading);
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
    openos_close(fd);
    return 0;
}

static int browser_fetch_http(const char *host, const char *path, char *out, int out_size)
{
    int fd;
    int total = 0;
    char request[256];
    unsigned int dst_ip = 0;
    unsigned short dst_port = 80;

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
        while (waited < BROWSER_RESPONSE_TIMEOUT_MS && total < out_size - 1) {
            int ready = browser_poll_fd(fd, OPENOS_POLLIN, BROWSER_POLL_SLICE_MS);
            if (ready < 0) {
                snprintf(out, out_size, "recv() poll failed from %s", host);
                openos_close(fd);
                return -1;
            }
            if (ready == 0) {
                waited += BROWSER_POLL_SLICE_MS;
                continue;
            }

            unsigned int room = (unsigned int)(out_size - 1 - total);
            unsigned int chunk = room > BROWSER_RECV_CHUNK_MAX ? BROWSER_RECV_CHUNK_MAX : room;
            int n = openos_recv(fd, out + total, chunk, 0);
            if (n < 0) {
                snprintf(out, out_size, "recv() failed from %s", host);
                openos_close(fd);
                return -1;
            }
            if (n == 0)
                break;
            total += n;
            out[total] = 0;
            if (total >= BROWSER_RECV_MAX)
                break;
        }
    }
    out[total] = 0;

    openos_close(fd);
    if (total <= 0) {
        snprintf(out, out_size, "HTTP response timeout from %s", host);
        return -1;
    }
    return 0;
}


static void browser_render_current_view(browser_load_context_t *load, int body_label, int scroll_line)
{
    char view[BROWSER_BODY_MAX + BROWSER_TITLE_MAX + 32];
    if (!load) return;
    browser_make_view(view, sizeof(view), load, scroll_line);
    openos_gui_set_text(load->window_id, body_label, view[0] ? view : "Empty response");
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
    if (!load || load->form_state.count <= 0 || load->form_state.focused < 0) {
        openos_gui_set_text(win, status_label, "No editable form fields");
        return;
    }
    snprintf(status, sizeof(status), "Field %d/%d: %s", load->form_state.focused + 1, load->form_state.count,
             load->form_state.controls[load->form_state.focused].name[0] ? load->form_state.controls[load->form_state.focused].name : "input");
    openos_gui_set_text(win, status_label, status);
}

static void browser_load_worker(void *arg)
{
    browser_load_context_t *ctx = (browser_load_context_t *)arg;
    int rc;

    if (!ctx)
        return;

    rc = ctx->is_file ? browser_fetch_file(ctx->path, ctx->response, sizeof(ctx->response))
                      : browser_fetch_http(ctx->host, ctx->path, ctx->response, sizeof(ctx->response));
    ctx->result = rc;
    if (rc < 0) {
        browser_make_error_page(ctx, ctx->is_file ? "File load failed" : "Network load failed", ctx->response);
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
        if (!ctx->is_file && (http_code < 200 || http_code >= 300)) {
            char detail[BROWSER_BODY_MAX / 2];
            ctx->result = -2;
            snprintf(detail, sizeof(detail),
                     "server returned non-success status %s. Response preview: %.220s",
                     ctx->http_status[0] ? ctx->http_status : "HTTP status unknown",
                     body && body[0] ? body : "<empty response body>");
            browser_make_error_page(ctx, ctx->http_status[0] ? ctx->http_status : "HTTP error", detail);
            printf("browser: HTTP error http://%s%s %s\n", ctx->host, ctx->path, ctx->status);
        } else {
            browser_extract_title(ctx->title, sizeof(ctx->title), body);
            {
                ob_html_parser_base_t parser;
                ob_dom_text_renderer_base_t renderer;
                ob_dom_document_t doc;
                ob_html_parser_base_init(&parser);
                ob_dom_text_renderer_base_init(&renderer);
                if (parser.iface.parse(&parser.iface, body, &doc) > 0) {
                    int i;
                    ctx->link_count = 0;
                    ctx->selected_link = 0;
                    for (i = 0; i < doc.count && ctx->link_count < BROWSER_LINK_MAX; ++i) {
                        if (doc.nodes[i].type == OB_DOM_NODE_ELEMENT && ob_token_eq_ci(doc.nodes[i].name, "a") && doc.nodes[i].href[0]) {
                            snprintf(ctx->links[ctx->link_count], sizeof(ctx->links[ctx->link_count]), "%s", doc.nodes[i].href);
                            ++ctx->link_count;
                        }
                    }
                    ob_dom_document_copy(&ctx->dom, &doc);
                    ob_form_state_collect_from_dom(&ctx->form_state, &ctx->dom);
                    ob_dom_text_render_with_form_state(&ctx->dom, &ctx->form_state, ctx->body, sizeof(ctx->body));
                } else
                    ctx->body[0] = 0;
            }
            if (!ctx->body[0])
                snprintf(ctx->body, sizeof(ctx->body), "Empty response");
            snprintf(ctx->status, sizeof(ctx->status), "%s", ctx->http_status[0] ? ctx->http_status : "Done");
            printf("browser: loaded %s%s%s %s\n", ctx->is_file ? "file://" : "http://", ctx->is_file ? "" : ctx->host, ctx->path, ctx->status);
            if (ctx->title[0]) printf("title: %s\n", ctx->title);
            printf("%s\n", ctx->body);
        }
    }

    if (ctx->window_id >= 0 && ctx->status_label_id >= 0 && ctx->body_label_id >= 0) {
        char view[BROWSER_BODY_MAX + BROWSER_TITLE_MAX + 32];
        openos_gui_set_text(ctx->window_id, ctx->status_label_id,
                            ctx->status[0] ? ctx->status : (rc < 0 ? "Failed" : "Done"));
        if (ctx->title[0])
            snprintf(view, sizeof(view), "%s\n\n%s", ctx->title, ctx->body[0] ? ctx->body : "Empty response");
        else
            snprintf(view, sizeof(view), "%s", ctx->body[0] ? ctx->body : (rc < 0 ? "Load failed" : "Empty response"));
        openos_gui_set_text(ctx->window_id, ctx->body_label_id, view);
    }
    ctx->done = 1;
}

int main(int argc, char **argv)
{
    const char *host = BROWSER_DEFAULT_HOST;
    const char *path = BROWSER_DEFAULT_PATH;
    int is_file = 0;
    char summary[160];
    int win;
    int status_label;
    int address_label;
    int body_label;
    int load_button;
    int back_button;
    int forward_button;
    int up_button;
    int down_button;
    int next_link_button;
    int open_link_button;
    int next_field_button;
    int submit_button;
    int close_button;
    int rc = 0;
    int scroll_line = 0;
    browser_load_context_t load;
    browser_history_t history;

    memset(&load, 0, sizeof(load));

    if (argc > 1 && argv && argv[1] && argv[1][0]) {
        if (browser_match_token_ci(argv[1], "file://") || argv[1][0] == '/') {
            if (browser_parse_file_arg(argv[1], load.path, sizeof(load.path)) == 0) {
                host = "";
                path = load.path;
                is_file = 1;
            }
        } else if (strstr(argv[1], "://") || strchr(argv[1], '/')) {
            if (browser_parse_url_arg(argv[1], load.host, sizeof(load.host), load.path, sizeof(load.path)) == 0) {
                host = load.host;
                path = load.path;
            } else {
                host = argv[1];
            }
        } else {
            host = argv[1];
        }
    }
    if (argc > 2 && argv && argv[2] && argv[2][0]) { path = argv[2]; is_file = 0; }
    if (!is_file && (!host || !host[0])) host = BROWSER_DEFAULT_HOST;
    if (!path || !path[0]) path = BROWSER_DEFAULT_PATH;

    browser_history_init(&history, host, path, is_file);

    win = openos_gui_create_window("用户态浏览器", 80, 80, 700, 332);
    if (win < 0) {
        printf("browser: failed to create GUI window\n");
        return 1;
    }

    status_label = openos_gui_add_label(win, 16, 24, 640, 20, "Ready");
    address_label = openos_gui_add_label(win, 16, 48, 650, 20, "URL: ");
    snprintf(summary, sizeof(summary), "Ready: %s%s%s", is_file ? "file://" : "http://", is_file ? "" : host, path);
    body_label = openos_gui_add_label(win, 16, 76, 650, 150, summary);
    load_button = openos_gui_add_button(win, 16, 248, 80, 24, "Refresh");
    back_button = openos_gui_add_button(win, 104, 248, 64, 24, "Back");
    forward_button = openos_gui_add_button(win, 176, 248, 72, 24, "Forward");
    up_button = openos_gui_add_button(win, 256, 248, 56, 24, "Up");
    down_button = openos_gui_add_button(win, 320, 248, 56, 24, "Down");
    next_link_button = openos_gui_add_button(win, 384, 248, 72, 24, "NextLink");
    open_link_button = openos_gui_add_button(win, 464, 248, 72, 24, "OpenLink");
    next_field_button = openos_gui_add_button(win, 544, 248, 80, 24, "NextField");
    submit_button = openos_gui_add_button(win, 16, 280, 80, 24, "Submit");
    close_button = openos_gui_add_button(win, 632, 280, 56, 24, "Close");
    load.window_id = win;
    load.status_label_id = status_label;
    load.body_label_id = body_label;
    load.address_label_id = address_label;
    browser_sync_address_from_target(&load, host, path, is_file);
    printf("browser: ready %s%s%s\n", is_file ? "file://" : "http://", is_file ? "" : host, path);

    for (;;) {
        openos_gui_event_t event;
        int ev = openos_gui_poll_event(&event);
        if (ev == 0 && event.type != OPENOS_GUI_EVENT_NONE && event.window_id == (unsigned int)win) {
            if (event.type == OPENOS_GUI_EVENT_KEY_DOWN || event.type == OPENOS_GUI_EVENT_TEXT_INPUT) {
                char next_host[BROWSER_HOST_MAX];
                char next_path[BROWSER_PATH_MAX];
                char error[96];
                int next_is_file = 0;
                int key_rc;
                snprintf(next_host, sizeof(next_host), "%s", host ? host : "");
                snprintf(next_path, sizeof(next_path), "%s", path ? path : "/");
                error[0] = 0;
                if (load.address_editing || load.form_state.count <= 0) {
                    key_rc = browser_address_handle_key(&load, event.key, next_host, sizeof(next_host), next_path, sizeof(next_path), &next_is_file, error, sizeof(error));
                    if (key_rc < 0) {
                        openos_gui_set_text(win, status_label, error[0] ? error : "Invalid address");
                    } else if (key_rc > 0) {
                        host = next_is_file ? "" : load.host;
                        path = load.path;
                        snprintf(load.host, sizeof(load.host), "%s", next_host);
                        snprintf(load.path, sizeof(load.path), "%s", next_path);
                        browser_history_push(&history, next_host, next_path, next_is_file);
                        scroll_line = 0;
                        browser_start_load(&load, win, status_label, body_label, next_host, next_path, next_is_file);
                    }
                } else if (ob_form_state_handle_key(&load.form_state, event.key)) {
                    browser_refresh_form_body(&load, body_label, scroll_line);
                    browser_update_form_status(win, status_label, &load);
                }
                continue;
            }
            if (event.widget_id == (unsigned int)close_button)
                break;
            if (event.widget_id == (unsigned int)load_button) {
                const browser_history_entry_t *cur = browser_history_current(&history);
                if (cur) {
                    scroll_line = 0;
                    browser_sync_address_from_target(&load, cur->host, cur->path, cur->is_file);
                    browser_start_load(&load, win, status_label, body_label, cur->host, cur->path, cur->is_file);
                }
            } else if (event.widget_id == (unsigned int)back_button) {
                if (browser_history_go(&history, -1) == 0) {
                    const browser_history_entry_t *cur = browser_history_current(&history);
                    scroll_line = 0;
                    browser_sync_address_from_target(&load, cur->host, cur->path, cur->is_file);
                    browser_start_load(&load, win, status_label, body_label, cur->host, cur->path, cur->is_file);
                } else {
                    openos_gui_set_text(win, status_label, "Back: no history");
                }
            } else if (event.widget_id == (unsigned int)forward_button) {
                if (browser_history_go(&history, 1) == 0) {
                    const browser_history_entry_t *cur = browser_history_current(&history);
                    scroll_line = 0;
                    browser_sync_address_from_target(&load, cur->host, cur->path, cur->is_file);
                    browser_start_load(&load, win, status_label, body_label, cur->host, cur->path, cur->is_file);
                } else {
                    openos_gui_set_text(win, status_label, "Forward: no history");
                }
            } else if (event.widget_id == (unsigned int)next_link_button) {
                if (load.link_count <= 0) {
                    openos_gui_set_text(win, status_label, "No links");
                } else {
                    load.selected_link = (load.selected_link + 1) % load.link_count;
                    browser_update_link_status(win, status_label, &load);
                }
            } else if (event.widget_id == (unsigned int)next_field_button) {
                load.address_editing = 0;
                if (ob_form_state_focus_next(&load.form_state) >= 0) {
                    browser_refresh_form_body(&load, body_label, scroll_line);
                    browser_update_form_status(win, status_label, &load);
                } else {
                    openos_gui_set_text(win, status_label, "No editable form fields");
                }
            } else if (event.widget_id == (unsigned int)submit_button) {
                const browser_history_entry_t *cur = browser_history_current(&history);
                char action_url[BROWSER_ADDRESS_MAX];
                char next_host[BROWSER_HOST_MAX];
                char next_path[BROWSER_PATH_MAX];
                char error[96];
                int next_is_file = 0;
                int submit_id = -1;
                int build_rc;
                error[0] = 0;
                if (load.form_state.count > 0 && load.form_state.focused >= 0 && load.form_state.focused < load.form_state.count)
                    submit_id = load.form_state.controls[load.form_state.focused].node_id;
                build_rc = ob_form_build_get_url(&load.dom, &load.form_state, submit_id,
                                                  cur ? cur->path : path, action_url, sizeof(action_url));
                if (!cur || build_rc < 0) {
                    openos_gui_set_text(win, status_label, build_rc == -2 ? "Submit: only GET forms supported" : "Submit: no GET form target");
                } else if (browser_resolve_link(cur, action_url, next_host, sizeof(next_host), next_path, sizeof(next_path), &next_is_file, error, sizeof(error)) == 0) {
                    scroll_line = 0;
                    browser_history_push(&history, next_host, next_path, next_is_file);
                    browser_sync_address_from_target(&load, next_host, next_path, next_is_file);
                    browser_start_load(&load, win, status_label, body_label, next_host, next_path, next_is_file);
                } else {
                    openos_gui_set_text(win, status_label, error[0] ? error : "Submit: unsupported target");
                }
            } else if (event.widget_id == (unsigned int)open_link_button) {
                const browser_history_entry_t *cur = browser_history_current(&history);
                char selected_href[OB_MAX_ATTR_VALUE];
                char next_host[BROWSER_HOST_MAX];
                char next_path[BROWSER_PATH_MAX];
                char error[96];
                int next_is_file = 0;
                selected_href[0] = 0;
                error[0] = 0;
                if (load.link_count > 0) snprintf(selected_href, sizeof(selected_href), "%s", load.links[load.selected_link]);
                if (!cur || !selected_href[0]) {
                    openos_gui_set_text(win, status_label, "OpenLink: no link");
                } else if (browser_resolve_link(cur, selected_href, next_host, sizeof(next_host), next_path, sizeof(next_path), &next_is_file, error, sizeof(error)) == 0) {
                    scroll_line = 0;
                    browser_history_push(&history, next_host, next_path, next_is_file);
                    browser_sync_address_from_target(&load, next_host, next_path, next_is_file);
                    browser_start_load(&load, win, status_label, body_label, next_host, next_path, next_is_file);
                } else {
                    openos_gui_set_text(win, status_label, error[0] ? error : "OpenLink: unsupported link");
                }
            } else if (event.widget_id == (unsigned int)up_button) {
                char view[BROWSER_BODY_MAX + BROWSER_TITLE_MAX + 32];
                if (scroll_line > 0) --scroll_line;
                browser_make_view(view, sizeof(view), &load, scroll_line);
                openos_gui_set_text(win, body_label, view[0] ? view : "Nothing to scroll");
            } else if (event.widget_id == (unsigned int)down_button) {
                char view[BROWSER_BODY_MAX + BROWSER_TITLE_MAX + 32];
                ++scroll_line;
                browser_make_view(view, sizeof(view), &load, scroll_line);
                openos_gui_set_text(win, body_label, view[0] ? view : "End of page");
            }
        }
        if (load.active && load.done) {
            rc = load.result;
            openos_gui_set_text(win, status_label, load.status[0] ? load.status : (rc < 0 ? "Failed" : "Done"));
            {
                char view[BROWSER_BODY_MAX + BROWSER_TITLE_MAX + 32];
                scroll_line = 0;
                browser_make_view(view, sizeof(view), &load, scroll_line);
                openos_gui_set_text(win, body_label, view);
                if (load.link_count > 0) browser_update_link_status(win, status_label, &load);
            }
            load.active = 0;
        }
        openos_sleep(10);
    }

    openos_gui_destroy_window(win);
    return rc < 0 ? 1 : 0;
}
