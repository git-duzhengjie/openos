#ifndef OPENOS_USER_BROWSER_ENGINE_H
#define OPENOS_USER_BROWSER_ENGINE_H

/*
 * OpenOS lightweight browser engine interfaces.
 *
 * This header intentionally keeps the first engine layer header-only: each user
 * program is currently linked as a standalone ELF in the OpenOS build system.
 * The structs below emulate interface/base/implementation layering in C.
 */

#include <string.h>

#define OB_HTML_TOKEN_TEXT 1
#define OB_HTML_TOKEN_TAG 2
#define OB_HTML_TOKEN_EOF 3

#define OB_DOM_NODE_DOCUMENT 1
#define OB_DOM_NODE_ELEMENT 2
#define OB_DOM_NODE_TEXT 3

#define OB_DISPLAY_INLINE 1
#define OB_DISPLAY_BLOCK 2
#define OB_DISPLAY_NONE 3

#define OB_MAX_TAG_NAME 16
#define OB_MAX_DOM_NODES 32
#define OB_MAX_NODE_TEXT 96

typedef struct ob_html_token {
    int type;
    char name[OB_MAX_TAG_NAME];
    const char *text;
    int text_len;
    int closing;
    int self_closing;
} ob_html_token_t;

typedef struct ob_html_tokenizer_i ob_html_tokenizer_i_t;
struct ob_html_tokenizer_i {
    int (*next)(ob_html_tokenizer_i_t *self, ob_html_token_t *out);
};

typedef struct ob_html_tokenizer_base {
    ob_html_tokenizer_i_t iface;
    const char *input;
    int pos;
} ob_html_tokenizer_base_t;

typedef struct ob_dom_node {
    int type;
    int parent;
    int first_child;
    int next_sibling;
    int style_display;
    char name[OB_MAX_TAG_NAME];
    char text[OB_MAX_NODE_TEXT];
} ob_dom_node_t;

typedef struct ob_dom_document {
    ob_dom_node_t nodes[OB_MAX_DOM_NODES];
    int count;
    int root;
} ob_dom_document_t;

typedef struct ob_html_parser_i ob_html_parser_i_t;
struct ob_html_parser_i {
    int (*parse)(ob_html_parser_i_t *self, const char *html, ob_dom_document_t *doc);
};

typedef struct ob_html_parser_base {
    ob_html_parser_i_t iface;
} ob_html_parser_base_t;

typedef struct ob_style_resolver_i ob_style_resolver_i_t;
struct ob_style_resolver_i {
    int (*display_for_tag)(ob_style_resolver_i_t *self, const char *tag);
};

typedef struct ob_default_style_resolver {
    ob_style_resolver_i_t iface;
} ob_default_style_resolver_t;

typedef struct ob_dom_text_renderer_i ob_dom_text_renderer_i_t;
struct ob_dom_text_renderer_i {
    int (*render)(ob_dom_text_renderer_i_t *self, const ob_dom_document_t *doc, char *out, int out_size);
};

typedef struct ob_dom_text_renderer_base {
    ob_dom_text_renderer_i_t iface;
} ob_dom_text_renderer_base_t;

static int ob_ascii_equal_ci(char a, char b)
{
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    return a == b;
}

static int ob_token_eq_ci(const char *a, const char *b)
{
    int i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i]) {
        if (!ob_ascii_equal_ci(a[i], b[i])) return 0;
        ++i;
    }
    return a[i] == 0 && b[i] == 0;
}

