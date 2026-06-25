#ifndef OPENOS_DISPLAY_H
#define OPENOS_DISPLAY_H

#include "types.h"

#define OPENOS_DISPLAY_ABI_VERSION_MAJOR 1u
#define OPENOS_DISPLAY_ABI_VERSION_MINOR 0u
#define OPENOS_DISPLAY_ABI_VERSION_PATCH 0u

typedef enum display_pixel_format {
    DISPLAY_PIXEL_FORMAT_UNKNOWN = 0,
    DISPLAY_PIXEL_FORMAT_RGB565 = 1,
    DISPLAY_PIXEL_FORMAT_RGB888 = 2,
    DISPLAY_PIXEL_FORMAT_XRGB8888 = 3,
    DISPLAY_PIXEL_FORMAT_ARGB8888 = 4,
} display_pixel_format_t;

typedef struct display_mode {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    display_pixel_format_t format;
} display_mode_t;

typedef struct display_surface {
    uint32_t base;
    display_mode_t mode;
} display_surface_t;

typedef struct display_ops {
    int (*get_primary_surface)(display_surface_t *surface);
    int (*present_rect)(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
    int (*set_mode)(const display_mode_t *mode);
} display_ops_t;

typedef struct display_device {
    const char *name;
    const display_ops_t *ops;
    display_surface_t surface;
    uint32_t flags;
} display_device_t;

void display_init(void);
int display_register_primary(const char *name, const display_ops_t *ops, const display_surface_t *surface);
const display_device_t *display_get_primary(void);
int display_get_primary_surface(display_surface_t *surface);
int display_present_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
int display_set_mode(const display_mode_t *mode);

#endif /* OPENOS_DISPLAY_H */
