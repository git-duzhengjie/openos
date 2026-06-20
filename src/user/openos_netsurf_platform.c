#include "openos.h"
#include "../../ports/netsurf-openos/include/openos_netsurf_platform.h"

#define OPENOS_NS_HTTP_PORT "80"
#define OPENOS_NS_FETCH_LIMIT 2048

static int g_openos_ns_window = -1;

int openos_ns_platform_init(void)
{
    if (g_openos_ns_window >= 0) return 0;
    g_openos_ns_window = openos_gui_create_window("NetSurf/OpenOS", 96, 96, 600, 360);
    return g_openos_ns_window >= 0 ? 0 : -1;
}

void openos_ns_platform_shutdown(void)
{
    if (g_openos_ns_window >= 0) {
        openos_gui_destroy_window(g_openos_ns_window);
        g_openos_ns_window = -1;
    }
}

int openos_ns_surface_init(openos_ns_surface_t *surface, int width, int height)
{
    if (!surface || width <= 0 || height <= 0) return -1;
    surface->pixels = (unsigned int *)malloc(width * height * (int)sizeof(unsigned int));
    if (!surface->pixels) return -1;
    surface->width = width;
    surface->height = height;
    surface->stride = width;
    surface->clip.x = 0;
    surface->clip.y = 0;
    surface->clip.w = width;
    surface->clip.h = height;
    memset(surface->pixels, 0, width * height * (int)sizeof(unsigned int));
    return 0;
}

void openos_ns_surface_destroy(openos_ns_surface_t *surface)
{
    if (!surface) return;
    if (surface->pixels) free(surface->pixels);
    memset(surface, 0, sizeof(*surface));
}

static openos_ns_rect_t openos_ns_clip_rect(openos_ns_surface_t *surface, const openos_ns_rect_t *rect)
{
    openos_ns_rect_t r = *rect;
    int max_x;
    int max_y;
    if (r.x < surface->clip.x) { r.w -= surface->clip.x - r.x; r.x = surface->clip.x; }
    if (r.y < surface->clip.y) { r.h -= surface->clip.y - r.y; r.y = surface->clip.y; }
    max_x = surface->clip.x + surface->clip.w;
    max_y = surface->clip.y + surface->clip.h;
    if (r.x + r.w > max_x) r.w = max_x - r.x;
    if (r.y + r.h > max_y) r.h = max_y - r.y;
    if (r.w < 0) r.w = 0;
    if (r.h < 0) r.h = 0;
    return r;
}

void openos_ns_surface_set_clip(openos_ns_surface_t *surface, const openos_ns_rect_t *clip)
{
    openos_ns_rect_t full;
    if (!surface) return;
    if (!clip) {
        surface->clip.x = 0;
        surface->clip.y = 0;
        surface->clip.w = surface->width;
        surface->clip.h = surface->height;
        return;
    }
    full = *clip;
    surface->clip = openos_ns_clip_rect(surface, &full);
}

void openos_ns_surface_clear(openos_ns_surface_t *surface, uint32_t rgba)
{
    openos_ns_rect_t rect;
    if (!surface) return;
    rect.x = 0;
    rect.y = 0;
    rect.w = surface->width;
    rect.h = surface->height;
    openos_ns_surface_fill(surface, &rect, rgba);
}

int openos_ns_surface_fill(openos_ns_surface_t *surface, const openos_ns_rect_t *rect, uint32_t rgba)
{
    openos_ns_rect_t r;
    if (!surface || !surface->pixels || !rect) return -1;
    r = openos_ns_clip_rect(surface, rect);
    for (int y = 0; y < r.h; ++y) {
        unsigned int *row = surface->pixels + (r.y + y) * surface->stride + r.x;
        for (int x = 0; x < r.w; ++x) row[x] = rgba;
    }
    return 0;
}

int openos_ns_surface_blit(openos_ns_surface_t *surface, int x, int y, int w, int h, const uint32_t *pixels, int stride)
{
    openos_ns_rect_t r;
    if (!surface || !surface->pixels || !pixels || w <= 0 || h <= 0 || stride <= 0) return -1;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    r = openos_ns_clip_rect(surface, &r);
    for (int yy = 0; yy < r.h; ++yy) {
        const unsigned int *src = pixels + (yy + r.y - y) * stride + (r.x - x);
        unsigned int *dst = surface->pixels + (r.y + yy) * surface->stride + r.x;
        memcpy(dst, src, r.w * (int)sizeof(unsigned int));
    }
    return 0;
}

int openos_ns_surface_draw_text(openos_ns_surface_t *surface, int x, int y, const char *utf8, uint32_t rgba)
{
    (void)surface;
    if (g_openos_ns_window < 0 || !utf8) return -1;
    return openos_gui_draw_text(g_openos_ns_window, 12 + x, 48 + y, utf8, rgba);
}

