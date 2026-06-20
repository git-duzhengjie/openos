#include "openos_netsurf_platform.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    openos_ns_surface_t surface;
    openos_ns_rect_t rect;
    openos_ns_text_metrics_t metrics;
    uint32_t pixels[4] = { 0xff0000ffu, 0xff00ff00u, 0xffff0000u, 0xffffffffu };
    char http[128];

    if (openos_ns_platform_init() != 0) return 1;
    if (openos_ns_surface_init(&surface, 64, 48) != 0) return 2;

    openos_ns_surface_clear(&surface, 0xff202020u);
    rect.x = 4;
    rect.y = 4;
    rect.w = 20;
    rect.h = 12;
    if (openos_ns_surface_fill(&surface, &rect, 0xff336699u) != 0) return 3;
    if (openos_ns_surface_blit(&surface, 28, 4, 2, 2, pixels, 2) != 0) return 4;
    if (openos_ns_surface_scroll(&surface, &rect, 0, 4) != 0) return 5;
    if (openos_ns_surface_present(&surface) != 0) return 6;
    if (openos_ns_measure_text("OpenOS NetSurf", &metrics) != 0) return 7;
    if (metrics.width <= 0 || metrics.height <= 0) return 8;
    if (openos_ns_tcp_fetch_http("example.com", "/", http, sizeof(http)) != 0) return 9;
    if (strstr(http, "example.com") == NULL) return 10;

    printf("netsurf-openos smoke: %dx%d metrics=%dx%d time=%u\n",
           surface.width, surface.height, metrics.width, metrics.height, openos_ns_time_ms());
    openos_ns_surface_destroy(&surface);
    openos_ns_platform_shutdown();
    return 0;
}
