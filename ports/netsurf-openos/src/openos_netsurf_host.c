#define _POSIX_C_SOURCE 200809L

#include "openos_netsurf_platform.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static openos_ns_rect_t clip_to_surface(const openos_ns_surface_t *surface, const openos_ns_rect_t *rect)
{
    openos_ns_rect_t out = *rect;
    int max_x;
    int max_y;

    if (out.x < surface->clip.x) {
        out.w -= surface->clip.x - out.x;
        out.x = surface->clip.x;
    }
    if (out.y < surface->clip.y) {
        out.h -= surface->clip.y - out.y;
        out.y = surface->clip.y;
    }

    max_x = surface->clip.x + surface->clip.w;
    max_y = surface->clip.y + surface->clip.h;
    if (out.x + out.w > max_x) out.w = max_x - out.x;
    if (out.y + out.h > max_y) out.h = max_y - out.y;
    if (out.w < 0) out.w = 0;
    if (out.h < 0) out.h = 0;
    return out;
}

int openos_ns_platform_init(void)
{
    return 0;
}

void openos_ns_platform_shutdown(void)
{
}

int openos_ns_surface_init(openos_ns_surface_t *surface, int width, int height)
{
    if (!surface || width <= 0 || height <= 0) return -1;
    memset(surface, 0, sizeof(*surface));
    surface->pixels = calloc((size_t)width * (size_t)height, sizeof(uint32_t));
    if (!surface->pixels) return -1;
    surface->width = width;
    surface->height = height;
    surface->stride = width;
    surface->clip.x = 0;
    surface->clip.y = 0;
    surface->clip.w = width;
    surface->clip.h = height;
    return 0;
}

void openos_ns_surface_destroy(openos_ns_surface_t *surface)
{
    if (!surface) return;
    free(surface->pixels);
    memset(surface, 0, sizeof(*surface));
}

void openos_ns_surface_set_clip(openos_ns_surface_t *surface, const openos_ns_rect_t *clip)
{
    openos_ns_rect_t full;
    if (!surface) return;
    full.x = 0;
    full.y = 0;
    full.w = surface->width;
    full.h = surface->height;
    surface->clip = clip ? clip_to_surface(surface, clip) : full;
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
    r = clip_to_surface(surface, rect);
    for (int y = 0; y < r.h; ++y) {
        uint32_t *row = surface->pixels + (r.y + y) * surface->stride + r.x;
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
    r = clip_to_surface(surface, &r);
    for (int yy = 0; yy < r.h; ++yy) {
        const uint32_t *src = pixels + (yy + r.y - y) * stride + (r.x - x);
        uint32_t *dst = surface->pixels + (r.y + yy) * surface->stride + r.x;
        memcpy(dst, src, (size_t)r.w * sizeof(uint32_t));
    }
    return 0;
}

int openos_ns_surface_draw_text(openos_ns_surface_t *surface, int x, int y, const char *utf8, uint32_t rgba)
{
    openos_ns_rect_t rect;
    (void)utf8;
    rect.x = x;
    rect.y = y;
    rect.w = 8;
    rect.h = 16;
    return openos_ns_surface_fill(surface, &rect, rgba);
}

int openos_ns_surface_scroll(openos_ns_surface_t *surface, const openos_ns_rect_t *rect, int dx, int dy)
{
    openos_ns_rect_t r;
    if (!surface || !surface->pixels || !rect) return -1;
    r = clip_to_surface(surface, rect);
    if (r.w <= 0 || r.h <= 0) return 0;
    if (dy > 0) {
        for (int y = r.h - 1; y >= 0; --y) {
            int sy = r.y + y;
            int ty = sy + dy;
            if (ty < r.y || ty >= r.y + r.h) continue;
            memmove(surface->pixels + ty * surface->stride + r.x + dx,
                    surface->pixels + sy * surface->stride + r.x,
                    (size_t)(r.w - (dx > 0 ? dx : 0)) * sizeof(uint32_t));
        }
    } else {
        for (int y = 0; y < r.h; ++y) {
            int sy = r.y + y;
            int ty = sy + dy;
            if (ty < r.y || ty >= r.y + r.h) continue;
            memmove(surface->pixels + ty * surface->stride + r.x + dx,
                    surface->pixels + sy * surface->stride + r.x,
                    (size_t)(r.w - (dx > 0 ? dx : 0)) * sizeof(uint32_t));
        }
    }
    return 0;
}

int openos_ns_surface_present(openos_ns_surface_t *surface)
{
    return surface && surface->pixels ? 0 : -1;
}

int openos_ns_poll_event(openos_ns_event_t *event)
{
    if (!event) return -1;
    memset(event, 0, sizeof(*event));
    return 0;
}

uint32_t openos_ns_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

int openos_ns_file_read_all(const char *path, uint8_t **out_data, size_t *out_size)
{
    FILE *fp;
    long size;
    uint8_t *data;
    if (!path || !out_data || !out_size) return -1;
    *out_data = NULL;
    *out_size = 0;
    fp = fopen(path, "rb");
    if (!fp) return -1;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return -1; }
    size = ftell(fp);
    if (size < 0) { fclose(fp); return -1; }
    rewind(fp);
    data = malloc((size_t)size + 1u);
    if (!data) { fclose(fp); return -1; }
    if (fread(data, 1, (size_t)size, fp) != (size_t)size) { free(data); fclose(fp); return -1; }
    data[size] = 0;
    fclose(fp);
    *out_data = data;
    *out_size = (size_t)size;
    return 0;
}

void openos_ns_file_free(void *ptr)
{
    free(ptr);
}

int openos_ns_tcp_fetch_http(const char *host, const char *path, char *out, size_t out_size)
{
    if (!host || !path || !out || out_size == 0) return -1;
    snprintf(out, out_size, "HTTP fetch placeholder for http://%s%s", host, path);
    return 0;
}

int openos_ns_measure_text(const char *utf8, openos_ns_text_metrics_t *metrics)
{
    size_t len;
    if (!metrics) return -1;
    len = utf8 ? strlen(utf8) : 0;
    metrics->width = (int)len * 8;
    metrics->height = 16;
    metrics->lines = 1;
    return 0;
}
