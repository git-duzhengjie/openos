#ifndef OPENOS_HTTP_CLIENT_H
#define OPENOS_HTTP_CLIENT_H

#include <stddef.h>
#include <stdint.h>
#include "socket.h"

// HTTP 方法
typedef enum {
    HTTP_GET,
    HTTP_POST,
    HTTP_HEAD,
    HTTP_PUT,
    HTTP_DELETE,
} http_method_t;

// HTTP 状态码
typedef enum {
    HTTP_STATUS_CONTINUE = 100,
    HTTP_STATUS_SWITCHING_PROTOCOLS = 101,
    HTTP_STATUS_OK = 200,
    HTTP_STATUS_CREATED = 201,
    HTTP_STATUS_ACCEPTED = 202,
    HTTP_STATUS_NO_CONTENT = 204,
    HTTP_STATUS_PARTIAL_CONTENT = 206,
    HTTP_STATUS_MOVED_PERMANENTLY = 301,
    HTTP_STATUS_FOUND = 302,
    HTTP_STATUS_NOT_MODIFIED = 304,
    HTTP_STATUS_TEMPORARY_REDIRECT = 307,
    HTTP_STATUS_PERMANENT_REDIRECT = 308,
    HTTP_STATUS_BAD_REQUEST = 400,
    HTTP_STATUS_UNAUTHORIZED = 401,
    HTTP_STATUS_FORBIDDEN = 403,
    HTTP_STATUS_NOT_FOUND = 404,
    HTTP_STATUS_METHOD_NOT_ALLOWED = 405,
    HTTP_STATUS_REQUEST_TIMEOUT = 408,
    HTTP_STATUS_TOO_MANY_REQUESTS = 429,
    HTTP_STATUS_INTERNAL_SERVER_ERROR = 500,
    HTTP_STATUS_NOT_IMPLEMENTED = 501,
    HTTP_STATUS_BAD_GATEWAY = 502,
    HTTP_STATUS_SERVICE_UNAVAILABLE = 503,
    HTTP_STATUS_GATEWAY_TIMEOUT = 504,
} http_status_t;

// HTTP 头部链表
typedef struct http_header {
    char* name;
    char* value;
    struct http_header* next;
} http_header_t;

// HTTP 响应
typedef struct {
    int status_code;
    char* status_text;
    char* http_version;
    http_header_t* headers;
    uint8_t* body;
    size_t body_len;
    size_t content_length;
    int chunked;
    int keep_alive;
} http_response_t;

// HTTP 请求配置
typedef struct {
    int timeout_ms;
    int max_redirects;
    int keep_alive;
    const char* user_agent;
} http_config_t;

// HTTP 客户端
typedef struct {
    socket_t sock;
    http_config_t config;
    int connected;
    char host[256];
    int port;
    int use_https;
} http_client_t;

// 初始化 HTTP 客户端
int http_client_init(http_client_t* client, const http_config_t* config);

// 连接到服务器
int http_client_connect(http_client_t* client, const char* host, int port, int use_https);

// 发送 HTTP 请求
int http_client_request(http_client_t* client,
                        http_method_t method,
                        const char* path,
                        const http_header_t* extra_headers,
                        const uint8_t* body,
                        size_t body_len,
                        http_response_t* response);

// 释放响应资源
void http_response_free(http_response_t* response);

// 断开连接
void http_client_disconnect(http_client_t* client);

// 工具函数
const char* http_get_header(const http_response_t* response, const char* name);
int http_parse_url(const char* url, char* host, size_t host_len,
                   int* port, char* path, size_t path_len, int* use_https);

#endif
