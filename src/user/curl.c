#include "openos.h"

/*
 * curl - 简易 HTTP/1.0 GET 客户端
 * 用法：
 *   curl <url>              输出响应体到 stdout
 *   curl -i <url>           额外输出响应头
 *   curl -o <file> <url>    将响应体写入指定文件
 *
 * 仅支持 http:// 协议，url 格式：http://host[:port][/path]
 */

#define CURL_BUF_SIZE   4096
#define CURL_HDR_MAX    1024
#define CURL_HOST_MAX   256
#define CURL_PATH_MAX   1024

/* 大缓冲区放到 BSS，避免触发用户态 8KB 栈缺页 */
static char g_host[CURL_HOST_MAX];
static char g_path[CURL_PATH_MAX];
static char g_req[CURL_HDR_MAX];
static char g_hdrbuf[CURL_BUF_SIZE];
static char g_buf[CURL_BUF_SIZE];

static int str_starts_with(const char *s, const char *pre)
{
    while (*pre) {
        if (*s != *pre) return 0;
        s++;
        pre++;
    }
    return 1;
}

/* 解析 URL，输出 host/port/path */
static int parse_url(const char *url, char *host, int host_size,
                     int *port, char *path, int path_size)
{
    const char *p = url;
    int i;

    if (str_starts_with(p, "http://")) {
        p += 7;
    } else if (str_starts_with(p, "HTTP://")) {
        p += 7;
    }

    /* host[:port] */
    i = 0;
    while (*p && *p != '/' && *p != ':') {
        if (i >= host_size - 1) return -1;
        host[i++] = *p++;
    }
    host[i] = '\0';
    if (i == 0) return -1;

    *port = 80;
    if (*p == ':') {
        p++;
        int v = 0;
        while (*p >= '0' && *p <= '9') {
            v = v * 10 + (*p - '0');
            p++;
        }
        if (v <= 0 || v > 65535) return -1;
        *port = v;
    }

    /* path */
    if (*p == '\0') {
        if (path_size < 2) return -1;
        path[0] = '/';
        path[1] = '\0';
    } else {
        i = 0;
        while (*p) {
            if (i >= path_size - 1) return -1;
            path[i++] = *p++;
        }
        path[i] = '\0';
    }

    return 0;
}

/* 把 dnslookup 返回的 ip（网络字节序的 32 位整数）转换为点分十进制
 * 与 ping.c 中 parse_ip 的存储方式一致：低字节为 a，依次到高字节为 d */
static void ip_to_str(unsigned int ip, char *buf, int size)
{
    unsigned int a = ip & 0xff;
    unsigned int b = (ip >> 8) & 0xff;
    unsigned int c = (ip >> 16) & 0xff;
    unsigned int d = (ip >> 24) & 0xff;
    openos_snprintf(buf, size, "%u.%u.%u.%u", a, b, c, d);
}

/* 给 stderr 输出错误 */
static void curl_err(const char *msg)
{
    openos_write_fd(STDERR_FILENO, "curl: ", 6);
    openos_write_fd(STDERR_FILENO, msg, openos_strlen(msg));
    openos_write_fd(STDERR_FILENO, "\n", 1);
}

/* 寻找 \r\n\r\n */
static int find_header_end(const char *buf, int len)
{
    for (int i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' &&
            buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            return i + 4;
        }
    }
    /* 兼容 LF-only */
    for (int i = 0; i + 1 < len; i++) {
        if (buf[i] == '\n' && buf[i + 1] == '\n') {
            return i + 2;
        }
    }
    return -1;
}

