#include "openos.h"

#define CHROME_W 640
#define CHROME_H 360
#define CHROME_DEFAULT_URL "http://example.com/"
#define CHROME_RECV_MAX 4096
#define CHROME_RENDER_TEXT_MAX 900
#define CHROME_NET_WAIT_TRIES 80
#define CHROME_DNS_RETRIES 8
#define CHROME_CONNECT_TIMEOUT_MS 4000
#define CHROME_RESPONSE_TIMEOUT_MS 6000
#define CHROME_POLL_SLICE_MS 100
#define CHROME_RECV_CHUNK_MAX 1400

#define CHROME_WIDGET_BACK 1
#define CHROME_WIDGET_FORWARD 2
#define CHROME_WIDGET_REFRESH 3
#define CHROME_WIDGET_GO 4
#define CHROME_WIDGET_DOWNLOAD 5
#define CHROME_WIDGET_CLOSE 6
#define CHROME_WIDGET_ADDRESS 7
#define CHROME_WIDGET_STATUS 8
#define CHROME_WIDGET_BODY 9

typedef struct chrome_url {
    char scheme[8];
    char host[96];
    char path[192];
} chrome_url_t;

typedef struct chrome_page {
    chrome_url_t url;
    char display_url[256];
    char status[128];
    char body[CHROME_RENDER_TEXT_MAX];
    char raw[CHROME_RECV_MAX + 1];
    int loaded;
} chrome_page_t;

static void chrome_format_ip(unsigned int ip, char *out, int out_size)
{
    if (!out || out_size <= 0) return;
    snprintf(out, out_size, "%u.%u.%u.%u",
             (ip >> 24) & 0xffU,
             (ip >> 16) & 0xffU,
             (ip >> 8) & 0xffU,
             ip & 0xffU);
}

static void chrome_copy_text(char *dst, int dst_size, const char *src)
{
    if (!dst || dst_size <= 0) return;
    if (!src) src = "";
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = 0;
}

static void chrome_parse_url(const char *url, chrome_url_t *out)
{
    const char *p;
    const char *slash;
    int n;

    memset(out, 0, sizeof(*out));
    chrome_copy_text(out->scheme, sizeof(out->scheme), "http");
    chrome_copy_text(out->host, sizeof(out->host), "example.com");
    chrome_copy_text(out->path, sizeof(out->path), "/");
    if (!url || !*url) return;

    p = strstr(url, "://");
    if (p) {
        n = (int)(p - url);
        if (n > 0) {
            if (n >= (int)sizeof(out->scheme)) n = (int)sizeof(out->scheme) - 1;
            memcpy(out->scheme, url, n);
            out->scheme[n] = 0;
        }
        p += 3;
    } else {
        p = url;
    }

    slash = strchr(p, '/');
    if (!slash) {
        chrome_copy_text(out->host, sizeof(out->host), p);
        chrome_copy_text(out->path, sizeof(out->path), "/");
        return;
    }

    n = (int)(slash - p);
    if (n > 0) {
        if (n >= (int)sizeof(out->host)) n = (int)sizeof(out->host) - 1;
        memcpy(out->host, p, n);
        out->host[n] = 0;
    }
    chrome_copy_text(out->path, sizeof(out->path), slash);
}

static void chrome_format_url(const chrome_url_t *url, char *out, int out_size)
{
    snprintf(out, out_size, "%s://%s%s", url->scheme, url->host, url->path);
}

static int chrome_wait_for_network(char *out, int out_size)
{
    openos_netinfo_t info;
    int i;

    for (i = 0; i < CHROME_NET_WAIT_TRIES; ++i) {
        if (openos_netinfo(&info) == 0 && info.ip != 0 && info.gateway != 0 && info.dns != 0)
            return 0;
        openos_sleep(5);
    }

    if (openos_netinfo(&info) == 0) {
        char ip[24];
        char gw[24];
        char dns[24];
        chrome_format_ip(info.ip, ip, sizeof(ip));
        chrome_format_ip(info.gateway, gw, sizeof(gw));
        chrome_format_ip(info.dns, dns, sizeof(dns));
        snprintf(out, out_size, "network not ready: ip=%s gw=%s dns=%s", ip, gw, dns);
    } else {
        snprintf(out, out_size, "network not ready: no network device");
    }
    return -1;
}

