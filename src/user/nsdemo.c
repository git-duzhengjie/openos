#include "openos.h"
#include "../../ports/netsurf-openos/include/openos_netsurf_platform.h"

static const char *find_body(const char *response)
{
    const char *body = strstr(response, "\r\n\r\n");
    if (body) return body + 4;
    body = strstr(response, "\n\n");
    return body ? body + 2 : response;
}

static void html_to_text(char *dst, int dst_size, const char *src)
{
    int out = 0;
    int in_tag = 0;
    int space = 0;
    if (!dst || dst_size <= 0) return;
    dst[0] = 0;
    for (const char *p = src; p && *p && out < dst_size - 1; ++p) {
        char c = *p;
        if (c == '<') { in_tag = 1; space = 1; continue; }
        if (in_tag) { if (c == '>') in_tag = 0; continue; }
        if (c == '\r' || c == '\n' || c == '\t' || c == ' ') { space = 1; continue; }
        if (space && out > 0) dst[out++] = ' ';
        if (out >= dst_size - 1) break;
        dst[out++] = c;
        space = 0;
    }
    dst[out] = 0;
}

int main(int argc, char **argv)
{
    const char *host = argc > 1 ? argv[1] : "example.com";
    const char *path = argc > 2 ? argv[2] : "/";
    openos_ns_surface_t surface;
    openos_ns_rect_t header;
    openos_ns_text_metrics_t metrics;
    char response[2048];
    char text[512];
    char title[128];

    if (openos_ns_platform_init() < 0) {
        printf("nsdemo: platform init failed\n");
        return 1;
    }
    if (openos_ns_surface_init(&surface, 560, 260) < 0) {
        printf("nsdemo: surface init failed\n");
        return 1;
    }

    openos_ns_surface_clear(&surface, 0xfff4f7fbu);
    header.x = 0;
    header.y = 0;
    header.w = 560;
    header.h = 32;
    openos_ns_surface_fill(&surface, &header, 0xffdbeafeu);
    openos_ns_surface_present(&surface);

    snprintf(title, sizeof(title), "NetSurf/OpenOS demo: http://%s%s", host, path);
    openos_ns_surface_draw_text(&surface, 8, 8, title, 0xff1e3a8au);

    if (openos_ns_tcp_fetch_http(host, path, response, sizeof(response)) < 0) {
        snprintf(text, sizeof(text), "HTTP failed: %s%s", host, path);
    } else {
        html_to_text(text, sizeof(text), find_body(response));
    }

    openos_ns_measure_text(text, &metrics);
    openos_ns_surface_draw_text(&surface, 8, 48, text[0] ? text : "Empty response", 0xff111827u);
    openos_ns_surface_present(&surface);

    printf("nsdemo: loaded http://%s%s metrics=%dx%d\n", host, path, metrics.width, metrics.height);
    printf("%s\n", text);

    openos_ns_surface_destroy(&surface);
    openos_ns_platform_shutdown();
    return 0;
}