static int ob_default_display_for_tag(ob_style_resolver_i_t *self, const char *tag)
{
    (void)self;
    if (!tag || !tag[0]) return OB_DISPLAY_INLINE;
    if (ob_token_eq_ci(tag, "script") || ob_token_eq_ci(tag, "style") || ob_token_eq_ci(tag, "head"))
        return OB_DISPLAY_NONE;
    if (ob_token_eq_ci(tag, "p") || ob_token_eq_ci(tag, "br") ||
        ob_token_eq_ci(tag, "div") || ob_token_eq_ci(tag, "h1") ||
        ob_token_eq_ci(tag, "h2") || ob_token_eq_ci(tag, "h3") ||
        ob_token_eq_ci(tag, "li") || ob_token_eq_ci(tag, "ul") ||
        ob_token_eq_ci(tag, "ol") || ob_token_eq_ci(tag, "tr") ||
        ob_token_eq_ci(tag, "table") || ob_token_eq_ci(tag, "section") ||
        ob_token_eq_ci(tag, "article") || ob_token_eq_ci(tag, "nav") ||
        ob_token_eq_ci(tag, "main") || ob_token_eq_ci(tag, "header") ||
        ob_token_eq_ci(tag, "footer") || ob_token_eq_ci(tag, "title"))
        return OB_DISPLAY_BLOCK;
    return OB_DISPLAY_INLINE;
}

static void ob_default_style_resolver_init(ob_default_style_resolver_t *resolver)
{
    if (!resolver) return;
    resolver->iface.display_for_tag = ob_default_display_for_tag;
}

static int ob_cstr_len(const char *s)
{
    int n = 0;
    if (!s) return 0;
    while (s[n]) ++n;
    return n;
}

static char ob_decode_entity_char(const char **pp)
{
    const char *p = *pp;
    if (!p || *p != '&') return p ? *p : 0;
    if (!strncmp(p, "&amp;", 5)) { *pp = p + 4; return '&'; }
    if (!strncmp(p, "&lt;", 4)) { *pp = p + 3; return '<'; }
    if (!strncmp(p, "&gt;", 4)) { *pp = p + 3; return '>'; }
    if (!strncmp(p, "&quot;", 6)) { *pp = p + 5; return '"'; }
    if (!strncmp(p, "&#39;", 5)) { *pp = p + 4; return '\''; }
    if (!strncmp(p, "&apos;", 6)) { *pp = p + 5; return '\''; }
    if (!strncmp(p, "&nbsp;", 6)) { *pp = p + 5; return ' '; }
    return '&';
}

static void ob_dom_render_append_newline(char *out, int out_size, int *pos)
{
    if (!out || !pos || *pos <= 0 || *pos >= out_size - 1) return;
    if (out[*pos - 1] != '\n') out[(*pos)++] = '\n';
}

static void ob_dom_render_append_text(char *out, int out_size, int *pos, const char *text)
{
    int pending_space = 0;
    const char *p = text;
    if (!out || !pos || !text) return;
    while (*p && *pos < out_size - 1) {
        char c = *p;
        if (c == '&') c = ob_decode_entity_char(&p);
        if (c == '\r' || c == '\n' || c == '\t' || c == ' ') {
            pending_space = 1;
            ++p;
            continue;
        }
        if (pending_space && *pos > 0 && out[*pos - 1] != '\n') {
            out[(*pos)++] = ' ';
            if (*pos >= out_size - 1) break;
        }
        pending_space = 0;
        out[(*pos)++] = c;
        ++p;
    }
}

