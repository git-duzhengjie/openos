#include "openos.h"

#define BROWSER_DEFAULT_HOST "example.com"
#define BROWSER_DEFAULT_PATH "/"
#define BROWSER_HTTP_PORT "80"
#define BROWSER_RECV_MAX 1536
#define BROWSER_NET_WAIT_TRIES 80
#define BROWSER_DNS_RETRIES 8
#define BROWSER_CONNECT_RETRIES 12

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
        int connected = 0;
        for (int attempt = 0; attempt < BROWSER_CONNECT_RETRIES; ++attempt) {
            openos_sockaddr_in_t connect_addr;
            memset(&connect_addr, 0, sizeof(connect_addr));
            connect_addr.sin_family = OPENOS_AF_INET;
            connect_addr.sin_port = openos_htons(dst_port);
            connect_addr.sin_addr = dst_ip;
            if (openos_connect(fd, (openos_sockaddr_t *)&connect_addr, sizeof(connect_addr)) == 0) {
                connected = 1;
                break;
            }
            openos_sleep(10);
        }
        if (!connected) {
            browser_format_connect_error(out, out_size, host, dst_ip, dst_port);
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

    while (total < out_size - 1) {
        int n = openos_recv(fd, out + total, (unsigned int)(out_size - 1 - total), 0);
        if (n <= 0) break;
        total += n;
        if (total >= BROWSER_RECV_MAX) break;
    }
    out[total] = 0;

    openos_close(fd);
    return total > 0 ? 0 : -1;
}

int main(int argc, char **argv)
{
    const char *host = BROWSER_DEFAULT_HOST;
    const char *path = BROWSER_DEFAULT_PATH;
    char response[BROWSER_RECV_MAX + 1];
    char summary[512];
    int win;
    int status_label;
    int body_label;
    int close_button;
    int rc;

    if (argc > 1 && argv[1] && argv[1][0]) host = argv[1];
    if (argc > 2 && argv[2] && argv[2][0]) path = argv[2];

    win = openos_gui_create_window("用户态浏览器", 80, 80, 560, 260);
    if (win < 0) {
        printf("browser: failed to create GUI window\n");
        return 1;
    }

    status_label = openos_gui_add_label(win, 16, 24, 500, 20, "Loading http://example.com/ ...");
    body_label = openos_gui_add_label(win, 16, 56, 520, 150, "Waiting for HTTP response...");
    close_button = openos_gui_add_button(win, 16, 216, 80, 24, "Close");

    snprintf(summary, sizeof(summary), "Loading http://%s%s ...", host, path);
    openos_gui_set_text(win, status_label, summary);
    rc = browser_fetch_http(host, path, response, sizeof(response));
    if (rc < 0) {
        openos_gui_set_text(win, status_label, "Failed");
        openos_gui_set_text(win, body_label, response);
        printf("browser: %s\n", response);
    } else {
        collapse_html_text(summary, sizeof(summary), find_body(response));
        openos_gui_set_text(win, status_label, "Done");
        openos_gui_set_text(win, body_label, summary[0] ? summary : "Empty response");
        printf("browser: loaded http://%s%s\n", host, path);
        printf("%s\n", summary);
    }

    for (;;) {
        openos_gui_event_t event;
        int ev = openos_gui_poll_event(&event);
        if (ev > 0 && event.window_id == (unsigned int)win && event.widget_id == (unsigned int)close_button) {
            break;
        }
        openos_sleep(10);
    }

    openos_gui_destroy_window(win);
    return rc < 0 ? 1 : 0;
}