static int chrome_poll_fd(int fd, unsigned int events, unsigned int timeout_ms)
{
    openos_pollfd_t pfd;
    int rc;

    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = fd;
    pfd.events = (short)events;
    pfd.revents = 0;

    rc = openos_poll(&pfd, 1, timeout_ms);
    if (rc > 0) {
        if (pfd.revents & (OPENOS_POLLERR | OPENOS_POLLHUP)) return -1;
        if (pfd.revents & events) return 1;
        return -1;
    }
    if (rc < 0) return -1;
    return 0;
}

static void chrome_connect_error(char *out, int out_size, const char *host, unsigned int dst_ip)
{
    openos_netinfo_t info;
    char dst[24] = "0.0.0.0";
    char ip[24] = "0.0.0.0";
    char gw[24] = "0.0.0.0";
    char dns[24] = "0.0.0.0";
    chrome_format_ip(dst_ip, dst, sizeof(dst));
    if (openos_netinfo(&info) == 0) {
        chrome_format_ip(info.ip, ip, sizeof(ip));
        chrome_format_ip(info.gateway, gw, sizeof(gw));
        chrome_format_ip(info.dns, dns, sizeof(dns));
    }
    snprintf(out, out_size, "connect failed: host=%s dst=%s ip=%s gw=%s dns=%s", host, dst, ip, gw, dns);
}

static const char *chrome_find_body(const char *response)
{
    const char *body = strstr(response, "\r\n\r\n");
    if (body) return body + 4;
    body = strstr(response, "\n\n");
    if (body) return body + 2;
    return response;
}

static int chrome_status_code(const char *response)
{
    int code = 0;
    if (!response || strncmp(response, "HTTP/", 5) != 0) return 0;
    while (*response && *response != ' ') response++;
    while (*response == ' ') response++;
    while (*response >= '0' && *response <= '9') {
        code = code * 10 + (*response - '0');
        response++;
    }
    return code;
}

static void chrome_collapse_html(char *dst, int dst_size, const char *src)
{
    int out = 0;
    int in_tag = 0;
    int pending_space = 0;
    int line_len = 0;

    if (!dst || dst_size <= 0) return;
    dst[0] = 0;
    if (!src) return;

    for (; *src && out < dst_size - 1; ++src) {
        char c = *src;
        if (c == '<') {
            in_tag = 1;
            pending_space = 1;
            continue;
        }
        if (in_tag) {
            if (c == '>') in_tag = 0;
            continue;
        }
        if (c == '&') {
            if (!strncmp(src, "&amp;", 5)) { c = '&'; src += 4; }
            else if (!strncmp(src, "&lt;", 4)) { c = '<'; src += 3; }
            else if (!strncmp(src, "&gt;", 4)) { c = '>'; src += 3; }
            else if (!strncmp(src, "&quot;", 6)) { c = '"'; src += 5; }
            else if (!strncmp(src, "&nbsp;", 6)) { c = ' '; src += 5; }
        }
        if (c == '\r' || c == '\n' || c == '\t' || c == ' ') {
            pending_space = 1;
            continue;
        }
        if (pending_space && out > 0 && dst[out - 1] != '\n') {
            dst[out++] = ' ';
            line_len++;
            if (out >= dst_size - 1) break;
        }
        pending_space = 0;
        if (line_len >= 76 && out < dst_size - 2) {
            dst[out++] = '\n';
            line_len = 0;
        }
        dst[out++] = c;
        line_len++;
    }
    dst[out] = 0;
}