static void ob_copy_tag_name(char *dst, int dst_size, const char *src, int len)
{
    int i = 0;
    if (!dst || dst_size <= 0) return;
    while (i < dst_size - 1 && i < len && src[i]) {
        char c = src[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        dst[i++] = c;
    }
    dst[i] = 0;
}

static int ob_is_void_tag(const char *tag)
{
    return ob_token_eq_ci(tag, "area") || ob_token_eq_ci(tag, "base") ||
           ob_token_eq_ci(tag, "br") || ob_token_eq_ci(tag, "col") ||
           ob_token_eq_ci(tag, "embed") || ob_token_eq_ci(tag, "hr") ||
           ob_token_eq_ci(tag, "img") || ob_token_eq_ci(tag, "input") ||
           ob_token_eq_ci(tag, "link") || ob_token_eq_ci(tag, "meta") ||
           ob_token_eq_ci(tag, "param") || ob_token_eq_ci(tag, "source") ||
           ob_token_eq_ci(tag, "track") || ob_token_eq_ci(tag, "wbr");
}

static int ob_tokenizer_next_impl(ob_html_tokenizer_i_t *iface, ob_html_token_t *out)
{
    ob_html_tokenizer_base_t *self = (ob_html_tokenizer_base_t *)iface;
    const char *s;
    int start;
    int name_start;
    int name_len;
    char quote = 0;
    if (!self || !out || !self->input) return -1;
    s = self->input;
    out->type = OB_HTML_TOKEN_EOF;
    out->name[0] = 0;
    out->text = 0;
    out->text_len = 0;
    out->closing = 0;
    out->self_closing = 0;
    if (!s[self->pos]) return 0;
    if (s[self->pos] != '<') {
        start = self->pos;
        while (s[self->pos] && s[self->pos] != '<') ++self->pos;
        out->type = OB_HTML_TOKEN_TEXT;
        out->text = s + start;
        out->text_len = self->pos - start;
        return 0;
    }
    ++self->pos;
    if (s[self->pos] == '/') { out->closing = 1; ++self->pos; }
    while (s[self->pos] == ' ' || s[self->pos] == '\t' || s[self->pos] == '\r' || s[self->pos] == '\n') ++self->pos;
    name_start = self->pos;
    while (s[self->pos] && s[self->pos] != '>' && s[self->pos] != '/' &&
           s[self->pos] != ' ' && s[self->pos] != '\t' && s[self->pos] != '\r' && s[self->pos] != '\n')
        ++self->pos;
    name_len = self->pos - name_start;
    while (s[self->pos]) {
        char c = s[self->pos];
        if (quote) {
            if (c == quote) quote = 0;
            ++self->pos;
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            ++self->pos;
            continue;
        }
        if (c == '/' && s[self->pos + 1] == '>') out->self_closing = 1;
        if (c == '>') break;
        ++self->pos;
    }
    if (s[self->pos] == '>') ++self->pos;
    out->type = OB_HTML_TOKEN_TAG;
    ob_copy_tag_name(out->name, sizeof(out->name), s + name_start, name_len);
    return 0;
}

static void ob_html_tokenizer_base_init(ob_html_tokenizer_base_t *tokenizer, const char *html)
{
    if (!tokenizer) return;
    tokenizer->iface.next = ob_tokenizer_next_impl;
    tokenizer->input = html ? html : "";
    tokenizer->pos = 0;
}

static int ob_dom_add_node(ob_dom_document_t *doc, int type, const char *name, const char *text, int text_len, int parent, int display)
{
    int id;
    int i;
    if (!doc || doc->count >= OB_MAX_DOM_NODES) return -1;
    id = doc->count++;
    doc->nodes[id].type = type;
    doc->nodes[id].parent = parent;
    doc->nodes[id].first_child = -1;
    doc->nodes[id].next_sibling = -1;
    doc->nodes[id].style_display = display;
    doc->nodes[id].name[0] = 0;
    doc->nodes[id].text[0] = 0;
    if (name) ob_copy_tag_name(doc->nodes[id].name, sizeof(doc->nodes[id].name), name, ob_cstr_len(name));
    if (text && text_len > 0) {
        for (i = 0; i < text_len && i < OB_MAX_NODE_TEXT - 1; ++i) doc->nodes[id].text[i] = text[i];
        doc->nodes[id].text[i] = 0;
    }
    if (parent >= 0 && parent < doc->count) {
        int child = doc->nodes[parent].first_child;
        if (child < 0) doc->nodes[parent].first_child = id;
        else {
            while (doc->nodes[child].next_sibling >= 0) child = doc->nodes[child].next_sibling;
            doc->nodes[child].next_sibling = id;
        }
    }
    return id;
}

static void ob_dom_close_tag(ob_dom_document_t *doc, int *stack, int *depth, const char *tag)
{
    int i;
    if (!doc || !stack || !depth || !tag || !tag[0]) return;
    for (i = *depth - 1; i > 0; --i) {
        int node_id = stack[i];
        if (node_id >= 0 && node_id < doc->count &&
            doc->nodes[node_id].type == OB_DOM_NODE_ELEMENT &&
            ob_token_eq_ci(doc->nodes[node_id].name, tag)) {
            *depth = i;
            return;
        }
    }
}

static int ob_html_parse_impl(ob_html_parser_i_t *iface, const char *html, ob_dom_document_t *doc)
{
    ob_html_tokenizer_base_t tokenizer;
    ob_default_style_resolver_t styles;
    ob_html_token_t tok;
    int stack[OB_MAX_DOM_NODES];
    int depth = 0;
    int current;
    (void)iface;
    if (!doc) return -1;
    doc->count = 0;
    doc->root = ob_dom_add_node(doc, OB_DOM_NODE_DOCUMENT, "#document", 0, 0, -1, OB_DISPLAY_BLOCK);
    if (doc->root < 0) return -1;
    stack[depth++] = doc->root;
    ob_default_style_resolver_init(&styles);
    ob_html_tokenizer_base_init(&tokenizer, html);
    while (tokenizer.iface.next(&tokenizer.iface, &tok) == 0 && tok.type != OB_HTML_TOKEN_EOF) {
        current = stack[depth - 1];
        if (tok.type == OB_HTML_TOKEN_TEXT) {
            ob_dom_add_node(doc, OB_DOM_NODE_TEXT, "#text", tok.text, tok.text_len, current, OB_DISPLAY_INLINE);
        } else if (tok.type == OB_HTML_TOKEN_TAG && tok.name[0]) {
            if (tok.closing) {
                ob_dom_close_tag(doc, stack, &depth, tok.name);
            } else {
                int display = styles.iface.display_for_tag(&styles.iface, tok.name);
                int id = ob_dom_add_node(doc, OB_DOM_NODE_ELEMENT, tok.name, 0, 0, current, display);
                if (id >= 0 && !tok.self_closing && !ob_is_void_tag(tok.name) && depth < OB_MAX_DOM_NODES) stack[depth++] = id;
            }
        }
    }
    return doc->count;
}

static void ob_html_parser_base_init(ob_html_parser_base_t *parser)
{
    if (!parser) return;
    parser->iface.parse = ob_html_parse_impl;
}

static void ob_dom_render_node_text(const ob_dom_document_t *doc, int node_id, char *out, int out_size, int *pos)
{
    int child;
    int is_block;
    if (!doc || node_id < 0 || node_id >= doc->count || !out || !pos || *pos >= out_size - 1) return;
    if (doc->nodes[node_id].style_display == OB_DISPLAY_NONE) return;
    is_block = doc->nodes[node_id].type == OB_DOM_NODE_ELEMENT && doc->nodes[node_id].style_display == OB_DISPLAY_BLOCK;
    if (is_block && *pos > 0) ob_dom_render_append_newline(out, out_size, pos);
    if (doc->nodes[node_id].type == OB_DOM_NODE_TEXT) ob_dom_render_append_text(out, out_size, pos, doc->nodes[node_id].text);
    child = doc->nodes[node_id].first_child;
    while (child >= 0 && *pos < out_size - 1) {
        ob_dom_render_node_text(doc, child, out, out_size, pos);
        child = doc->nodes[child].next_sibling;
    }
    if (is_block && *pos > 0) ob_dom_render_append_newline(out, out_size, pos);
}

static int ob_dom_text_render_impl(ob_dom_text_renderer_i_t *iface, const ob_dom_document_t *doc, char *out, int out_size)
{
    int pos = 0;
    (void)iface;
    if (!out || out_size <= 0) return -1;
    out[0] = 0;
    if (!doc || doc->root < 0 || doc->root >= doc->count) return -1;
    ob_dom_render_node_text(doc, doc->root, out, out_size, &pos);
    while (pos > 0 && (out[pos - 1] == ' ' || out[pos - 1] == '\n')) --pos;
    out[pos] = 0;
    return pos;
}

static void ob_dom_text_renderer_base_init(ob_dom_text_renderer_base_t *renderer)
{
    if (!renderer) return;
    renderer->iface.render = ob_dom_text_render_impl;
}

#endif /* OPENOS_USER_BROWSER_ENGINE_H */
