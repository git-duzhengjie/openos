#include "display.h"

#include "framebuffer.h"

static display_device_t primary_display;
static uint8_t display_ready;

void display_init(void) {
    display_surface_t surface;
    const framebuffer_info_t *fb = framebuffer_get_info();

    if (fb && fb->available) {
        surface.base = (uint32_t)fb->virt_addr;
        surface.mode.width = fb->width;
        surface.mode.height = fb->height;
        surface.mode.pitch = fb->pitch;
        surface.mode.bpp = fb->bpp;
        surface.mode.format = DISPLAY_PIXEL_FORMAT_XRGB8888;
    } else {
        surface.base = 0;
        surface.mode.width = 0;
        surface.mode.height = 0;
        surface.mode.pitch = 0;
        surface.mode.bpp = 0;
        surface.mode.format = DISPLAY_PIXEL_FORMAT_UNKNOWN;
    }

    primary_display.name = "legacy-framebuffer";
    primary_display.ops = 0;
    primary_display.surface = surface;
    primary_display.flags = 0;
    display_ready = 1;
}

int display_register_primary(const char *name, const display_ops_t *ops, const display_surface_t *surface) {
    if (!surface) {
        return -1;
    }

    primary_display.name = name ? name : "display0";
    primary_display.ops = ops;
    primary_display.surface = *surface;
    primary_display.flags = 0;
    display_ready = 1;
    return 0;
}

const display_device_t *display_get_primary(void) {
    if (!display_ready) {
        display_init();
    }
    return &primary_display;
}

int display_get_primary_surface(display_surface_t *surface) {
    const display_device_t *device;
    if (!surface) {
        return -1;
    }

    device = display_get_primary();
    *surface = device->surface;
    if (device->ops && device->ops->get_primary_surface) {
        return device->ops->get_primary_surface(surface);
    }
    return 0;
}

int display_present_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    const display_device_t *device = display_get_primary();
    if (device->ops && device->ops->present_rect) {
        return device->ops->present_rect(x, y, width, height);
    }

    (void)x;
    (void)y;
    (void)width;
    (void)height;
    return 0;
}

int display_set_mode(const display_mode_t *mode) {
    const display_device_t *device;
    if (!mode) {
        return -1;
    }

    device = display_get_primary();
    if (device->ops && device->ops->set_mode) {
        return device->ops->set_mode(mode);
    }

    return -2;
}
