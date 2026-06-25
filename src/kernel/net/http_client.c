#include "http_client.h"
#include "tls.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define DEFAULT_USER_AGENT "OpenOS/1.0"
#define DEFAULT_TIMEOUT 30000
#define DEFAULT_MAX_REDIRECTS 5
#define RECV_BUF_SIZE 4096

static const char* http_method_str(http_method_t method) {
    switch (method) {
        case HTTP_GET: return "GET";
        case HTTP_POST: return "POST";
        case HTTP_HEAD: return "HEAD";
        case HTTP_PUT: return "PUT";
        case HTTP_DELETE: return "DELETE";
        default: return "GET";
    }
}

int http_client_init(http_client_t* client, const http_config_t* config) {
    if (!client) return -1;
    
    memset(client, 0, sizeof(http_client_t));
    
    if (config) {
        client->config = *config;
    } else {
        client->config.timeout_ms = DEFAULT_TIMEOUT;
        client->config.max_redirects = DEFAULT_MAX_REDIRECTS;
        client->config.keep_alive = 1;
        client->config.user_agent = DEFAULT_USER_AGENT;
    }
    
    return 0;
}

int http_client_connect(http_client_t* client, const char* host, int port, int use_https) {
    if (!client || !host) return -1;
    
    strncpy(client->host, host, sizeof(client->host) - 1);
    client->port = port;
    client->use_https = use_https;
    
    if (socket_init(&client->sock) != 0) {
        return -1;
    }
    
    if (socket_connect(&client->sock, host, port, client->config.timeout_ms) != 0) {
        socket_close(&client->sock);
        return -1;
    }
    
    if (use_https) {
        // TODO: TLS 握手
        // int ret = tls_connect(&client->tls, &client->sock, host);
        // if (ret != 0) {
        //     socket_close(&client->sock);
        //     return -1;
        // }
    }
    
    client->connected = 1;
    return 0;
}

static int http_send_line(http_client_t* client, const char* line) {
    size_t len = strlen(line);
    if (client->use_https) {
        // TODO: TLS 发送
        return -1;
    } else {
        return socket_send(&client->sock, (const uint8_t*)line, len);
    }
}

static int http_recv_line(http_client_t* client, char* buf, size_t max_len) {
    size_t pos = 0;
    char c;
    int ret;
    
    while (pos < max_len - 1) {
        if (client->use_https) {
            // TODO: TLS 接收
            return -1;
        } else {
            ret = socket_recv(&client->sock, (uint8_t*)&c, 1);
        }
        
        if (ret <= 0) break;
        
        if (c == '\n') {
            if (pos > 0 && buf[pos-1] == '\r') {
                buf[pos-1] = '\0';
            } else {
                buf[pos] = '\0';
            }
            return pos;
        }
        
        buf[pos++] = c;
    }
    
    buf[pos] = '\0';
    return pos > 0 ? (int)pos : -1;
}

static int http_parse_status_line(const char* line, http_response_t* response) {
    // 格式: HTTP/1.1 200 OK
    char version[16];
    int code;
    char text[256];
    
    if (sscanf(line, "%15s %d %255[^\r\n]", version, &code, text) < 2) {
        return -1;
    }
    
    response->http_version = strdup(version);
    response->status_code = code;
    response->status_text = strdup(text);
    
    return 0;
}

static int http_parse_header(const char* line, http_header_t** header_list) {
    char* colon = strchr(line, ':');
    if (!colon) return -1;
    
    *colon = '\0';
    const char* name = line;
    const char* value = colon + 1;
    
    // 跳过值前面的空格
    while (*value == ' ') value++;
    
    http_header_t* header = malloc(sizeof(http_header_t));
    if (!header) return -1;
    
    header->name = strdup(name);
    header->value = strdup(value);
    header->next = NULL;
    
    if (*header_list) {
        http_header_t* last = *header_list;
        while (last->next) last = last->next;
        last->next = header;
    } else {
        *header_list = header;
    }
    
    return 0;
}

static int http_parse_headers(http_client_t* client, http_response_t* response) {
    char line[1024];
    int ret;
    
    // 读取状态行
    ret = http_recv_line(client, line, sizeof(line));
    if (ret < 0) return -1;
    
    if (http_parse_status_line(line, response) != 0) {
        return -1;
    }
    
    // 读取头部
    while (1) {
        ret = http_recv_line(client, line, sizeof(line));
        if (ret < 0) return -1;
        
        // 空行表示头部结束
        if (strlen(line) == 0) break;
        
        http_parse_header(line, &response->headers);
    }
    
    // 解析特殊头部
    const char* cl = http_get_header(response, "Content-Length");
    if (cl) {
        response->content_length = atoi(cl);
    }
    
    const char* te = http_get_header(response, "Transfer-Encoding");
    if (te && strstr(te, "chunked")) {
        response->chunked = 1;
    }
    
    const char* conn = http_get_header(response, "Connection");
    if (conn && strstr(conn, "keep-alive")) {
        response->keep_alive = 1;
    }
    
    return 0;
}

