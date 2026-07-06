#include "openos64.h"

/*
 * wget — ring3 用户态真·联网工具（M1.7）
 *
 * 通过 SYS_DNSLOOKUP + SYS_TCP_CONNECT/SEND/RECV/CLOSE 直通内核真 netstack，
 * 完成一次真实的 HTTP/1.1 GET 请求并把响应打印到标准输出。
 *
 * 用法:
 *   wget <host> [path]        默认端口 80，默认 path "/"
 *   例: wget example.com /
 */

static void put_str(const char *s) {
    openos64_write(1, s, openos64_strlen(s));
}

static void app_u32(char *buf, int *pos, uint32_t v) {
    char tmp[12];
    int n = 0;
    if (v == 0) { buf[(*pos)++] = '0'; return; }
    while (v > 0 && n < 11) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n > 0) buf[(*pos)++] = tmp[--n];
}

static void app_ip(char *buf, int *pos, uint32_t ip_h) {
    for (int i = 3; i >= 0; --i) {
        app_u32(buf, pos, (uint8_t)((ip_h >> (i * 8)) & 0xff));
        if (i != 0) buf[(*pos)++] = '.';
    }
}

/* 拼接字符串到 buf，返回新位置 */
static int app_str(char *buf, int pos, const char *s) {
    while (*s) buf[pos++] = *s++;
    return pos;
}

int openos64_main(int argc, char **argv, char **envp) {
    (void)envp;

    if (argc < 2) {
        put_str("用法: wget [-1] <域名> [路径]\n");
        put_str("  -1  使用单次 SYS_HTTP_GET 系统调用(内核内完成 DNS+连接+GET+收)，响应写回缓冲\n");
        put_str("例: wget example.com /   或   wget -1 example.com /\n");
        return 1;
    }

    /* -1 / --http: one-shot 模式，走 M1.9 的 SYS_HTTP_GET，响应正文写回用户缓冲 */
    int oneshot = 0;
    int ai = 1;
    if (argv[1][0] == '-' && (argv[1][1] == '1' || argv[1][1] == 'h')) {
        oneshot = 1;
        ai = 2;
    }
    if (ai >= argc) {
        put_str("wget: 缺少域名参数\n");
        return 1;
    }

    const char *host = argv[ai];
    const char *path = (argc >= ai + 2) ? argv[ai + 1] : "/";

    if (oneshot) {
        static char page[8192];
        put_str("[one-shot] 调用 SYS_HTTP_GET: ");
        put_str(host); put_str(path); put_str("\n---- 响应开始 ----\n");
        int n = openos64_http_get(host, path, page, sizeof(page));
        if (n < 0) {
            put_str("\nwget: SYS_HTTP_GET 失败\n");
            return 1;
        }
        openos64_write(1, page, (openos64_size_t)n);
        put_str("\n---- 响应结束 ----\n");
        {
            char line[64];
            int p = 0;
            p = app_str(line, p, "共接收 ");
            app_u32(line, &p, (uint32_t)n);
            p = app_str(line, p, " 字节 (写回缓冲)\n");
            openos64_write(1, line, (openos64_size_t)p);
        }
        return 0;
    }

    /* 1) DNS 解析 */
    uint32_t ip_h = 0;
    if (openos64_dnslookup(host, &ip_h) != 0) {
        put_str("wget: DNS 解析失败: ");
        put_str(host);
        put_str("\n");
        return 1;
    }

    {
        char line[128];
        int p = 0;
        p = app_str(line, p, "解析 ");
        p = app_str(line, p, host);
        p = app_str(line, p, " -> ");
        app_ip(line, &p, ip_h);
        line[p++] = '\n';
        openos64_write(1, line, (openos64_size_t)p);
    }

    /* 2) TCP 连接 (端口 80) */
    put_str("正在连接 (TCP 80) ...\n");
    int conn = openos64_tcp_connect(ip_h, 80);
    if (conn < 0) {
        put_str("wget: TCP 连接失败\n");
        return 1;
    }
    put_str("已连接，发送 HTTP GET 请求 ...\n");

    /* 3) 组装并发送 HTTP GET 请求 */
    char req[512];
    int rp = 0;
    rp = app_str(req, rp, "GET ");
    rp = app_str(req, rp, path);
    rp = app_str(req, rp, " HTTP/1.1\r\nHost: ");
    rp = app_str(req, rp, host);
    rp = app_str(req, rp, "\r\nConnection: close\r\nUser-Agent: openos-wget/1.0\r\n\r\n");

    int sent = openos64_tcp_send(conn, req, (uint32_t)rp);
    if (sent < 0) {
        put_str("wget: 发送请求失败\n");
        openos64_tcp_close(conn);
        return 1;
    }

    /* 4) 循环接收响应并打印 */
    put_str("---- 响应开始 ----\n");
    char buf[2048];
    int total = 0;
    int empty_rounds = 0;
    for (int i = 0; i < 40; i++) {
        int got = openos64_tcp_recv(conn, buf, sizeof(buf), 120);
        if (got > 0) {
            openos64_write(1, buf, (openos64_size_t)got);
            total += got;
            empty_rounds = 0;
        } else {
            /* got==0: 暂无数据或对端关闭；连续多轮空则退出 */
            empty_rounds++;
            if (empty_rounds >= 3) break;
        }
    }
    put_str("\n---- 响应结束 ----\n");

    {
        char line[64];
        int p = 0;
        p = app_str(line, p, "共接收 ");
        app_u32(line, &p, (uint32_t)total);
        p = app_str(line, p, " 字节\n");
        openos64_write(1, line, (openos64_size_t)p);
    }

    /* 5) 关闭连接 */
    openos64_tcp_close(conn);
    return 0;
}
