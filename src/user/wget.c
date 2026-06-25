#include "openos.h"

/*
 * wget - 简易 HTTP/1.0 文件下载器
 * 用法：
 *   wget <url>             根据 url 自动决定文件名
 *   wget -O <file> <url>   将响应体写入指定文件
 *   wget -q <url>          安静模式（不输出进度）
 *
 * 仅支持 http:// 协议。
 */

#define WGET_BUF_SIZE   4096
#define WGET_HDR_MAX    1024
#define WGET_HOST_MAX   256
#define WGET_PATH_MAX   1024
#define WGET_NAME_MAX   256

/* 大缓冲区放到 BSS，避免用户态 8KB 栈爆 */
static char g_host[WGET_HOST_MAX];
static char g_path[WGET_PATH_MAX];
static char g_fname[WGET_NAME_MAX];
static char g_req[WGET_HDR_MAX];
static char g_hdrbuf[WGET_BUF_SIZE];
static char g_buf[WGET_BUF_SIZE];

static int str_starts_with(const char *s, const char *pre)
{
    while (*pre) {
        if (*s != *pre) return 0;
        s++;
        pre++;
    }
    return 1;
}

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

/* 从 path 中提取文件名；如果 path 是 "/" 或以 "/" 结尾，返回 "index.html" */
static void derive_filename(const char *path, char *name, int size)
{
    const char *last = path;
    const char *p = path;
    while (*p) {
        if (*p == '/') last = p + 1;
        p++;
    }
    /* 去掉 query 部分 */
    int n = 0;
    while (last[n] && last[n] != '?' && last[n] != '#' && n < size - 1) {
        name[n] = last[n];
        n++;
    }
    name[n] = '\0';
    if (n == 0) {
        /* 默认 */
        const char *def = "index.html";
        int i = 0;
        while (def[i] && i < size - 1) {
            name[i] = def[i];
            i++;
        }
        name[i] = '\0';
    }
}

static void ip_to_str(unsigned int ip, char *buf, int size)
{
    unsigned int a = ip & 0xff;
    unsigned int b = (ip >> 8) & 0xff;
    unsigned int c = (ip >> 16) & 0xff;
    unsigned int d = (ip >> 24) & 0xff;
    openos_snprintf(buf, size, "%u.%u.%u.%u", a, b, c, d);
}

static void wget_err(const char *msg)
{
    openos_write_fd(STDERR_FILENO, "wget: ", 6);
    openos_write_fd(STDERR_FILENO, msg, openos_strlen(msg));
    openos_write_fd(STDERR_FILENO, "\n", 1);
}

static void wget_log(int quiet, const char *msg)
{
    if (quiet) return;
    openos_write_fd(STDERR_FILENO, msg, openos_strlen(msg));
}

static int find_header_end(const char *buf, int len)
{
    for (int i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' &&
            buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            return i + 4;
        }
    }
    for (int i = 0; i + 1 < len; i++) {
        if (buf[i] == '\n' && buf[i + 1] == '\n') {
            return i + 2;
        }
    }
    return -1;
}

/* 检查 status line 是否为 2xx */
static int is_success_status(const char *hdr, int len)
{
    /* HTTP/1.x SSS */
    int i = 0;
    /* 跳过到第一个空格 */
    while (i < len && hdr[i] != ' ' && hdr[i] != '\r' && hdr[i] != '\n') i++;
    while (i < len && hdr[i] == ' ') i++;
    if (i + 2 >= len) return 0;
    if (hdr[i] == '2') return 1;
    return 0;
}