int main(int argc, char **argv)
{
    int show_headers = 0;
    const char *out_file = 0;
    const char *url = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] == '-' && a[1] == 'i' && a[2] == '\0') {
            show_headers = 1;
        } else if (a[0] == '-' && a[1] == 'o' && a[2] == '\0') {
            if (i + 1 >= argc) {
                curl_err("option -o requires a file");
                return 1;
            }
            out_file = argv[++i];
        } else if (a[0] == '-' && a[1] == 'h' && a[2] == '\0') {
            openos_puts("Usage: curl [-i] [-o file] <url>");
            return 0;
        } else if (a[0] == '-') {
            curl_err("unknown option");
            return 1;
        } else if (!url) {
            url = a;
        } else {
            curl_err("too many arguments");
            return 1;
        }
    }

    if (!url) {
        openos_puts("Usage: curl [-i] [-o file] <url>");
        return 1;
    }

    int port = 80;
    if (parse_url(url, g_host, CURL_HOST_MAX, &port, g_path, CURL_PATH_MAX) != 0) {
        curl_err("invalid url");
        return 1;
    }

    /* DNS */
    unsigned int ip = 0;
    if (openos_dnslookup(g_host, &ip) != 0 || ip == 0) {
        curl_err("dns lookup failed");
        return 1;
    }

    char ipstr[32];
    ip_to_str(ip, ipstr, sizeof(ipstr));

    /* 输出进度信息到 stderr */
    {
        char info[160];
        openos_snprintf(info, sizeof(info), "Connecting to %s (%s:%d)...\n", g_host, ipstr, port);
        openos_write_fd(STDERR_FILENO, info, openos_strlen(info));
    }

    /* 建 socket */
    int fd = openos_socket(OPENOS_AF_INET, OPENOS_SOCK_STREAM, 0);
    if (fd < 0) {
        curl_err("socket failed");
        return 1;
    }

    openos_sockaddr_in_t addr;
    openos_memset(&addr, 0, sizeof(addr));
    addr.sin_family = OPENOS_AF_INET;
    addr.sin_port = openos_htons((unsigned short)port);
    addr.sin_addr = ip;

    if (openos_connect(fd, (openos_sockaddr_t *)&addr, sizeof(addr)) != 0) {
        curl_err("connect failed");
        openos_close(fd);
        return 1;
    }

    /* 拼接 HTTP 请求 */
    openos_snprintf(g_req, sizeof(g_req),
        "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: openos-curl/1.0\r\n"
        "Accept: */*\r\nConnection: close\r\n\r\n",
        g_path, g_host);

    int req_len = openos_strlen(g_req);
    int sent = 0;
    while (sent < req_len) {
        int n = openos_send(fd, g_req + sent, req_len - sent, 0);
        if (n <= 0) {
            curl_err("send failed");
            openos_close(fd);
            return 1;
        }
        sent += n;
    }

    /* 接收响应：先缓冲 header 部分 */
    int hdr_used = 0;
    int header_end = -1;

    while (hdr_used < CURL_BUF_SIZE) {
        int n = openos_recv(fd, g_hdrbuf + hdr_used, CURL_BUF_SIZE - hdr_used, 0);
        if (n <= 0) break;
        hdr_used += n;
        header_end = find_header_end(g_hdrbuf, hdr_used);
        if (header_end >= 0) break;
    }

    int out_fd = STDOUT_FILENO;
    int opened = 0;
    if (out_file) {
        out_fd = openos_open(out_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out_fd < 0) {
            curl_err("cannot open output file");
            openos_close(fd);
            return 1;
        }
        opened = 1;
    }

    /* 头部输出（如果需要）到 stdout */
    if (show_headers && header_end > 0) {
        openos_write_fd(STDOUT_FILENO, g_hdrbuf, header_end);
    }

    /* body 部分：先把缓冲里的写出去 */
    int body_start = header_end > 0 ? header_end : 0;
    if (header_end < 0) {
        /* 没找到 header end，把整体作为 body 处理（容错） */
        body_start = 0;
    }
    if (hdr_used > body_start) {
        openos_write_fd(out_fd, g_hdrbuf + body_start, hdr_used - body_start);
    }

    /* 继续接收剩余数据 */
    for (;;) {
        int n = openos_recv(fd, g_buf, CURL_BUF_SIZE, 0);
        if (n <= 0) break;
        openos_write_fd(out_fd, g_buf, n);
    }

    openos_close(fd);
    if (opened) openos_close(out_fd);
    return 0;
}
