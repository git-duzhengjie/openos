#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/user/browser_engine.h"

static int read_file(const char *path, char *out, size_t out_size)
{
    FILE *fp;
    size_t n;
    if (!path || !out || out_size == 0) return -1;
    fp = fopen(path, "rb");
    if (!fp) return -1;
    n = fread(out, 1, out_size - 1, fp);
    out[n] = 0;
    fclose(fp);
    return (int)n;
}

static int render_sample(const char *path, const char *base_path, const char *needle)
{
    char html[OB_HTML_LIMIT_BYTES];
    char rendered[512];
    ob_dom_document_t doc;
    ob_dom_document_t copy;
    ob_form_state_t form_state;
    ob_html_parser_base_t parser;
    ob_dom_text_renderer_base_t renderer;
    int nodes;

    if (read_file(path, html, sizeof(html)) <= 0) {
        fprintf(stderr, "sample read failed: %s\n", path);
        return 1;
    }
    ob_html_parser_base_init(&parser);
    ob_dom_text_renderer_base_init(&renderer);
    nodes = parser.iface.parse(&parser.iface, html, &doc);
    if (nodes <= 1 || doc.count > OB_MAX_DOM_NODES) {
        fprintf(stderr, "sample parse failed: %s nodes=%d count=%d\n", path, nodes, doc.count);
        return 1;
    }
    ob_dom_normalize_resource_urls(&doc, base_path);
    ob_dom_document_copy(&copy, &doc);
    ob_form_state_collect_from_dom(&form_state, &copy);
    if (form_state.count > 0) {
        char form_url[128];
        ob_form_state_focus_next(&form_state);
        ob_form_state_handle_key(&form_state, 0);
        ob_form_build_get_url(&copy, &form_state, 0, base_path, form_url, sizeof(form_url));
    }
    if (renderer.iface.render(&renderer.iface, &copy, rendered, sizeof(rendered)) <= 0 || !strstr(rendered, needle)) {
        fprintf(stderr, "sample render failed: %s output=%s\n", path, rendered);
        return 1;
    }
    printf("browser sample ok: %s nodes=%d\n", path, copy.count);
    return 0;
}

int main(void)
{
    ob_url_parts_t parts;
    ob_http_headers_t headers;
    char error[64];
    const char *response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 12\r\n\r\nhello";

    if (ob_url_parse_address("/browser/index.html", "openos.local", &parts, error, sizeof(error)) != 0) return 1;
    if (ob_http_parse_headers(response, &headers) != 0 || !ob_http_content_is_renderable_html(headers.content_type)) return 1;

    if (render_sample("tests/resources/browser/index.html", "/browser/index.html", "[Image: OpenOS logo src=\"/browser/img/logo.png\" size=\"64x32\"]") != 0) return 1;
    if (render_sample("tests/resources/browser/form.html", "/browser/form.html", "[buttonSend]") != 0) return 1;
    if (render_sample("tests/resources/browser/error.html", "/browser/error.html", "Expected Error Path") != 0) return 1;
    return 0;
}