static int chrome_fetch_http(const chrome_url_t *url, char *out, int out_size, char *err, int err_size)
{
    int fd;
    int total = 0;
    char request[320];
    unsigned int dst_ip = 0;
    unsigned short dst_port = 80;
    int attempt;

    if (!url || !out || out_size <= 0) return -1;
    out[0] = 0;
    if (strcmp(url->scheme, "http") != 0) {
        snprintf(err, err_size, "unsupported scheme: %s (only http is available)", url->scheme);
        return -1;
    }
    if (chrome_wait_for_network(err, err_size) < 0) return -1;

    for (attempt = 0; attempt < CHROME_DNS_RETRIES; ++attempt) {
        if (openos_dnslookup(url->host, &dst_ip) == 0 && dst_ip != 0) break;
        dst_ip = 0;
        openos_sleep(10);
    }
    if (dst_ip == 0) {
        snprintf(err, err_size, "DNS failed for %s", url->host);
        return -1;
    }

    fd = openos_socket(OPENOS_AF_INET, OPENOS_SOCK_STREAM, 0);
    if (fd < 0) {
        snprintf(err, err_size, "socket() failed");
        return -1;
    }

    {
        openos_sockaddr_in_t addr;
        int ready;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = OPENOS_AF_INET;
        addr.sin_port = openos_htons(dst_port);
        addr.sin_addr = dst_ip;
        if (openos_connect(fd, (openos_sockaddr_t *)&addr, sizeof(addr)) < 0) {
            chrome_connect_error(err, err_size, url->host, dst_ip);
            openos_close(fd);
            return -1;
        }
        ready = chrome_poll_fd(fd, OPENOS_POLLOUT, CHROME_CONNECT_TIMEOUT_MS);
        if (ready <= 0) {
            chrome_connect_error(err, err_size, url->host, dst_ip);
            openos_close(fd);
            return -1;
        }
    }

    snprintf(request, sizeof(request),
             "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: OpenOS-Chromium/0.1\r\nAccept: text/html,*/*\r\nConnection: close\r\n\r\n",
             url->path, url->host);
    if (openos_send(fd, request, strlen(request), 0) < 0) {
        openos_close(fd);
        snprintf(err, err_size, "send() failed");
        return -1;
    }

    {
        unsigned int waited = 0;
        while (waited < CHROME_RESPONSE_TIMEOUT_MS && total < out_size - 1) {
            int ready = chrome_poll_fd(fd, OPENOS_POLLIN, CHROME_POLL_SLICE_MS);
            if (ready < 0) {
                snprintf(err, err_size, "recv poll failed from %s", url->host);
                openos_close(fd);
                return -1;
            }
            if (ready == 0) {
                waited += CHROME_POLL_SLICE_MS;
                continue;
            }
            {
                unsigned int room = (unsigned int)(out_size - 1 - total);
                unsigned int chunk = room > CHROME_RECV_CHUNK_MAX ? CHROME_RECV_CHUNK_MAX : room;
                int n = openos_recv(fd, out + total, chunk, 0);
                if (n < 0) {
                    snprintf(err, err_size, "recv() failed from %s", url->host);
                    openos_close(fd);
                    return -1;
                }
                if (n == 0) break;
                total += n;
                out[total] = 0;
            }
        }
    }

    openos_close(fd);
    out[total] = 0;
    if (total <= 0) {
        snprintf(err, err_size, "HTTP response timeout from %s", url->host);
        return -1;
    }
    return total;
}

static void chrome_render_page(int win, chrome_page_t *page)
{
    char line[180];

    openos_gui_fill_rect(win, 0, 0, CHROME_W, CHROME_H, 0xfff1f3f4U);
    openos_gui_fill_rect(win, 0, 0, CHROME_W, 46, 0xffdfe3eaU);
    openos_gui_fill_rect(win, 118, 10, 390, 25, 0xffffffffU);
    openos_gui_draw_text(win, 128, 18, page->display_url, 0xff202124U);
    openos_gui_draw_text(win, 12, 18, "<", 0xff5f6368U);
    openos_gui_draw_text(win, 42, 18, ">", 0xff5f6368U);
    openos_gui_draw_text(win, 72, 18, "R", 0xff1a73e8U);
    openos_gui_draw_text(win, 522, 18, "Go", 0xff1a73e8U);
    openos_gui_draw_text(win, 562, 18, "Save", 0xff1a73e8U);

    openos_gui_fill_rect(win, 0, 46, CHROME_W, 22, 0xffffffffU);
    openos_gui_draw_text(win, 12, 52, page->status, page->loaded ? 0xff188038U : 0xffd93025U);

    openos_gui_fill_rect(win, 18, 82, CHROME_W - 36, CHROME_H - 106, 0xffffffffU);
    if (page->loaded) {
        openos_gui_draw_text(win, 32, 98, "OpenOS Chromium", 0xff1a73e8U);
        openos_gui_draw_text(win, 32, 122, page->body[0] ? page->body : "Empty response", 0xff202124U);
    } else {
        snprintf(line, sizeof(line), "Error loading %s", page->display_url);
        openos_gui_draw_text(win, 32, 98, line, 0xffd93025U);
        openos_gui_draw_text(win, 32, 126, page->body, 0xff202124U);
    }
    openos_gui_present(win);
}