int openos_ns_surface_scroll(openos_ns_surface_t *surface, const openos_ns_rect_t *rect, int dx, int dy)
{
    openos_ns_rect_t r;
    if (!surface || !surface->pixels || !rect || dx != 0) return -1;
    r = openos_ns_clip_rect(surface, rect);
    if (dy > 0) {
        for (int y = r.h - 1; y >= 0; --y) {
            int sy = r.y + y;
            int ty = sy + dy;
            if (ty < r.y || ty >= r.y + r.h) continue;
            memmove(surface->pixels + ty * surface->stride + r.x,
                    surface->pixels + sy * surface->stride + r.x,
                    r.w * (int)sizeof(unsigned int));
        }
    } else {
        for (int y = 0; y < r.h; ++y) {
            int sy = r.y + y;
            int ty = sy + dy;
            if (ty < r.y || ty >= r.y + r.h) continue;
            memmove(surface->pixels + ty * surface->stride + r.x,
                    surface->pixels + sy * surface->stride + r.x,
                    r.w * (int)sizeof(unsigned int));
        }
    }
    return 0;
}

int openos_ns_surface_present(openos_ns_surface_t *surface)
{
    if (!surface || !surface->pixels || g_openos_ns_window < 0) return -1;
    openos_gui_blit_rgba32(g_openos_ns_window, 12, 48, surface->width, surface->height,
                           surface->pixels, (unsigned int)surface->stride);
    return openos_gui_present(g_openos_ns_window);
}

int openos_ns_poll_event(openos_ns_event_t *event)
{
    openos_gui_event_t gui_event;
    int rc;
    if (!event) return -1;
    memset(event, 0, sizeof(*event));
    rc = openos_gui_poll_event(&gui_event);
    if (rc <= 0) return rc;
    event->type = OPENOS_NS_EVENT_MOUSE_DOWN;
    event->x = gui_event.x;
    event->y = gui_event.y;
    event->button = gui_event.button;
    return 1;
}

uint32_t openos_ns_time_ms(void)
{
    return openos_uptime_ms();
}

int openos_ns_file_read_all(const char *path, uint8_t **out_data, size_t *out_size)
{
    int fd;
    int cap = 1024;
    int total = 0;
    unsigned char *data;
    if (!path || !out_data || !out_size) return -1;
    *out_data = 0;
    *out_size = 0;
    fd = openos_open(path, O_RDONLY, 0);
    if (fd < 0) return -1;
    data = (unsigned char *)malloc(cap + 1);
    if (!data) { openos_close(fd); return -1; }
    for (;;) {
        int n;
        if (total + 256 > cap) {
            unsigned char *next;
            cap *= 2;
            next = (unsigned char *)realloc(data, cap + 1);
            if (!next) { free(data); openos_close(fd); return -1; }
            data = next;
        }
        n = openos_read(fd, data + total, 256);
        if (n <= 0) break;
        total += n;
    }
    openos_close(fd);
    data[total] = 0;
    *out_data = data;
    *out_size = (size_t)total;
    return 0;
}

void openos_ns_file_free(void *ptr)
{
    if (ptr) free(ptr);
}

int openos_ns_tcp_fetch_http(const char *host, const char *path, char *out, size_t out_size)
{
    openos_addrinfo_t hints;
    openos_addrinfo_t *res = 0;
    int fd;
    int total = 0;
    char request[256];
    if (!host || !path || !out || out_size == 0) return -1;
    out[0] = 0;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = OPENOS_AF_INET;
    hints.ai_socktype = OPENOS_SOCK_STREAM;
    if (openos_getaddrinfo(host, OPENOS_NS_HTTP_PORT, &hints, &res) < 0 || !res) return -1;
    fd = openos_socket(OPENOS_AF_INET, OPENOS_SOCK_STREAM, 0);
    if (fd < 0) { openos_freeaddrinfo(res); return -1; }
    if (openos_connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        openos_close(fd);
        openos_freeaddrinfo(res);
        return -1;
    }
    snprintf(request, sizeof(request), "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
    if (openos_send(fd, request, strlen(request), 0) < 0) {
        openos_close(fd);
        openos_freeaddrinfo(res);
        return -1;
    }
    while ((size_t)total < out_size - 1 && total < OPENOS_NS_FETCH_LIMIT) {
        int n = openos_recv(fd, out + total, (unsigned int)(out_size - 1 - (size_t)total), 0);
        if (n <= 0) break;
        total += n;
    }
    out[total] = 0;
    openos_close(fd);
    openos_freeaddrinfo(res);
    return total > 0 ? 0 : -1;
}

int openos_ns_measure_text(const char *utf8, openos_ns_text_metrics_t *metrics)
{
    openos_font_query_t query;
    if (!metrics) return -1;
    memset(&query, 0, sizeof(query));
    if (utf8) strncpy(query.text, utf8, sizeof(query.text) - 1);
    if (openos_font_query(&query) < 0) return -1;
    metrics->width = (int)query.text_width;
    metrics->height = (int)query.text_height;
    metrics->lines = (int)query.text_lines;
    return 0;
}
