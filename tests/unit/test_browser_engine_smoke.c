#include <stdio.h>
#include <string.h>

#include "../../src/user/browser_engine.h"

int main(void)
{
    const char *html = "<html><head><title>OpenOS &amp; Browser</title></head>"
                       "<body><h1>Hello</h1><p>Lightweight engine</p></body></html>";
    ob_html_tokenizer_base_t tokenizer;
    ob_html_token_t tok;
    ob_html_parser_base_t parser;
    ob_dom_text_renderer_base_t renderer;
    ob_dom_document_t doc;
    char rendered[256];
    int saw_html = 0;
    int saw_text = 0;

    ob_html_tokenizer_base_init(&tokenizer, html);
    while (tokenizer.iface.next(&tokenizer.iface, &tok) == 0 && tok.type != OB_HTML_TOKEN_EOF) {
        if (tok.type == OB_HTML_TOKEN_TAG && strcmp(tok.name, "html") == 0) saw_html = 1;
        if (tok.type == OB_HTML_TOKEN_TEXT && tok.text_len > 0) saw_text = 1;
    }
    if (!saw_html || !saw_text) {
        fprintf(stderr, "browser tokenizer smoke failed\n");
        return 1;
    }

    ob_html_parser_base_init(&parser);
    ob_dom_text_renderer_base_init(&renderer);
    if (parser.iface.parse(&parser.iface, html, &doc) <= 1) {
        fprintf(stderr, "browser parser smoke failed\n");
        return 1;
    }
    if (doc.root != 0 || doc.nodes[0].type != OB_DOM_NODE_DOCUMENT) {
        fprintf(stderr, "browser DOM root smoke failed\n");
        return 1;
    }

    if (renderer.iface.render(&renderer.iface, &doc, rendered, sizeof(rendered)) <= 0 ||
        strstr(rendered, "OpenOS & Browser") ||
        !strstr(rendered, "Hello\nLightweight engine")) {
        fprintf(stderr, "browser DOM text renderer smoke failed: %s\n", rendered);
        return 1;
    }

    {
        const char *semantic = "<header>Top</header><nav>Nav</nav><main><article><section>Body</section></article></main><footer>Bottom</footer>";
        if (parser.iface.parse(&parser.iface, semantic, &doc) <= 1 ||
            renderer.iface.render(&renderer.iface, &doc, rendered, sizeof(rendered)) <= 0 ||
            !strstr(rendered, "Top\nNav\nBody\nBottom")) {
            fprintf(stderr, "browser default display smoke failed: %s\n", rendered);
            return 1;
        }
    }

    {
        const char *attrs = "<main data-x=\"1>2\"><p class='lead'>A<br>B<img src=\"x>y.png\">C<meta charset=\"utf-8\"><p>D</p></main>";
        if (parser.iface.parse(&parser.iface, attrs, &doc) <= 1 ||
            renderer.iface.render(&renderer.iface, &doc, rendered, sizeof(rendered)) <= 0 ||
            !strstr(rendered, "A\nB [Image src=\"x>y.png\"]C\nD")) {
            fprintf(stderr, "browser tokenizer attrs/void smoke failed: %s\n", rendered);
            return 1;
        }
    }

    {
        const char *links = "<main><p><a href=\"/one\">One</a> <a href='/two'>Two</a> <a href=three>Three</a></p></main>";
        int first_link = -1;
        int second_link = -1;
        int third_link = -1;
        int i;
        if (parser.iface.parse(&parser.iface, links, &doc) <= 1 ||
            renderer.iface.render(&renderer.iface, &doc, rendered, sizeof(rendered)) <= 0 ||
            !strstr(rendered, "One [1] /one Two [2] /two Three [3] three")) {
            fprintf(stderr, "browser link render smoke failed: %s\n", rendered);
            return 1;
        }
        for (i = 0; i < doc.count; ++i) {
            if (strcmp(doc.nodes[i].name, "a") == 0) {
                if (first_link < 0) first_link = i;
                else if (second_link < 0) second_link = i;
                else if (third_link < 0) third_link = i;
            }
        }
        if (first_link < 0 || second_link < 0 || third_link < 0 ||
            strcmp(doc.nodes[first_link].href, "/one") != 0 ||
            strcmp(doc.nodes[second_link].href, "/two") != 0 ||
            strcmp(doc.nodes[third_link].href, "three") != 0) {
            fprintf(stderr, "browser href extraction smoke failed: %s | %s | %s\n",
                    first_link >= 0 ? doc.nodes[first_link].href : "",
                    second_link >= 0 ? doc.nodes[second_link].href : "",
                    third_link >= 0 ? doc.nodes[third_link].href : "");
            return 1;
        }
    }

    {
        const char *nested = "<main><section><p>One</section><p>Two</p></main>";
        int section_id;
        int first_p_id;
        int second_p_id;
        if (parser.iface.parse(&parser.iface, nested, &doc) <= 1) {
            fprintf(stderr, "browser parser nested smoke failed\n");
            return 1;
        }
        section_id = doc.nodes[1].first_child;
        first_p_id = section_id >= 0 ? doc.nodes[section_id].first_child : -1;
        second_p_id = section_id >= 0 ? doc.nodes[section_id].next_sibling : -1;
        if (section_id < 0 || first_p_id < 0 || second_p_id < 0 ||
            strcmp(doc.nodes[section_id].name, "section") != 0 ||
            strcmp(doc.nodes[first_p_id].name, "p") != 0 ||
            strcmp(doc.nodes[second_p_id].name, "p") != 0) {
            fprintf(stderr, "browser parser close-tag stack smoke failed\n");
            return 1;
        }
    }

    {
        const char *outline = "<main><h1>Title</h1><h2>Part</h2><h3>Detail</h3>"
                              "<ul><li>First</li><li>Second <a href='/next'>Next</a></li></ul></main>";
        if (parser.iface.parse(&parser.iface, outline, &doc) <= 1 ||
            renderer.iface.render(&renderer.iface, &doc, rendered, sizeof(rendered)) <= 0 ||
            !strstr(rendered, "# Title\n## Part\n### Detail\n- First\n- Second Next [1] /next")) {
            fprintf(stderr, "browser heading/list render smoke failed: %s\n", rendered);
            return 1;
        }
    }

    {
        const char *forms = "<main><form>"
                            "<input name='q' placeholder='Search'>"
                            "<input type=password name=p value=secret>"
                            "<button value='go'>Go</button>"
                            "<textarea name='msg' placeholder='Message'>Hi</textarea>"
                            "<select name='choice'><option value='a'>A</option></select>"
                            "</form></main>";
        if (parser.iface.parse(&parser.iface, forms, &doc) <= 1 ||
            renderer.iface.render(&renderer.iface, &doc, rendered, sizeof(rendered)) <= 0 ||
            !strstr(rendered, "[input type=\"text\" name=\"q\" placeholder=\"Search\"]") ||
            !strstr(rendered, "[input type=\"password\" name=\"p\" value=\"secret\"]") ||
            !strstr(rendered, "[button value=\"go\"Go]") ||
            !strstr(rendered, "[textarea name=\"msg\" placeholder=\"Message\"Hi]") ||
            !strstr(rendered, "[select name=\"choice\" [option value=\"a\"A]]")) {
            fprintf(stderr, "browser form render smoke failed: %s\n", rendered);
            return 1;
        }
    }

    {
        const char *comments = "<!doctype html><!-- hidden --><main>Visible<!-- ignored --><p>Text</p></main>";
        int saw_bang_tag = 0;
        int saw_hidden_text = 0;
        ob_html_tokenizer_base_init(&tokenizer, comments);
        while (tokenizer.iface.next(&tokenizer.iface, &tok) == 0 && tok.type != OB_HTML_TOKEN_EOF) {
            if (tok.type == OB_HTML_TOKEN_TAG && tok.name[0] == '!') saw_bang_tag = 1;
            if (tok.type == OB_HTML_TOKEN_TEXT && tok.text_len == 6 && strncmp(tok.text, "hidden", 6) == 0) saw_hidden_text = 1;
        }
        if (saw_bang_tag || saw_hidden_text ||
            parser.iface.parse(&parser.iface, comments, &doc) <= 1 ||
            renderer.iface.render(&renderer.iface, &doc, rendered, sizeof(rendered)) <= 0 ||
            !strstr(rendered, "Visible\nText") || strstr(rendered, "doctype") || strstr(rendered, "hidden") || strstr(rendered, "ignored")) {
            fprintf(stderr, "browser comment/doctype smoke failed: %s\n", rendered);
            return 1;
        }
    }

    {
        const char *styled = "<main><span style='display:block'>Block</span>"
                             "<p style=\"display:inline\">Inline</p>"
                             "<div style='display:none'>Hidden</div>"
                             "<span style='font-weight:bold'>Bold</span>"
                             "<b style='font-weight:700'>Heavy</b></main>";
        if (parser.iface.parse(&parser.iface, styled, &doc) <= 1 ||
            renderer.iface.render(&renderer.iface, &doc, rendered, sizeof(rendered)) <= 0 ||
            !strstr(rendered, "Block\nInline**Bold****Heavy**") ||
            strstr(rendered, "Hidden")) {
            fprintf(stderr, "browser inline style smoke failed: %s\n", rendered);
            return 1;
        }
    }


    {
        const char *editable_forms = "<form><input name='q' value='hi'><textarea name='msg'></textarea></form>";
        ob_dom_document_t copied_doc;
        ob_form_state_t form;
        if (parser.iface.parse(&parser.iface, editable_forms, &doc) <= 1 ||
            ob_form_state_collect_from_dom(&form, &doc) != 2 || form.focused != 0 ||
            !ob_form_state_handle_key(&form, '!') || !strstr(form.controls[0].value, "hi!") ||
            ob_form_state_focus_next(&form) != 1 ||
            !ob_form_state_handle_key(&form, 'o') || !ob_form_state_handle_key(&form, 'k') ||
            !strstr(form.controls[1].value, "ok") ||
            ob_dom_text_render_with_form_state(&doc, &form, rendered, sizeof(rendered)) <= 0 ||
            !strstr(rendered, "value=\"hi!\"") || !strstr(rendered, "[*textarea") ||
            !strstr(rendered, "value=\"ok\"")) {
            fprintf(stderr, "browser form state editing smoke failed: %s\n", rendered);
            return 1;
        }
        ob_dom_document_copy(&copied_doc, &doc);
        if (copied_doc.count != doc.count || copied_doc.root != doc.root || strcmp(copied_doc.nodes[form.controls[0].node_id].form_name, "q") != 0) {
            fprintf(stderr, "browser DOM copy smoke failed\n");
            return 1;
        }
        if (!ob_form_state_handle_key(&form, 8) || !strstr(form.controls[1].value, "o") ||
            !ob_form_state_handle_key(&form, 27) || form.controls[1].value[0] != 0) {
            fprintf(stderr, "browser form state edit key smoke failed: %s\n", form.controls[1].value);
            return 1;
        }
    }

    {
        const char *submit_form = "<form action='/search' method='get'><input name='q' value='hello world'>"
                                  "<textarea name='msg'>a&b</textarea><input type='submit' value='Go'></form>"
                                  "<form action='/post' method='post'><input name='x' value='1'><button>Send</button></form>";
        ob_form_state_t form;
        int first_input = -1;
        int submit_input = -1;
        int post_button = -1;
        int i;
        char url[160];
        if (parser.iface.parse(&parser.iface, submit_form, &doc) <= 1 ||
            ob_form_state_collect_from_dom(&form, &doc) != 5) {
            fprintf(stderr, "browser form submit collect smoke failed\n");
            return 1;
        }
        for (i = 0; i < doc.count; ++i) {
            if (strcmp(doc.nodes[i].name, "input") == 0 && strcmp(doc.nodes[i].form_name, "q") == 0) first_input = i;
            if (strcmp(doc.nodes[i].name, "input") == 0 && strcmp(doc.nodes[i].form_type, "submit") == 0) submit_input = i;
            if (strcmp(doc.nodes[i].name, "button") == 0) post_button = i;
        }
        if (first_input < 0 || submit_input < 0 || post_button < 0 ||
            form.count != 5 || form.controls[0].editable != 1 || form.controls[2].editable != 0 ||
            ob_form_state_focus_next(&form) < 0 || ob_form_state_handle_key(&form, 'Z') != 1 ||
            ob_form_build_get_url(&doc, &form, first_input, "/base", url, sizeof(url)) <= 0 ||
            strcmp(url, "/search?q=hello+world&msg=a%26bZ") != 0 ||
            ob_form_build_get_url(&doc, &form, submit_input, "/base", url, sizeof(url)) <= 0 ||
            strcmp(url, "/search?q=hello+world&msg=a%26bZ") != 0 ||
            ob_form_build_get_url(&doc, &form, post_button, "/base", url, sizeof(url)) != -2) {
            fprintf(stderr, "browser form GET submit smoke failed: %s\n", url);
            return 1;
        }
    }

    {
        char url[128];
        ob_url_join_relative_path(url, sizeof(url), "/docs/guide/index.html", "./intro.html");
        if (strcmp(url, "/docs/guide/intro.html") != 0) {
            fprintf(stderr, "browser relative ./ URL smoke failed: %s\n", url);
            return 1;
        }
        ob_url_join_relative_path(url, sizeof(url), "/docs/guide/index.html", "../api/ref.html?x=1#top");
        if (strcmp(url, "/docs/api/ref.html?x=1#top") != 0) {
            fprintf(stderr, "browser relative ../ URL smoke failed: %s\n", url);
            return 1;
        }
        ob_url_join_relative_path(url, sizeof(url), "/docs/guide/index.html?old=1", "?q=2#part");
        if (strcmp(url, "/docs/guide/index.html?q=2#part") != 0) {
            fprintf(stderr, "browser relative query URL smoke failed: %s\n", url);
            return 1;
        }
        ob_url_join_relative_path(url, sizeof(url), "/docs/guide/", "../index.html#home");
        if (strcmp(url, "/docs/index.html#home") != 0) {
            fprintf(stderr, "browser directory relative URL smoke failed: %s\n", url);
            return 1;
        }
        ob_url_join_relative_path(url, sizeof(url), "/docs/guide/index.html", "/assets/../img/logo.png?v=1");
        if (strcmp(url, "/img/logo.png?v=1") != 0) {
            fprintf(stderr, "browser absolute normalized URL smoke failed: %s\n", url);
            return 1;
        }
    }

    {
        ob_url_parts_t parts;
        char error[64];
        if (ob_url_parse_address("http://example.com/docs/../index.html?q=1#top", 0, &parts, error, sizeof(error)) != 0 ||
            parts.is_file || strcmp(parts.host, "example.com") != 0 || strcmp(parts.path, "/index.html?q=1#top") != 0) {
            fprintf(stderr, "browser address parse http smoke failed: host=%s path=%s error=%s\n", parts.host, parts.path, error);
            return 1;
        }
        if (ob_url_parse_address("/local/page.html", 0, &parts, error, sizeof(error)) != 0 ||
            !parts.is_file || strcmp(parts.path, "/local/page.html") != 0) {
            fprintf(stderr, "browser address parse file path smoke failed: path=%s error=%s\n", parts.path, error);
            return 1;
        }
        if (ob_url_parse_address("/remote/path", "openos.local", &parts, error, sizeof(error)) != 0 ||
            parts.is_file || strcmp(parts.host, "openos.local") != 0 || strcmp(parts.path, "/remote/path") != 0) {
            fprintf(stderr, "browser address parse default host smoke failed: host=%s path=%s error=%s\n", parts.host, parts.path, error);
            return 1;
        }
        if (ob_url_parse_address("https://example.com/secure", 0, &parts, error, sizeof(error)) != 0 ||
            parts.is_file || !parts.is_https || strcmp(parts.host, "example.com") != 0 ||
            strcmp(parts.path, "/secure") != 0) {
            fprintf(stderr, "browser address parse https smoke failed: host=%s path=%s https=%d error=%s\n",
                    parts.host, parts.path, parts.is_https, error);
            return 1;
        }
    }

    {
        const char *images = "<main><img src='img/logo.png' alt='Logo' width='32' height='16'></main>";
        int img_id = -1;
        int i;
        if (parser.iface.parse(&parser.iface, images, &doc) <= 1) {
            fprintf(stderr, "browser image parse smoke failed\n");
            return 1;
        }
        ob_dom_normalize_resource_urls(&doc, "/docs/index.html");
        if (renderer.iface.render(&renderer.iface, &doc, rendered, sizeof(rendered)) <= 0 ||
            !strstr(rendered, "[Image: Logo src=\"/docs/img/logo.png\" size=\"32x16\"]")) {
            fprintf(stderr, "browser image placeholder smoke failed: %s\n", rendered);
            return 1;
        }
        for (i = 0; i < doc.count; ++i) {
            if (strcmp(doc.nodes[i].name, "img") == 0) img_id = i;
        }
        if (img_id < 0 || strcmp(doc.nodes[img_id].img_src, "/docs/img/logo.png") != 0 ||
            strcmp(doc.nodes[img_id].img_alt, "Logo") != 0 || strcmp(doc.nodes[img_id].img_width, "32") != 0 ||
            strcmp(doc.nodes[img_id].img_height, "16") != 0) {
            fprintf(stderr, "browser image attrs smoke failed\n");
            return 1;
        }
    }

    {
        const char *response = "HTTP/1.1 302 Found\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: 42\r\nLocation: /next\r\n\r\n<html></html>";
        ob_http_headers_t headers;
        if (ob_http_parse_headers(response, &headers) != 0 || strcmp(headers.status_line, "HTTP/1.1 302 Found") != 0 ||
            strcmp(headers.content_type, "text/html; charset=utf-8") != 0 || strcmp(headers.content_length, "42") != 0 ||
            strcmp(headers.location, "/next") != 0 || !ob_http_content_is_renderable_html(headers.content_type) ||
            ob_http_content_is_renderable_html("image/png")) {
            fprintf(stderr, "browser HTTP header smoke failed: status=%s type=%s len=%s loc=%s\n",
                    headers.status_line, headers.content_type, headers.content_length, headers.location);
            return 1;
        }
    }

    {
        char many_nodes[OB_HTML_LIMIT_BYTES];
        int pos = 0;
        int i;
        pos += snprintf(many_nodes + pos, sizeof(many_nodes) - (size_t)pos, "<main>");
        for (i = 0; i < OB_MAX_DOM_NODES * 2 && pos < (int)sizeof(many_nodes) - 16; ++i) {
            pos += snprintf(many_nodes + pos, sizeof(many_nodes) - (size_t)pos, "<span>x</span>");
        }
        snprintf(many_nodes + pos, sizeof(many_nodes) - (size_t)pos, "</main>");
        if (parser.iface.parse(&parser.iface, many_nodes, &doc) <= 1 || doc.count != OB_MAX_DOM_NODES ||
            renderer.iface.render(&renderer.iface, &doc, rendered, sizeof(rendered)) <= 0) {
            fprintf(stderr, "browser DOM node limit smoke failed: count=%d output=%s\n", doc.count, rendered);
            return 1;
        }
    }

    printf("browser engine smoke ok: nodes=%d\n", doc.count);
    return 0;
}