static void chrome_load(chrome_page_t *page, const char *url_text)
{
    char err[256];
    int rc;
    int code;

    memset(page, 0, sizeof(*page));
    chrome_parse_url(url_text, &page->url);
    chrome_format_url(&page->url, page->display_url, sizeof(page->display_url));
    snprintf(page->status, sizeof(page->status), "Loading %s ...", page->display_url);

    rc = chrome_fetch_http(&page->url, page->raw, sizeof(page->raw), err, sizeof(err));
    if (rc < 0) {
        page->loaded = 0;
        snprintf(page->status, sizeof(page->status), "Failed");
        chrome_copy_text(page->body, sizeof(page->body), err);
        return;
    }

    code = chrome_status_code(page->raw);
    if (code >= 400) {
        page->loaded = 0;
        snprintf(page->status, sizeof(page->status), "HTTP error %d", code);
        chrome_collapse_html(page->body, sizeof(page->body), chrome_find_body(page->raw));
        if (!page->body[0]) snprintf(page->body, sizeof(page->body), "server returned HTTP %d", code);
        return;
    }

    page->loaded = 1;
    snprintf(page->status, sizeof(page->status), "Done: HTTP %d, %d bytes", code ? code : 200, rc);
    chrome_collapse_html(page->body, sizeof(page->body), chrome_find_body(page->raw));
}

static int chrome_write_file(const char *path, const char *data, int len)
{
    int fd = openos_open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    int written = 0;
    if (fd < 0) return -1;
    while (written < len) {
        int n = openos_write_fd(fd, data + written, len - written);
        if (n <= 0) {
            openos_close(fd);
            return -1;
        }
        written += n;
    }
    openos_fsync(fd);
    openos_close(fd);
    return 0;
}

static void chrome_download(chrome_page_t *page)
{
    char path[160];
    const char *name;
    int raw_len;

    openos_mkdir("/downloads", 0755);
    name = strrchr(page->url.path, '/');
    if (!name || !name[1]) name = "index.html";
    else name++;
    snprintf(path, sizeof(path), "/downloads/%s", name);
    raw_len = strlen(page->raw);
    if (page->loaded && raw_len > 0 && chrome_write_file(path, page->raw, raw_len) == 0) {
        snprintf(page->status, sizeof(page->status), "Downloaded to %s", path);
    } else {
        snprintf(page->status, sizeof(page->status), "Download failed: no loaded response");
    }
}

static const char *chrome_next_demo_url(const char *current)
{
    if (strstr(current, "/download")) return "http://example.com/";
    if (strstr(current, "/missing")) return "http://example.com/download.html";
    return "http://example.com/missing.html";
}

int main(int argc, char **argv)
{
    const char *initial_url = CHROME_DEFAULT_URL;
    chrome_page_t page;
    int win;
    int back_button;
    int forward_button;
    int refresh_button;
    int go_button;
    int download_button;
    int close_button;

    if (argc > 1 && argv && argv[1] && argv[1][0]) initial_url = argv[1];

    win = openos_gui_create_window("chromium", 60, 60, CHROME_W, CHROME_H);
    if (win < 0) {
        printf("chromium: failed to create GUI window\n");
        return 1;
    }

    back_button = openos_gui_add_button(win, 8, 8, 24, 26, "<");
    forward_button = openos_gui_add_button(win, 38, 8, 24, 26, ">");
    refresh_button = openos_gui_add_button(win, 68, 8, 34, 26, "R");
    openos_gui_add_label(win, 120, 15, 380, 18, initial_url);
    go_button = openos_gui_add_button(win, 516, 8, 38, 26, "Go");
    download_button = openos_gui_add_button(win, 560, 8, 54, 26, "Save");
    close_button = openos_gui_add_button(win, 558, 324, 64, 24, "Close");
    (void)back_button;
    (void)forward_button;
    (void)go_button;
    (void)download_button;
    (void)close_button;

    chrome_load(&page, initial_url);
    chrome_render_page(win, &page);
    printf("chromium: loaded %s status=%s\n", page.display_url, page.status);

    for (;;) {
        openos_gui_event_t event;
        int ev = openos_gui_poll_event(&event);
        if (ev > 0 && event.window_id == (unsigned int)win) {
            if (event.widget_id == (unsigned int)close_button) break;
            if (event.widget_id == (unsigned int)refresh_button || event.widget_id == (unsigned int)go_button) {
                char reload_url[256];
                chrome_copy_text(reload_url, sizeof(reload_url), page.display_url);
                chrome_load(&page, reload_url);
                chrome_render_page(win, &page);
            } else if (event.widget_id == (unsigned int)forward_button) {
                chrome_load(&page, chrome_next_demo_url(page.display_url));
                chrome_render_page(win, &page);
            } else if (event.widget_id == (unsigned int)back_button) {
                chrome_load(&page, CHROME_DEFAULT_URL);
                chrome_render_page(win, &page);
            } else if (event.widget_id == (unsigned int)download_button) {
                chrome_download(&page);
                chrome_render_page(win, &page);
            }
        }
        openos_sleep(10);
    }

    openos_gui_destroy_window(win);
    return page.loaded ? 0 : 1;
}
