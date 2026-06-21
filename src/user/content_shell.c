#include "openos.h"

#define CONTENT_W 360
#define CONTENT_H 220

typedef struct content_url {
    char scheme[8];
    char host[96];
    char path[160];
} content_url_t;

static void content_parse_url(const char *url, content_url_t *out)
{
    const char *p;
    const char *slash;
    int n;

    memset(out, 0, sizeof(*out));
    strcpy(out->scheme, "http");
    strcpy(out->host, "example.com");
    strcpy(out->path, "/");
    if (!url || !*url) {
        return;
    }

    p = strstr(url, "://");
    if (p) {
        n = (int)(p - url);
        if (n > 0 && n < (int)sizeof(out->scheme)) {
            memcpy(out->scheme, url, n);
            out->scheme[n] = 0;
        }
        p += 3;
    } else {
        p = url;
    }

    slash = strchr(p, '/');
    if (!slash) {
        strncpy(out->host, p, sizeof(out->host) - 1);
        out->host[sizeof(out->host) - 1] = 0;
        return;
    }

    n = (int)(slash - p);
    if (n > 0) {
        if (n >= (int)sizeof(out->host)) n = (int)sizeof(out->host) - 1;
        memcpy(out->host, p, n);
        out->host[n] = 0;
    }
    strncpy(out->path, slash, sizeof(out->path) - 1);
    out->path[sizeof(out->path) - 1] = 0;
}

static int has_arg(int argc, char **argv, const char *arg)
{
    int i;
    for (i = 1; i < argc; ++i) {
        if (argv[i] && strcmp(argv[i], arg) == 0) {
            return 1;
        }
    }
    return 0;
}

static const char *find_url_arg(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; ++i) {
        if (!argv[i]) continue;
        if (strncmp(argv[i], "--", 2) == 0) continue;
        return argv[i];
    }
    return "http://example.com/";
}

static void draw_page(int win, const content_url_t *url, int disable_gpu, int disable_sandbox)
{
    char line[180];

    openos_gui_fill_rect(win, 0, 0, CONTENT_W, CONTENT_H, 0xfff8fafdU);
    openos_gui_fill_rect(win, 0, 0, CONTENT_W, 34, 0xff202124U);
    openos_gui_draw_text(win, 12, 11, "content_shell", 0xffffffffU);

    openos_gui_fill_rect(win, 12, 46, CONTENT_W - 24, 24, 0xffffffffU);
    snprintf(line, sizeof(line), "%s://%s%s", url->scheme, url->host, url->path);
    openos_gui_draw_text(win, 18, 54, line, 0xff202124U);

    openos_gui_fill_rect(win, 18, 86, CONTENT_W - 36, 82, 0xffffffffU);
    openos_gui_draw_text(win, 30, 100, "Example Domain", 0xff1a73e8U);
    openos_gui_draw_text(win, 30, 124, "This page is rendered by OpenOS content_shell smoke.", 0xff202124U);
    openos_gui_draw_text(win, 30, 146, "single-process HTML/CSS paint path", 0xff5f6368U);

    snprintf(line, sizeof(line), "flags: gpu=%s sandbox=%s", disable_gpu ? "disabled" : "enabled", disable_sandbox ? "disabled" : "enabled");
    openos_gui_draw_text(win, 18, 184, line, 0xff5f6368U);
    openos_gui_present(win);
}

int main(int argc, char **argv)
{
    const char *url_arg = find_url_arg(argc, argv);
    int disable_gpu = has_arg(argc, argv, "--disable-gpu");
    int disable_sandbox = has_arg(argc, argv, "--disable-sandbox");
    content_url_t url;
    int win;

    content_parse_url(url_arg, &url);
    win = openos_gui_create_window("content_shell", 140, 100, CONTENT_W, CONTENT_H);
    if (win < 0) {
        printf("content_shell: failed to create window\n");
        return 1;
    }

    draw_page(win, &url, disable_gpu, disable_sandbox);
    printf("content_shell: single-process %s://%s%s gpu=%s sandbox=%s\n",
           url.scheme, url.host, url.path,
           disable_gpu ? "disabled" : "enabled",
           disable_sandbox ? "disabled" : "enabled");
    return 0;
}
