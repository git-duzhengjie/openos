#ifndef OPENOS_NETSURF_PLATFORM_H
#define OPENOS_NETSURF_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

typedef struct openos_ns_rect {
    int x;
    int y;
    int w;
    int h;
} openos_ns_rect_t;

typedef struct openos_ns_surface {
    uint32_t *pixels;
    int width;
    int height;
    int stride;
    openos_ns_rect_t clip;
} openos_ns_surface_t;

typedef enum openos_ns_event_type {
    OPENOS_NS_EVENT_NONE = 0,
    OPENOS_NS_EVENT_MOUSE_MOVE,
    OPENOS_NS_EVENT_MOUSE_DOWN,
    OPENOS_NS_EVENT_MOUSE_UP,
    OPENOS_NS_EVENT_KEY_DOWN,
    OPENOS_NS_EVENT_TIMER
} openos_ns_event_type_t;

typedef struct openos_ns_event {
    openos_ns_event_type_t type;
    int x;
    int y;
    int button;
    uint32_t key;
} openos_ns_event_t;

typedef struct openos_ns_text_metrics {
    int width;
    int height;
    int lines;
} openos_ns_text_metrics_t;

int openos_ns_platform_init(void);
void openos_ns_platform_shutdown(void);

int openos_ns_surface_init(openos_ns_surface_t *surface, int width, int height);
void openos_ns_surface_destroy(openos_ns_surface_t *surface);
void openos_ns_surface_set_clip(openos_ns_surface_t *surface, const openos_ns_rect_t *clip);
void openos_ns_surface_clear(openos_ns_surface_t *surface, uint32_t rgba);
int openos_ns_surface_fill(openos_ns_surface_t *surface, const openos_ns_rect_t *rect, uint32_t rgba);
int openos_ns_surface_blit(openos_ns_surface_t *surface, int x, int y, int w, int h, const uint32_t *pixels, int stride);
int openos_ns_surface_draw_text(openos_ns_surface_t *surface, int x, int y, const char *utf8, uint32_t rgba);
int openos_ns_surface_scroll(openos_ns_surface_t *surface, const openos_ns_rect_t *rect, int dx, int dy);
int openos_ns_surface_present(openos_ns_surface_t *surface);

int openos_ns_poll_event(openos_ns_event_t *event);
uint32_t openos_ns_time_ms(void);

int openos_ns_file_read_all(const char *path, uint8_t **out_data, size_t *out_size);
void openos_ns_file_free(void *ptr);

int openos_ns_tcp_fetch_http(const char *host, const char *path, char *out, size_t out_size);
int openos_ns_measure_text(const char *utf8, openos_ns_text_metrics_t *metrics);

#endif