static int http_read_body(http_client_t* client, http_response_t* response) {
    uint8_t buf[RECV_BUF_SIZE];
    size_t total = 0;
    int ret;
    
    if (response->chunked) {
        // TODO: 分块编码支持
        return -1;
    }
    
    size_t body_size = response->content_length;
    if (body_size == 0) {
        // 读取直到连接关闭
        response->body = NULL;
        response->body_len = 0;
        return 0;
    }
    
    response->body = malloc(body_size);
    if (!response->body) return -1;
    
    while (total < body_size) {
        size_t to_read = body_size - total;
        if (to_read > RECV_BUF_SIZE) to_read = RECV_BUF_SIZE;
        
        if (client->use_https) {
            // TODO: TLS 接收
            ret = -1;
        } else {
            ret = socket_recv(&client->sock, response->body + total, to_read);
        }
        
        if (ret <= 0) break;
        total += ret;
    }
    
    response->body_len = total;
    return 0;
}

int http_client_request(http_client_t* client,
                        http_method_t method,
                        const char* path,
                        const http_header_t* extra_headers,
                        const uint8_t* body,
                        size_t body_len,
                        http_response_t* response) {
    if (!client || !client->connected || !path || !response) {
        return -1;
    }
    
    memset(response, 0, sizeof(http_response_t));
    
    // 构建请求行
    char req_line[1024];
    snprintf(req_line, sizeof(req_line),
             "%s %s HTTP/1.1\r\n",
             http_method_str(method), path);
    http_send_line(client, req_line);
    
    // 发送标准头部
    char host_header[512];
    snprintf(host_header, sizeof(host_header),
             "Host: %s\r\n", client->host);
    http_send_line(client, host_header);
    
    char ua_header[512];
    snprintf(ua_header, sizeof(ua_header),
             "User-Agent: %s\r\n", client->config.user_agent);
    http_send_line(client, ua_header);
    
    if (client->config.keep_alive) {
        http_send_line(client, "Connection: keep-alive\r\n");
    }
    
    if (body && body_len > 0) {
        char cl_header[64];
        snprintf(cl_header, sizeof(cl_header),
                 "Content-Length: %zu\r\n", body_len);
        http_send_line(client, cl_header);
    }
    
    // 发送额外头部
    const http_header_t* h = extra_headers;
    while (h) {
        char header[1024];
        snprintf(header, sizeof(header), "%s: %s\r\n", h->name, h->value);
        http_send_line(client, header);
        h = h->next;
    }
    
    // 头部结束
    http_send_line(client, "\r\n");
    
    // 发送请求体
    if (body && body_len > 0) {
        if (client->use_https) {
            // TODO: TLS 发送
        } else {
            socket_send(&client->sock, body, body_len);
        }
    }
    
    // 解析响应
    if (http_parse_headers(client, response) != 0) {
        http_response_free(response);
        return -1;
    }
    
    // 读取响应体
    if (method != HTTP_HEAD) {
        if (http_read_body(client, response) != 0) {
            http_response_free(response);
            return -1;
        }
    }
    
    return 0;
}

void http_response_free(http_response_t* response) {
    if (!response) return;
    
    if (response->http_version) free(response->http_version);
    if (response->status_text) free(response->status_text);
    
    http_header_t* h = response->headers;
    while (h) {
        http_header_t* next = h->next;
        free(h->name);
        free(h->value);
        free(h);
        h = next;
    }
    
    if (response->body) free(response->body);
    memset(response, 0, sizeof(http_response_t));
}

void http_client_disconnect(http_client_t* client) {
    if (!client) return;
    
    if (client->use_https) {
        // TODO: TLS 关闭
    }
    
    if (client->connected) {
        socket_close(&client->sock);
    }
    
    memset(client, 0, sizeof(http_client_t));
}

const char* http_get_header(const http_response_t* response, const char* name) {
    if (!response || !name) return NULL;
    
    const http_header_t* h = response->headers;
    while (h) {
        if (strcasecmp(h->name, name) == 0) {
            return h->value;
        }
        h = h->next;
    }
    
    return NULL;
}

int http_parse_url(const char* url, char* host, size_t host_len,
                   int* port, char* path, size_t path_len, int* use_https) {
    if (!url || !host || !port || !path || !use_https) {
        return -1;
    }
    
    const char* p = url;
    *use_https = 0;
    
    // 检查协议
    if (strncmp(p, "https://", 8) == 0) {
        *use_https = 1;
        p += 8;
        *port = 443;
    } else if (strncmp(p, "http://", 7) == 0) {
        p += 7;
        *port = 80;
    } else {
        return -1;
    }
    
    // 提取主机名
    const char* host_start = p;
    while (*p && *p != ':' && *p != '/' && *p != '?') p++;
    
    size_t h_len = p - host_start;
    if (h_len >= host_len) return -1;
    memcpy(host, host_start, h_len);
    host[h_len] = '\0';
    
    // 提取端口
    if (*p == ':') {
        p++;
        *port = 0;
        while (*p >= '0' && *p <= '9') {
            *port = *port * 10 + (*p - '0');
            p++;
        }
    }
    
    // 提取路径
    if (*p == '/') {
        const char* path_start = p;
        while (*p && *p != '?') p++;
        
        size_t p_len = p - path_start;
        if (p_len >= path_len) return -1;
        memcpy(path, path_start, p_len);
        path[p_len] = '\0';
    } else {
        strcpy(path, "/");
    }
    
    return 0;
}