int main(int argc, char **argv)
{
    int quiet = 0;
    const char *out_file = 0;
    const char *url = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] == '-' && a[1] == 'O' && a[2] == '\0') {
            if (i + 1 >= argc) {
                wget_err("option -O requires a file");
                return 1;
            }
            out_file = argv[++i];
        } else if (a[0] == '-' && a[1] == 'q' && a[2] == '\0') {
            quiet = 1;
        } else if (a[0] == '-' && a[1] == 'h' && a[2] == '\0') {
            openos_puts("Usage: wget [-q] [-O file] <url>");
            return 0;
        } else if (a[0] == '-') {
            wget_err("unknown option");
            return 1;
        } else if (!url) {
            url = a;
        } else {
            wget_err("too many arguments");
            return 1;
        }
    }

    if (!url) {
        openos_puts("Usage: wget [-q] [-O file] <url>");
        return 1;
    }

    int port = 80;
    if (parse_url(url, g_host, WGET_HOST_MAX, &port, g_path, WGET_PATH_MAX) != 0) {
        wget_err("invalid url");
        return 1;
    }

    const char *fname = out_file;
    if (!fname) {
        derive_filename(g_path, g_fname, sizeof(g_fname));
        fname = g_fname;
    }

    /* DNS */
    unsigned int ip = 0;
    if (openos_dnslookup(g_host, &ip) != 0 || ip == 0) {
        wget_err("dns lookup failed");
        return 1;
    }

    char ipstr[32];
    ip_to_str(ip, ipstr, sizeof(ipstr));

    char info[160];
    openos_snprintf(info, sizeof(info), "Resolving %s -> %s\n", g_host, ipstr);
    wget_log(quiet, info);
    openos_snprintf(info, sizeof(info), "Connecting to %s:%d ...\n", ipstr, port);
    wget_log(quiet, info);

    int fd = openos_socket(OPENOS_AF_INET, OPENOS_SOCK_STREAM, 0);
    if (fd < 0) {
        wget_err("socket failed");
        return 1;
    }

    openos_sockaddr_in_t addr;
    openos_memset(&addr, 0, sizeof(addr));
    addr.sin_family = OPENOS_AF_INET;
    addr.sin_port = openos_htons((unsigned short)port);
    addr.sin_addr = ip;

    if (openos_connect(fd, (openos_sockaddr_t *)&addr, sizeof(addr)) != 0) {
        wget_err("connect failed");
        openos_close(fd);
        return 1;
    }

    openos_snprintf(g_req, sizeof(g_req),
        "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: openos-wget/1.0\r\n"
        "Accept: */*\r\nConnection: close\r\n\r\n",
        g_path, g_host);

    int req_len = openos_strlen(g_req);
    int sent = 0;
    while (sent < req_len) {
        int n = openos_send(fd, g_req + sent, req_len - sent, 0);
        if (n <= 0) {
            wget_err("send failed");
            openos_close(fd);
            return 1;
        }
        sent += n;
    }

    /* 接收 header */
    int hdr_used = 0;
    int header_end = -1;

    while (hdr_used < WGET_BUF_SIZE) {
        int n = openos_recv(fd, g_hdrbuf + hdr_used, WGET_BUF_SIZE - hdr_used, 0);
        if (n <= 0) break;
        hdr_used += n;
        header_end = find_header_end(g_hdrbuf, hdr_used);
        if (header_end >= 0) break;
    }

    if (header_end <= 0) {
        wget_err("no http response");
        openos_close(fd);
        return 1;
    }

    if (!is_success_status(g_hdrbuf, header_end)) {
        /* 把 status line 输出到 stderr */
        int eol = 0;
        while (eol < header_end && g_hdrbuf[eol] != '\r' && g_hdrbuf[eol] != '\n') eol++;
        openos_write_fd(STDERR_FILENO, "wget: server returned: ", 23);
        openos_write_fd(STDERR_FILENO, g_hdrbuf, eol);
        openos_write_fd(STDERR_FILENO, "\n", 1);
        openos_close(fd);
        return 1;
    }

    int out_fd = openos_open(fname, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd < 0) {
        wget_err("cannot open output file");
        openos_close(fd);
        return 1;
    }

    openos_snprintf(info, sizeof(info), "Saving to: '%s'\n", fname);
    wget_log(quiet, info);

    long total = 0;

    /* 先写 buffered 的 body */
    if (hdr_used > header_end) {
        int blen = hdr_used - header_end;
        openos_write_fd(out_fd, g_hdrbuf + header_end, blen);
        total += blen;
    }

    for (;;) {
        int n = openos_recv(fd, g_buf, WGET_BUF_SIZE, 0);
        if (n <= 0) break;
        openos_write_fd(out_fd, g_buf, n);
        total += n;
    }

    openos_close(fd);
    openos_close(out_fd);

    openos_snprintf(info, sizeof(info), "'%s' saved [%ld bytes]\n", fname, total);
    wget_log(quiet, info);
    return 0;
}
