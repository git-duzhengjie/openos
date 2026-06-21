#include "openos.h"

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
#define BROWSER_BODY_MAX 512

typedef struct browser_load_context {
    volatile int active;
    volatile int done;
    volatile int result;
    int window_id;
    int status_label_id;
    int body_label_id;
    char host[BROWSER_HOST_MAX];
    char path[BROWSER_PATH_MAX];
    char response[BROWSER_RECV_MAX + 1];
    char body[BROWSER_BODY_MAX];
    char status[64];
} browser_load_context_t;

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

static void collapse_html_text(char *dst, int dst_size, const char *src)
{
    int out = 0;
    int in_tag = 0;
    int pending_space = 0;

    if (!dst || dst_size <= 0) return;
    dst[0] = 0;
    if (!src) return;

    for (const char *p = src; *p && out < dst_size - 1; ++p) {
        char c = *p;
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
            if (!strncmp(p, "&amp;", 5)) { c = '&'; p += 4; }
            else if (!strncmp(p, "&lt;", 4)) { c = '<'; p += 3; }
            else if (!strncmp(p, "&gt;", 4)) { c = '>'; p += 3; }
            else if (!strncmp(p, "&quot;", 6)) { c = '"'; p += 5; }
            else if (!strncmp(p, "&nbsp;", 6)) { c = ' '; p += 5; }
        }
        if (c == '\r' || c == '\n' || c == '\t' || c == ' ') {
            pending_space = 1;
            continue;
        }
        if (pending_space && out > 0 && dst[out - 1] != '\n') {
            dst[out++] = ' ';
            if (out >= dst_size - 1) break;
        }
        pending_space = 0;
        dst[out++] = c;
    }
    dst[out] = 0;
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

static void browser_load_worker(void *arg)
{
    browser_load_context_t *ctx = (browser_load_context_t *)arg;
    int rc;

    if (!ctx)
        return;

    rc = browser_fetch_http(ctx->host, ctx->path, ctx->response, sizeof(ctx->response));
    ctx->result = rc;
    if (rc < 0) {
        snprintf(ctx->status, sizeof(ctx->status), "Failed");
        snprintf(ctx->body, sizeof(ctx->body), "%s", ctx->response);
        printf("browser: %s\n", ctx->response);
    } else {
        collapse_html_text(ctx->body, sizeof(ctx->body), find_body(ctx->response));
        if (!ctx->body[0])
            snprintf(ctx->body, sizeof(ctx->body), "Empty response");
        snprintf(ctx->status, sizeof(ctx->status), "Done");
        printf("browser: loaded http://%s%s\n", ctx->host, ctx->path);
        printf("%s\n", ctx->body);
    }

    if (ctx->window_id >= 0 && ctx->status_label_id >= 0 && ctx->body_label_id >= 0) {
        openos_gui_set_text(ctx->window_id, ctx->status_label_id,
                            ctx->status[0] ? ctx->status : (rc < 0 ? "Failed" : "Done"));
        openos_gui_set_text(ctx->window_id, ctx->body_label_id,
                            ctx->body[0] ? ctx->body : (rc < 0 ? "Load failed" : "Empty response"));
    }
    ctx->done = 1;
}

int main(int argc, char **argv)
{
    const char *host = BROWSER_DEFAULT_HOST;
    const char *path = BROWSER_DEFAULT_PATH;
    char summary[128];
    int win;
    int status_label;
    int body_label;
    int load_button;
    int close_button;
    int rc = 0;
    browser_load_context_t load;

    memset(&load, 0, sizeof(load));

    if (argc > 1 && argv && argv[1] && argv[1][0]) host = argv[1];
    if (argc > 2 && argv && argv[2] && argv[2][0]) path = argv[2];
    if (!host || !host[0]) host = BROWSER_DEFAULT_HOST;
    if (!path || !path[0]) path = BROWSER_DEFAULT_PATH;

    win = openos_gui_create_window("用户态浏览器", 80, 80, 560, 260);
    if (win < 0) {
        printf("browser: failed to create GUI window\n");
        return 1;
    }

    status_label = openos_gui_add_label(win, 16, 24, 500, 20, "Ready");
    snprintf(summary, sizeof(summary), "Ready: http://%s%s", host, path);
    body_label = openos_gui_add_label(win, 16, 56, 520, 150, summary);
    load_button = openos_gui_add_button(win, 16, 216, 80, 24, "Load");
    close_button = openos_gui_add_button(win, 112, 216, 80, 24, "Close");
    printf("browser: ready http://%s%s\n", host, path);

    for (;;) {
        openos_gui_event_t event;
        int ev = openos_gui_poll_event(&event);
        if (ev == 0 && event.type != OPENOS_GUI_EVENT_NONE && event.window_id == (unsigned int)win) {
            if (event.widget_id == (unsigned int)close_button)
                break;
            if (event.widget_id == (unsigned int)load_button) {
                if (load.active && !load.done) {
                    openos_gui_set_text(win, status_label, "Loading");
                    openos_gui_set_text(win, body_label, "Load already in progress...");
                } else {
                    openos_thread_t tid;
                    char loading[128];

                    memset(&load, 0, sizeof(load));
                    snprintf(load.host, sizeof(load.host), "%s", host);
                    snprintf(load.path, sizeof(load.path), "%s", path);
                    load.window_id = win;
                    load.status_label_id = status_label;
                    load.body_label_id = body_label;
                    snprintf(loading, sizeof(loading), "Loading http://%s%s ...", load.host, load.path);
                    load.active = 1;
                    load.done = 0;
                    load.result = -1;
                    openos_gui_set_text(win, status_label, loading);
                    openos_gui_set_text(win, body_label, "Waiting for HTTP response...");
                    if (openos_thread_create(&tid, browser_load_worker, &load) != 0) {
                        load.active = 0;
                        load.done = 1;
                        load.result = -1;
                        rc = -1;
                        openos_gui_set_text(win, status_label, "Failed");
                        openos_gui_set_text(win, body_label, "failed to create browser loader thread");
                    }
                }
            }
        }
        if (load.active && load.done) {
            rc = load.result;
            openos_gui_set_text(win, status_label, load.status[0] ? load.status : (rc < 0 ? "Failed" : "Done"));
            openos_gui_set_text(win, body_label, load.body[0] ? load.body : (rc < 0 ? "Load failed" : "Empty response"));
            load.active = 0;
        }
        openos_sleep(10);
    }

    openos_gui_destroy_window(win);
    return rc < 0 ? 1 : 0;
}
