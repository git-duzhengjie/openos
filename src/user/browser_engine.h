#ifndef OPENOS_USER_BROWSER_ENGINE_H
#define OPENOS_USER_BROWSER_ENGINE_H

/*
 * OpenOS lightweight browser engine interfaces.
 *
 * This header intentionally keeps the first engine layer header-only: each user
 * program is currently linked as a standalone ELF in the OpenOS build system.
 * The structs below emulate interface/base/implementation layering in C.
 */

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
#define OB_MAX_ATTR_VALUE 96
#define OB_FORM_STATE_MAX_CONTROLS 8
#define OB_MAX_HEADER_VALUE 128
#define OB_HTML_LIMIT_BYTES 4096


typedef struct ob_html_token {
    int type;
    char name[OB_MAX_TAG_NAME];
    const char *text;
    int text_len;
    int closing;
    int self_closing;
    const char *attrs;
    int attrs_len;
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
    int font_weight_bold;
    char name[OB_MAX_TAG_NAME];
    char text[OB_MAX_NODE_TEXT];
    char href[OB_MAX_ATTR_VALUE];
    char img_src[OB_MAX_ATTR_VALUE];
    char img_alt[OB_MAX_ATTR_VALUE];
    char img_width[OB_MAX_ATTR_VALUE];
    char img_height[OB_MAX_ATTR_VALUE];
    char form_type[OB_MAX_ATTR_VALUE];
    char form_value[OB_MAX_ATTR_VALUE];
    char form_placeholder[OB_MAX_ATTR_VALUE];
    char form_name[OB_MAX_ATTR_VALUE];
    char form_action[OB_MAX_ATTR_VALUE];
    char form_method[OB_MAX_ATTR_VALUE];
    int form_owner;
} ob_dom_node_t;

typedef struct ob_dom_document {
    ob_dom_node_t nodes[OB_MAX_DOM_NODES];
    int count;
    int root;
} ob_dom_document_t;

static void ob_dom_copy_text(char *dst, int dst_size, const char *src)
{
    int i;
    if (!dst || dst_size <= 0) return;
    if (!src) src = "";
    for (i = 0; i < dst_size - 1 && src[i]; ++i) dst[i] = src[i];
    dst[i] = 0;
}

static void ob_dom_node_copy(ob_dom_node_t *dst, const ob_dom_node_t *src)
{
    if (!dst || !src) return;
    dst->type = src->type;
    dst->parent = src->parent;
    dst->first_child = src->first_child;
    dst->next_sibling = src->next_sibling;
    dst->style_display = src->style_display;
    dst->font_weight_bold = src->font_weight_bold;
    ob_dom_copy_text(dst->name, sizeof(dst->name), src->name);
    ob_dom_copy_text(dst->text, sizeof(dst->text), src->text);
    ob_dom_copy_text(dst->href, sizeof(dst->href), src->href);
    ob_dom_copy_text(dst->img_src, sizeof(dst->img_src), src->img_src);
    ob_dom_copy_text(dst->img_alt, sizeof(dst->img_alt), src->img_alt);
    ob_dom_copy_text(dst->img_width, sizeof(dst->img_width), src->img_width);
    ob_dom_copy_text(dst->img_height, sizeof(dst->img_height), src->img_height);
    ob_dom_copy_text(dst->form_type, sizeof(dst->form_type), src->form_type);
    ob_dom_copy_text(dst->form_value, sizeof(dst->form_value), src->form_value);
    ob_dom_copy_text(dst->form_placeholder, sizeof(dst->form_placeholder), src->form_placeholder);
    ob_dom_copy_text(dst->form_name, sizeof(dst->form_name), src->form_name);
    ob_dom_copy_text(dst->form_action, sizeof(dst->form_action), src->form_action);
    ob_dom_copy_text(dst->form_method, sizeof(dst->form_method), src->form_method);
    dst->form_owner = src->form_owner;
}

static void ob_dom_document_copy(ob_dom_document_t *dst, const ob_dom_document_t *src)
{
    int i;
    if (!dst || !src) return;
    dst->count = src->count;
    dst->root = src->root;
    if (dst->count < 0) dst->count = 0;
    if (dst->count > OB_MAX_DOM_NODES) dst->count = OB_MAX_DOM_NODES;
    for (i = 0; i < dst->count; ++i) ob_dom_node_copy(&dst->nodes[i], &src->nodes[i]);
    for (; i < OB_MAX_DOM_NODES; ++i) {
        dst->nodes[i].type = 0;
        dst->nodes[i].parent = -1;
        dst->nodes[i].first_child = -1;
        dst->nodes[i].next_sibling = -1;
        dst->nodes[i].style_display = 0;
        dst->nodes[i].font_weight_bold = 0;
        dst->nodes[i].name[0] = 0;
        dst->nodes[i].text[0] = 0;
        dst->nodes[i].href[0] = 0;
        dst->nodes[i].img_src[0] = 0;
        dst->nodes[i].img_alt[0] = 0;
        dst->nodes[i].img_width[0] = 0;
        dst->nodes[i].img_height[0] = 0;
        dst->nodes[i].form_type[0] = 0;
        dst->nodes[i].form_value[0] = 0;
        dst->nodes[i].form_placeholder[0] = 0;
        dst->nodes[i].form_name[0] = 0;
        dst->nodes[i].form_action[0] = 0;
        dst->nodes[i].form_method[0] = 0;
        dst->nodes[i].form_owner = -1;
    }
}

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

typedef struct ob_form_control_state {
    int node_id;
    int editable;
    char name[OB_MAX_ATTR_VALUE];
    char value[OB_MAX_ATTR_VALUE];
} ob_form_control_state_t;

typedef struct ob_form_state {
    ob_form_control_state_t controls[OB_FORM_STATE_MAX_CONTROLS];
    int count;
    int focused;
} ob_form_state_t;

typedef struct ob_http_headers {
    char status_line[OB_MAX_HEADER_VALUE];
    char content_type[OB_MAX_HEADER_VALUE];
    char content_length[OB_MAX_HEADER_VALUE];
    char location[OB_MAX_HEADER_VALUE];
} ob_http_headers_t;

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
        if (pending_space && *pos > 0 && out[*pos - 1] != '\n' && out[*pos - 1] != ' ') {
            out[(*pos)++] = ' ';
            if (*pos >= out_size - 1) break;
        }
        pending_space = 0;
        out[(*pos)++] = c;
        ++p;
    }
    if (pending_space && *pos > 0 && *pos < out_size - 1 && out[*pos - 1] != '\n' && out[*pos - 1] != ' ') {
        out[(*pos)++] = ' ';
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
    out->attrs = 0;
    out->attrs_len = 0;
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
    if (s[self->pos] == '!') {
        if (s[self->pos + 1] == '-' && s[self->pos + 2] == '-') {
            self->pos += 3;
            while (s[self->pos] && !(s[self->pos] == '-' && s[self->pos + 1] == '-' && s[self->pos + 2] == '>')) ++self->pos;
            if (s[self->pos]) self->pos += 3;
        } else {
            ++self->pos;
            while (s[self->pos] && s[self->pos] != '>') ++self->pos;
            if (s[self->pos] == '>') ++self->pos;
        }
        return ob_tokenizer_next_impl(iface, out);
    }
    if (s[self->pos] == '/') { out->closing = 1; ++self->pos; }
    while (s[self->pos] == ' ' || s[self->pos] == '\t' || s[self->pos] == '\r' || s[self->pos] == '\n') ++self->pos;
    name_start = self->pos;
    while (s[self->pos] && s[self->pos] != '>' && s[self->pos] != '/' &&
           s[self->pos] != ' ' && s[self->pos] != '\t' && s[self->pos] != '\r' && s[self->pos] != '\n')
        ++self->pos;
    name_len = self->pos - name_start;
    out->attrs = s + self->pos;
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
    out->attrs_len = (int)((s + self->pos) - out->attrs);
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

static void ob_copy_attr_value(char *dst, int dst_size, const char *src, int len)
{
    int i = 0;
    if (!dst || dst_size <= 0) return;
    if (!src || len <= 0) { dst[0] = 0; return; }
    while (i < dst_size - 1 && i < len && src[i]) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static int ob_attr_name_eq_ci(const char *p, int len, const char *name)
{
    int i = 0;
    if (!p || !name || len <= 0) return 0;
    while (i < len && name[i]) {
        if (!ob_ascii_equal_ci(p[i], name[i])) return 0;
        ++i;
    }
    return i == len && name[i] == 0;
}

static void ob_extract_attr_value(const char *attrs, int attrs_len, const char *name, char *out, int out_size)
{
    int pos = 0;
    if (!out || out_size <= 0) return;
    out[0] = 0;
    if (!attrs || attrs_len <= 0 || !name) return;
    while (pos < attrs_len) {
        int key_start;
        int key_len;
        while (pos < attrs_len && (attrs[pos] == ' ' || attrs[pos] == '\t' || attrs[pos] == '\r' || attrs[pos] == '\n' || attrs[pos] == '/')) ++pos;
        key_start = pos;
        while (pos < attrs_len && attrs[pos] != '=' && attrs[pos] != ' ' && attrs[pos] != '\t' && attrs[pos] != '\r' && attrs[pos] != '\n' && attrs[pos] != '/') ++pos;
        key_len = pos - key_start;
        while (pos < attrs_len && (attrs[pos] == ' ' || attrs[pos] == '\t' || attrs[pos] == '\r' || attrs[pos] == '\n')) ++pos;
        if (pos >= attrs_len || attrs[pos] != '=') {
            while (pos < attrs_len && attrs[pos] != ' ' && attrs[pos] != '\t' && attrs[pos] != '\r' && attrs[pos] != '\n') ++pos;
            continue;
        }
        ++pos;
        while (pos < attrs_len && (attrs[pos] == ' ' || attrs[pos] == '\t' || attrs[pos] == '\r' || attrs[pos] == '\n')) ++pos;
        if (pos >= attrs_len) return;
        if (attrs[pos] == '"' || attrs[pos] == '\'') {
            char quote = attrs[pos++];
            int value_start = pos;
            while (pos < attrs_len && attrs[pos] != quote) ++pos;
            if (ob_attr_name_eq_ci(attrs + key_start, key_len, name)) {
                ob_copy_attr_value(out, out_size, attrs + value_start, pos - value_start);
                return;
            }
            if (pos < attrs_len) ++pos;
        } else {
            int value_start = pos;
            while (pos < attrs_len && attrs[pos] != ' ' && attrs[pos] != '\t' && attrs[pos] != '\r' && attrs[pos] != '\n' && attrs[pos] != '/') ++pos;
            if (ob_attr_name_eq_ci(attrs + key_start, key_len, name)) {
                ob_copy_attr_value(out, out_size, attrs + value_start, pos - value_start);
                return;
            }
        }
    }
}

static int ob_css_ident_eq_ci(const char *p, int len, const char *name)
{
    int i = 0;
    if (!p || !name || len <= 0) return 0;
    while (i < len && name[i]) {
        if (!ob_ascii_equal_ci(p[i], name[i])) return 0;
        ++i;
    }
    return i == len && name[i] == 0;
}

static void ob_css_apply_inline_style(ob_dom_node_t *node, const ob_html_token_t *tok)
{
    char style[OB_MAX_ATTR_VALUE];
    int pos = 0;
    if (!node || !tok) return;
    ob_extract_attr_value(tok->attrs, tok->attrs_len, "style", style, sizeof(style));
    while (style[pos]) {
        int key_start;
        int key_len;
        int value_start;
        int value_len;
        while (style[pos] == ' ' || style[pos] == '\t' || style[pos] == '\r' || style[pos] == '\n' || style[pos] == ';') ++pos;
        key_start = pos;
        while (style[pos] && style[pos] != ':' && style[pos] != ';') ++pos;
        key_len = pos - key_start;
        while (key_len > 0 && (style[key_start + key_len - 1] == ' ' || style[key_start + key_len - 1] == '\t' ||
                               style[key_start + key_len - 1] == '\r' || style[key_start + key_len - 1] == '\n')) --key_len;
        if (style[pos] != ':') {
            while (style[pos] && style[pos] != ';') ++pos;
            continue;
        }
        ++pos;
        while (style[pos] == ' ' || style[pos] == '\t' || style[pos] == '\r' || style[pos] == '\n') ++pos;
        value_start = pos;
        while (style[pos] && style[pos] != ';') ++pos;
        value_len = pos - value_start;
        while (value_len > 0 && (style[value_start + value_len - 1] == ' ' || style[value_start + value_len - 1] == '\t' ||
                                 style[value_start + value_len - 1] == '\r' || style[value_start + value_len - 1] == '\n')) --value_len;
        if (ob_css_ident_eq_ci(style + key_start, key_len, "display")) {
            if (ob_css_ident_eq_ci(style + value_start, value_len, "none")) node->style_display = OB_DISPLAY_NONE;
            else if (ob_css_ident_eq_ci(style + value_start, value_len, "block")) node->style_display = OB_DISPLAY_BLOCK;
            else if (ob_css_ident_eq_ci(style + value_start, value_len, "inline")) node->style_display = OB_DISPLAY_INLINE;
        } else if (ob_css_ident_eq_ci(style + key_start, key_len, "font-weight")) {
            if (ob_css_ident_eq_ci(style + value_start, value_len, "bold") ||
                ob_css_ident_eq_ci(style + value_start, value_len, "700") ||
                ob_css_ident_eq_ci(style + value_start, value_len, "800") ||
                ob_css_ident_eq_ci(style + value_start, value_len, "900"))
                node->font_weight_bold = 1;
        }
        if (style[pos] == ';') ++pos;
    }
}

static void ob_dom_extract_resource_attrs(ob_dom_node_t *node, const ob_html_token_t *tok)
{
    if (!node || !tok) return;
    if (ob_token_eq_ci(node->name, "img")) {
        ob_extract_attr_value(tok->attrs, tok->attrs_len, "src", node->img_src, sizeof(node->img_src));
        ob_extract_attr_value(tok->attrs, tok->attrs_len, "alt", node->img_alt, sizeof(node->img_alt));
        ob_extract_attr_value(tok->attrs, tok->attrs_len, "width", node->img_width, sizeof(node->img_width));
        ob_extract_attr_value(tok->attrs, tok->attrs_len, "height", node->img_height, sizeof(node->img_height));
    }
}

static void ob_dom_extract_form_attrs(ob_dom_node_t *node, const ob_html_token_t *tok)
{
    if (!node || !tok) return;
    if (ob_token_eq_ci(node->name, "form")) {
        ob_extract_attr_value(tok->attrs, tok->attrs_len, "action", node->form_action, sizeof(node->form_action));
        ob_extract_attr_value(tok->attrs, tok->attrs_len, "method", node->form_method, sizeof(node->form_method));
        if (!node->form_method[0]) ob_copy_attr_value(node->form_method, sizeof(node->form_method), "get", 3);
    } else if (ob_token_eq_ci(node->name, "input")) {
        ob_extract_attr_value(tok->attrs, tok->attrs_len, "type", node->form_type, sizeof(node->form_type));
        ob_extract_attr_value(tok->attrs, tok->attrs_len, "value", node->form_value, sizeof(node->form_value));
        ob_extract_attr_value(tok->attrs, tok->attrs_len, "placeholder", node->form_placeholder, sizeof(node->form_placeholder));
        ob_extract_attr_value(tok->attrs, tok->attrs_len, "name", node->form_name, sizeof(node->form_name));
        if (!node->form_type[0]) ob_copy_attr_value(node->form_type, sizeof(node->form_type), "text", 4);
    } else if (ob_token_eq_ci(node->name, "button") || ob_token_eq_ci(node->name, "textarea") ||
               ob_token_eq_ci(node->name, "select") || ob_token_eq_ci(node->name, "option")) {
        ob_extract_attr_value(tok->attrs, tok->attrs_len, "value", node->form_value, sizeof(node->form_value));
        ob_extract_attr_value(tok->attrs, tok->attrs_len, "name", node->form_name, sizeof(node->form_name));
        ob_extract_attr_value(tok->attrs, tok->attrs_len, "placeholder", node->form_placeholder, sizeof(node->form_placeholder));
    }
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
    doc->nodes[id].font_weight_bold = 0;
    doc->nodes[id].name[0] = 0;
    doc->nodes[id].text[0] = 0;
    doc->nodes[id].href[0] = 0;
    doc->nodes[id].form_type[0] = 0;
    doc->nodes[id].form_value[0] = 0;
    doc->nodes[id].form_placeholder[0] = 0;
    doc->nodes[id].form_name[0] = 0;
    doc->nodes[id].form_action[0] = 0;
    doc->nodes[id].form_method[0] = 0;
    doc->nodes[id].form_owner = -1;
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
                if (id >= 0 && ob_token_eq_ci(tok.name, "a"))
                    ob_extract_attr_value(tok.attrs, tok.attrs_len, "href", doc->nodes[id].href, sizeof(doc->nodes[id].href));
                if (id >= 0) {
                    int form_depth;
                    doc->nodes[id].form_owner = -1;
                    for (form_depth = depth - 1; form_depth > 0; --form_depth) {
                        int owner = stack[form_depth];
                        if (owner >= 0 && owner < doc->count && ob_token_eq_ci(doc->nodes[owner].name, "form")) {
                            doc->nodes[id].form_owner = owner;
                            break;
                        }
                    }
                }
                if (id >= 0) ob_css_apply_inline_style(&doc->nodes[id], &tok);
                if (id >= 0) {
                    ob_dom_extract_form_attrs(&doc->nodes[id], &tok);
                    ob_dom_extract_resource_attrs(&doc->nodes[id], &tok);
                }
                if (id >= 0 && ob_token_eq_ci(tok.name, "form")) doc->nodes[id].form_owner = id;
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

static void ob_dom_render_append_int(char *out, int out_size, int *pos, int value)
{
    char digits[12];
    int count = 0;
    if (!out || !pos || *pos >= out_size - 1) return;
    if (value <= 0) {
        if (*pos < out_size - 1) out[(*pos)++] = '0';
        return;
    }
    while (value > 0 && count < (int)sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (count > 0 && *pos < out_size - 1) out[(*pos)++] = digits[--count];
}

static void ob_dom_render_append_link_marker(char *out, int out_size, int *pos, int link_number, const char *href)
{
    if (!out || !pos || link_number <= 0 || *pos >= out_size - 1) return;
    if (*pos > 0 && out[*pos - 1] != ' ' && out[*pos - 1] != '\n') out[(*pos)++] = ' ';
    if (*pos < out_size - 1) out[(*pos)++] = '[';
    ob_dom_render_append_int(out, out_size, pos, link_number);
    if (*pos < out_size - 1) out[(*pos)++] = ']';
    if (href && href[0]) {
        int i = 0;
        if (*pos < out_size - 1) out[(*pos)++] = ' ';
        while (href[i] && *pos < out_size - 1) out[(*pos)++] = href[i++];
    }
    if (*pos < out_size - 1) out[(*pos)++] = ' ';
}

static void ob_dom_render_append_literal(char *out, int out_size, int *pos, const char *text)
{
    int i = 0;
    if (!out || !pos || !text) return;
    while (text[i] && *pos < out_size - 1) out[(*pos)++] = text[i++];
}

static int ob_dom_is_editable_form_control(const ob_dom_node_t *node)
{
    if (!node || node->type != OB_DOM_NODE_ELEMENT) return 0;
    if (ob_token_eq_ci(node->name, "textarea")) return 1;
    if (ob_token_eq_ci(node->name, "input")) {
        if (!node->form_type[0]) return 1;
        return ob_token_eq_ci(node->form_type, "text") || ob_token_eq_ci(node->form_type, "search") ||
               ob_token_eq_ci(node->form_type, "password") || ob_token_eq_ci(node->form_type, "email") ||
               ob_token_eq_ci(node->form_type, "url") || ob_token_eq_ci(node->form_type, "number");
    }
    return 0;
}

static int ob_dom_is_submit_form_control(const ob_dom_node_t *node)
{
    if (!node || node->type != OB_DOM_NODE_ELEMENT) return 0;
    if (ob_token_eq_ci(node->name, "button")) return 1;
    if (ob_token_eq_ci(node->name, "input"))
        return ob_token_eq_ci(node->form_type, "submit") || ob_token_eq_ci(node->form_type, "button");
    return 0;
}

static int ob_dom_is_form_control(const ob_dom_node_t *node)
{
    if (!node || node->type != OB_DOM_NODE_ELEMENT) return 0;
    return ob_token_eq_ci(node->name, "input") || ob_token_eq_ci(node->name, "button") ||
           ob_token_eq_ci(node->name, "textarea") || ob_token_eq_ci(node->name, "select") ||
           ob_token_eq_ci(node->name, "option");
}

static void ob_form_state_init(ob_form_state_t *state)
{
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->focused = -1;
}

static const char *ob_form_state_value_for_node(const ob_form_state_t *state, int node_id)
{
    int i;
    if (!state) return 0;
    for (i = 0; i < state->count; ++i) {
        if (state->controls[i].node_id == node_id) return state->controls[i].value;
    }
    return 0;
}

static int ob_form_state_is_focused(const ob_form_state_t *state, int node_id)
{
    if (!state || state->focused < 0 || state->focused >= state->count) return 0;
    return state->controls[state->focused].node_id == node_id;
}

static const char *ob_dom_first_child_text(const ob_dom_document_t *doc, int parent_id)
{
    int i;
    if (!doc || parent_id < 0) return "";
    for (i = 0; i < doc->count; ++i) {
        if (doc->nodes[i].parent == parent_id && doc->nodes[i].type == OB_DOM_NODE_TEXT) return doc->nodes[i].text;
    }
    return "";
}

static int ob_form_state_collect_from_dom(ob_form_state_t *state, const ob_dom_document_t *doc)
{
    int i;
    if (!state || !doc) return -1;
    ob_form_state_init(state);
    for (i = 0; i < doc->count && state->count < OB_FORM_STATE_MAX_CONTROLS; ++i) {
        const ob_dom_node_t *node = &doc->nodes[i];
        if (!ob_dom_is_editable_form_control(node) && !ob_dom_is_submit_form_control(node)) continue;
        state->controls[state->count].node_id = i;
        state->controls[state->count].editable = ob_dom_is_editable_form_control(node);
        ob_copy_attr_value(state->controls[state->count].name, sizeof(state->controls[state->count].name),
                           node->form_name[0] ? node->form_name : node->name,
                           ob_cstr_len(node->form_name[0] ? node->form_name : node->name));
        {
            const char *initial_value = node->form_value;
            if (ob_token_eq_ci(node->name, "textarea") && !initial_value[0])
                initial_value = ob_dom_first_child_text(doc, i);
            ob_copy_attr_value(state->controls[state->count].value, sizeof(state->controls[state->count].value),
                               initial_value, ob_cstr_len(initial_value));
        }
        ++state->count;
    }
    if (state->count > 0) state->focused = 0;
    return state->count;
}

static int ob_form_state_focus_next(ob_form_state_t *state)
{
    if (!state || state->count <= 0) return -1;
    state->focused = (state->focused + 1) % state->count;
    return state->focused;
}

static int ob_form_state_handle_key(ob_form_state_t *state, unsigned int key)
{
    char *value;
    int len;
    if (!state || state->focused < 0 || state->focused >= state->count) return 0;
    if (!state->controls[state->focused].editable) return 0;
    value = state->controls[state->focused].value;
    len = ob_cstr_len(value);
    if (key == 8u || key == 127u) {
        if (len > 0) value[len - 1] = 0;
        return 1;
    }
    if (key == 27u) {
        value[0] = 0;
        return 1;
    }
    if (key >= 32u && key <= 126u && len < OB_MAX_ATTR_VALUE - 1) {
        value[len] = (char)key;
        value[len + 1] = 0;
        return 1;
    }
    return 0;
}

static void ob_dom_render_append_attr_label(char *out, int out_size, int *pos, const char *label, const char *value)
{
    if (!value || !value[0]) return;
    ob_dom_render_append_literal(out, out_size, pos, " ");
    ob_dom_render_append_literal(out, out_size, pos, label);
    ob_dom_render_append_literal(out, out_size, pos, "=\"");
    ob_dom_render_append_literal(out, out_size, pos, value);
    ob_dom_render_append_literal(out, out_size, pos, "\"");
}

static void ob_dom_render_append_form_start_ex(char *out, int out_size, int *pos, const ob_dom_node_t *node, int node_id, const ob_form_state_t *form_state)
{
    const char *value;
    if (!ob_dom_is_form_control(node)) return;
    if (*pos > 0 && out[*pos - 1] != ' ' && out[*pos - 1] != '\n') ob_dom_render_append_literal(out, out_size, pos, " ");
    ob_dom_render_append_literal(out, out_size, pos, ob_form_state_is_focused(form_state, node_id) ? "[*" : "[");
    ob_dom_render_append_literal(out, out_size, pos, node->name);
    if (ob_token_eq_ci(node->name, "input")) ob_dom_render_append_attr_label(out, out_size, pos, "type", node->form_type[0] ? node->form_type : "text");
    ob_dom_render_append_attr_label(out, out_size, pos, "name", node->form_name);
    value = ob_form_state_value_for_node(form_state, node_id);
    ob_dom_render_append_attr_label(out, out_size, pos, "value", value ? value : node->form_value);
    ob_dom_render_append_attr_label(out, out_size, pos, "placeholder", node->form_placeholder);
}

static void ob_dom_render_append_form_end(char *out, int out_size, int *pos, const ob_dom_node_t *node)
{
    if (!ob_dom_is_form_control(node)) return;
    ob_dom_render_append_literal(out, out_size, pos, "]");
}

static int ob_dom_heading_level(const ob_dom_node_t *node)
{
    if (!node || node->type != OB_DOM_NODE_ELEMENT) return 0;
    if (ob_token_eq_ci(node->name, "h1")) return 1;
    if (ob_token_eq_ci(node->name, "h2")) return 2;
    if (ob_token_eq_ci(node->name, "h3")) return 3;
    return 0;
}

static void ob_dom_render_append_heading_prefix(char *out, int out_size, int *pos, int level)
{
    int i;
    if (!out || !pos || level <= 0) return;
    if (*pos > 0 && out[*pos - 1] != '\n') ob_dom_render_append_newline(out, out_size, pos);
    for (i = 0; i < level && *pos < out_size - 1; ++i) out[(*pos)++] = '#';
    if (*pos < out_size - 1) out[(*pos)++] = ' ';
}

static int ob_dom_is_hidden_content_node(const ob_dom_node_t *node)
{
    if (!node || node->type != OB_DOM_NODE_ELEMENT) return 0;
    return ob_token_eq_ci(node->name, "head") ||
           ob_token_eq_ci(node->name, "style") ||
           ob_token_eq_ci(node->name, "script") ||
           ob_token_eq_ci(node->name, "title") ||
           ob_token_eq_ci(node->name, "meta") ||
           ob_token_eq_ci(node->name, "link");
}

static void ob_dom_render_node_text_ex(const ob_dom_document_t *doc, int node_id, char *out, int out_size, int *pos, int *link_count, const ob_form_state_t *form_state)
{
    int child;
    int is_block;
    int heading_level;
    int is_list_item;
    if (!doc || node_id < 0 || node_id >= doc->count || !out || !pos || *pos >= out_size - 1) return;
    if (doc->nodes[node_id].style_display == OB_DISPLAY_NONE || ob_dom_is_hidden_content_node(&doc->nodes[node_id])) return;
    is_block = doc->nodes[node_id].type == OB_DOM_NODE_ELEMENT && doc->nodes[node_id].style_display == OB_DISPLAY_BLOCK;
    heading_level = ob_dom_heading_level(&doc->nodes[node_id]);
    is_list_item = doc->nodes[node_id].type == OB_DOM_NODE_ELEMENT && ob_token_eq_ci(doc->nodes[node_id].name, "li");
    if (is_block && *pos > 0) ob_dom_render_append_newline(out, out_size, pos);
    if (heading_level > 0) ob_dom_render_append_heading_prefix(out, out_size, pos, heading_level);
    if (is_list_item) ob_dom_render_append_literal(out, out_size, pos, "- ");
    if (doc->nodes[node_id].font_weight_bold) ob_dom_render_append_literal(out, out_size, pos, "**");
    if (ob_dom_is_form_control(&doc->nodes[node_id])) ob_dom_render_append_form_start_ex(out, out_size, pos, &doc->nodes[node_id], node_id, form_state);
    if (doc->nodes[node_id].type == OB_DOM_NODE_ELEMENT && ob_token_eq_ci(doc->nodes[node_id].name, "img")) {
        if (*pos > 0 && out[*pos - 1] != ' ' && out[*pos - 1] != '\n') ob_dom_render_append_literal(out, out_size, pos, " ");
        ob_dom_render_append_literal(out, out_size, pos, "[Image");
        if (doc->nodes[node_id].img_alt[0]) { ob_dom_render_append_literal(out, out_size, pos, ": "); ob_dom_render_append_literal(out, out_size, pos, doc->nodes[node_id].img_alt); }
        if (doc->nodes[node_id].img_src[0]) { ob_dom_render_append_literal(out, out_size, pos, " src=\""); ob_dom_render_append_literal(out, out_size, pos, doc->nodes[node_id].img_src); ob_dom_render_append_literal(out, out_size, pos, "\""); }
        if (doc->nodes[node_id].img_width[0] || doc->nodes[node_id].img_height[0]) {
            ob_dom_render_append_literal(out, out_size, pos, " size=\"");
            ob_dom_render_append_literal(out, out_size, pos, doc->nodes[node_id].img_width[0] ? doc->nodes[node_id].img_width : "?");
            ob_dom_render_append_literal(out, out_size, pos, "x");
            ob_dom_render_append_literal(out, out_size, pos, doc->nodes[node_id].img_height[0] ? doc->nodes[node_id].img_height : "?");
            ob_dom_render_append_literal(out, out_size, pos, "\"");
        }
        ob_dom_render_append_literal(out, out_size, pos, "]");
    }
    if (doc->nodes[node_id].type == OB_DOM_NODE_TEXT) ob_dom_render_append_text(out, out_size, pos, doc->nodes[node_id].text);
    child = doc->nodes[node_id].first_child;
    while (child >= 0 && *pos < out_size - 1) {
        ob_dom_render_node_text_ex(doc, child, out, out_size, pos, link_count, form_state);
        child = doc->nodes[child].next_sibling;
    }
    if (ob_dom_is_form_control(&doc->nodes[node_id])) ob_dom_render_append_form_end(out, out_size, pos, &doc->nodes[node_id]);
    if (doc->nodes[node_id].font_weight_bold) ob_dom_render_append_literal(out, out_size, pos, "**");
    if (doc->nodes[node_id].type == OB_DOM_NODE_ELEMENT && ob_token_eq_ci(doc->nodes[node_id].name, "a") && doc->nodes[node_id].href[0] && link_count) {
        ++(*link_count);
        ob_dom_render_append_link_marker(out, out_size, pos, *link_count, doc->nodes[node_id].href);
    }
    if (is_block && *pos > 0) ob_dom_render_append_newline(out, out_size, pos);
}

static void ob_dom_render_node_text_with_form_state(const ob_dom_document_t *doc, int node_id, char *out, int out_size, int *pos, const ob_form_state_t *form_state)
{
    int link_count = 0;
    ob_dom_render_node_text_ex(doc, node_id, out, out_size, pos, &link_count, form_state);
}

static int ob_form_url_append_char(char *out, int out_size, int *pos, char ch)
{
    if (!out || !pos || out_size <= 0 || *pos >= out_size - 1) return -1;
    out[(*pos)++] = ch;
    out[*pos] = 0;
    return 0;
}

static int ob_form_url_append_text(char *out, int out_size, int *pos, const char *text)
{
    int i = 0;
    if (!text) text = "";
    while (text[i]) {
        if (ob_form_url_append_char(out, out_size, pos, text[i++]) < 0) return -1;
    }
    return 0;
}

static int ob_form_url_append_encoded(char *out, int out_size, int *pos, const char *text)
{
    static const char hex[] = "0123456789ABCDEF";
    int i = 0;
    if (!text) text = "";
    while (text[i]) {
        unsigned char ch = (unsigned char)text[i++];
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            if (ob_form_url_append_char(out, out_size, pos, (char)ch) < 0) return -1;
        } else if (ch == ' ') {
            if (ob_form_url_append_char(out, out_size, pos, '+') < 0) return -1;
        } else {
            if (ob_form_url_append_char(out, out_size, pos, '%') < 0) return -1;
            if (ob_form_url_append_char(out, out_size, pos, hex[(ch >> 4) & 15]) < 0) return -1;
            if (ob_form_url_append_char(out, out_size, pos, hex[ch & 15]) < 0) return -1;
        }
    }
    return 0;
}

static int ob_form_find_owner_for_node(const ob_dom_document_t *doc, int node_id)
{
    if (!doc || node_id < 0 || node_id >= doc->count) return -1;
    return doc->nodes[node_id].form_owner;
}

static int ob_form_build_get_url(const ob_dom_document_t *doc, const ob_form_state_t *state,
                                 int submit_node_id, const char *base_url, char *out, int out_size)
{
    int owner;
    int i;
    int pos = 0;
    int has_query = 0;
    const char *action = "";
    if (!doc || !out || out_size <= 0) return -1;
    out[0] = 0;
    owner = ob_form_find_owner_for_node(doc, submit_node_id);
    if (owner >= 0 && owner < doc->count) {
        if (doc->nodes[owner].form_method[0] && !ob_token_eq_ci(doc->nodes[owner].form_method, "get")) return -2;
        action = doc->nodes[owner].form_action;
    }
    if (!action || !action[0]) action = base_url ? base_url : "";
    if (ob_form_url_append_text(out, out_size, &pos, action) < 0) return -1;
    for (i = 0; i < pos; ++i) {
        if (out[i] == '?') has_query = 1;
    }
    if (state) {
        for (i = 0; i < state->count; ++i) {
            int cid = state->controls[i].node_id;
            if (cid < 0 || cid >= doc->count) continue;
            if (owner >= 0 && doc->nodes[cid].form_owner != owner) continue;
            if (!state->controls[i].editable) continue;
            if (!state->controls[i].name[0]) continue;
            if (ob_form_url_append_char(out, out_size, &pos, has_query ? '&' : '?') < 0) return -1;
            has_query = 1;
            if (ob_form_url_append_encoded(out, out_size, &pos, state->controls[i].name) < 0) return -1;
            if (ob_form_url_append_char(out, out_size, &pos, '=') < 0) return -1;
            if (ob_form_url_append_encoded(out, out_size, &pos, state->controls[i].value) < 0) return -1;
        }
    }
    return pos;
}

static int ob_dom_text_render_with_form_state(const ob_dom_document_t *doc, const ob_form_state_t *form_state, char *out, int out_size)
{
    int pos = 0;
    if (!out || out_size <= 0) return -1;
    out[0] = 0;
    if (!doc || doc->root < 0 || doc->root >= doc->count) return -1;
    ob_dom_render_node_text_with_form_state(doc, doc->root, out, out_size, &pos, form_state);
    while (pos > 0 && (out[pos - 1] == ' ' || out[pos - 1] == '\n')) --pos;
    out[pos] = 0;
    return pos;
}

static int ob_dom_text_render_impl(ob_dom_text_renderer_i_t *iface, const ob_dom_document_t *doc, char *out, int out_size)
{
    (void)iface;
    return ob_dom_text_render_with_form_state(doc, 0, out, out_size);
}

static void ob_dom_text_renderer_base_init(ob_dom_text_renderer_base_t *renderer)
{
    if (!renderer) return;
    renderer->iface.render = ob_dom_text_render_impl;
}

static void ob_url_split_path_suffix(const char *input, int *path_len, const char **suffix)
{
    const char *p = input ? input : "";
    int len = 0;
    while (p[len] && p[len] != '?' && p[len] != '#') ++len;
    if (path_len) *path_len = len;
    if (suffix) *suffix = p + len;
}

static int ob_url_append_segment(char *out, int out_size, int *written, const char *segment, int seg_len)
{
    if (!out || !written || out_size <= 0 || seg_len <= 0) return 0;
    if (*written > 1 && out[*written - 1] != '/') {
        if (*written >= out_size - 1) return -1;
        out[(*written)++] = '/';
    }
    if (*written == 0) {
        if (*written >= out_size - 1) return -1;
        out[(*written)++] = '/';
    }
    if (*written + seg_len >= out_size) return -1;
    memcpy(out + *written, segment, seg_len);
    *written += seg_len;
    out[*written] = 0;
    return 0;
}

static void ob_url_normalize_path(char *out, int out_size, const char *raw_path, int raw_len, const char *suffix)
{
    int written = 0;
    int i = 0;
    if (!out || out_size <= 0) return;
    out[0] = 0;
    if (!raw_path || raw_len <= 0) {
        raw_path = "/";
        raw_len = 1;
    }
    if (raw_path[0] == '/') out[written++] = '/';
    while (i < raw_len) {
        int start;
        int len;
        while (i < raw_len && raw_path[i] == '/') ++i;
        start = i;
        while (i < raw_len && raw_path[i] != '/') ++i;
        len = i - start;
        if (len == 0 || (len == 1 && raw_path[start] == '.')) continue;
        if (len == 2 && raw_path[start] == '.' && raw_path[start + 1] == '.') {
            if (written > 1) {
                if (out[written - 1] == '/') --written;
                while (written > 1 && out[written - 1] != '/') --written;
                out[written] = 0;
            }
            continue;
        }
        if (ob_url_append_segment(out, out_size, &written, raw_path + start, len) < 0) break;
    }
    if (written == 0) out[written++] = '/';
    out[written] = 0;
    if (suffix && suffix[0] && written < out_size - 1) snprintf(out + written, out_size - written, "%s", suffix);
}

static void ob_url_join_relative_path(char *out, int out_size, const char *base_path, const char *href)
{
    const char *base = base_path && base_path[0] ? base_path : "/";
    const char *slash = 0;
    const char *suffix = "";
    char combined[512];
    int base_path_len;
    int href_path_len;
    int base_len;
    if (!out || out_size <= 0) return;
    out[0] = 0;
    ob_url_split_path_suffix(base, &base_path_len, 0);
    if (!href || !href[0]) {
        ob_url_normalize_path(out, out_size, base, base_path_len, base + base_path_len);
        return;
    }
    ob_url_split_path_suffix(href, &href_path_len, &suffix);
    if (href[0] == '?' || href[0] == '#') {
        ob_url_normalize_path(out, out_size, base, base_path_len, href);
        return;
    }
    if (href[0] == '/') {
        ob_url_normalize_path(out, out_size, href, href_path_len, suffix);
        return;
    }
    if (base_path_len > 0) {
        const char *p = base;
        const char *end = base + base_path_len;
        while (p < end) {
            if (*p == '/') slash = p;
            ++p;
        }
    }
    base_len = slash ? (int)(slash - base + 1) : 1;
    if (base_len > 1) snprintf(combined, sizeof(combined), "%.*s%.*s", base_len, base, href_path_len, href);
    else snprintf(combined, sizeof(combined), "/%.*s", href_path_len, href);
    ob_url_normalize_path(out, out_size, combined, (int)strlen(combined), suffix);
}

typedef struct ob_url_parts {
    char host[128];
    char path[256];
    int is_file;
    int is_https;
} ob_url_parts_t;

static int ob_ascii_match_ci(const char *p, const char *token)
{
    int i = 0;
    if (!p || !token) return 0;
    while (token[i]) {
        char a = p[i];
        char b = token[i];
        if (!a) return 0;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
        ++i;
    }
    return 1;
}

static int ob_url_parse_address(const char *text, const char *default_host, ob_url_parts_t *out, char *error, int error_size)
{
    const char *p = text;
    const char *slash;
    int host_len;
    if (error && error_size > 0) error[0] = 0;
    if (!text || !out) return -1;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (!*p) {
        if (error && error_size > 0) snprintf(error, error_size, "empty address");
        return -1;
    }
    memset(out, 0, sizeof(*out));
    if (ob_ascii_match_ci(p, "https://")) {
        out->is_https = 1;
        p += 8;
    }
    if (ob_ascii_match_ci(p, "file://")) {
        p += 7;
        if (!*p) {
            if (error && error_size > 0) snprintf(error, error_size, "empty file path");
            return -1;
        }
        out->is_file = 1;
        snprintf(out->path, sizeof(out->path), "%s", p);
        return 0;
    }
    if (p[0] == '/') {
        if (default_host && default_host[0]) {
            out->is_file = 0;
            snprintf(out->host, sizeof(out->host), "%s", default_host);
            ob_url_join_relative_path(out->path, sizeof(out->path), "/", p);
        } else {
            out->is_file = 1;
            snprintf(out->path, sizeof(out->path), "%s", p);
        }
        return 0;
    }
    if (!out->is_https && ob_ascii_match_ci(p, "http://")) p += 7;
    slash = strchr(p, '/');
    host_len = slash ? (int)(slash - p) : (int)strlen(p);
    while (host_len > 0 && (p[host_len - 1] == ' ' || p[host_len - 1] == '\t' || p[host_len - 1] == '\r' || p[host_len - 1] == '\n')) --host_len;
    if (host_len <= 0 || host_len >= (int)sizeof(out->host)) {
        if (error && error_size > 0) snprintf(error, error_size, "invalid host");
        return -1;
    }
    out->is_file = 0;
    memcpy(out->host, p, host_len);
    out->host[host_len] = 0;
    if (slash && *slash) ob_url_join_relative_path(out->path, sizeof(out->path), "/", slash);
    else snprintf(out->path, sizeof(out->path), "/");
    return 0;
}

static void ob_dom_normalize_resource_urls(ob_dom_document_t *doc, const char *base_path)
{
    int i;
    if (!doc) return;
    for (i = 0; i < doc->count; ++i) {
        if (doc->nodes[i].type == OB_DOM_NODE_ELEMENT && ob_token_eq_ci(doc->nodes[i].name, "img") && doc->nodes[i].img_src[0] &&
            !ob_ascii_match_ci(doc->nodes[i].img_src, "http://") && !ob_ascii_match_ci(doc->nodes[i].img_src, "https://") &&
            !ob_ascii_match_ci(doc->nodes[i].img_src, "file://")) {
            char normalized[OB_MAX_ATTR_VALUE];
            ob_url_join_relative_path(normalized, sizeof(normalized), base_path && base_path[0] ? base_path : "/", doc->nodes[i].img_src);
            ob_dom_copy_text(doc->nodes[i].img_src, sizeof(doc->nodes[i].img_src), normalized);
        }
    }
}

static void ob_http_headers_init(ob_http_headers_t *headers)
{
    if (headers) memset(headers, 0, sizeof(*headers));
}

static int ob_http_header_name_eq_ci(const char *p, int len, const char *name)
{
    int i = 0;
    if (!p || !name || len <= 0) return 0;
    while (i < len && name[i]) {
        if (!ob_ascii_equal_ci(p[i], name[i])) return 0;
        ++i;
    }
    return i == len && name[i] == 0;
}

static void ob_http_copy_trimmed(char *dst, int dst_size, const char *src, int len)
{
    int start = 0;
    int end = len;
    int i;
    if (!dst || dst_size <= 0) return;
    dst[0] = 0;
    if (!src || len <= 0) return;
    while (start < end && (src[start] == ' ' || src[start] == '\t' || src[start] == '\r' || src[start] == '\n')) ++start;
    while (end > start && (src[end - 1] == ' ' || src[end - 1] == '\t' || src[end - 1] == '\r' || src[end - 1] == '\n')) --end;
    for (i = 0; i < dst_size - 1 && start + i < end; ++i) dst[i] = src[start + i];
    dst[i] = 0;
}

static int ob_http_parse_headers(const char *response, ob_http_headers_t *headers)
{
    int pos = 0;
    if (!response || !headers) return -1;
    ob_http_headers_init(headers);
    while (response[pos] && response[pos] != '\r' && response[pos] != '\n' && pos < OB_MAX_HEADER_VALUE - 1) {
        headers->status_line[pos] = response[pos];
        ++pos;
    }
    headers->status_line[pos] = 0;
    while (response[pos] == '\r' || response[pos] == '\n') ++pos;
    while (response[pos]) {
        int line_start = pos;
        int line_end;
        int colon = -1;
        int i;
        while (response[pos] && response[pos] != '\r' && response[pos] != '\n') ++pos;
        line_end = pos;
        if (line_end == line_start) break;
        for (i = line_start; i < line_end; ++i) {
            if (response[i] == ':') { colon = i; break; }
        }
        if (colon > line_start) {
            int name_len = colon - line_start;
            const char *value = response + colon + 1;
            int value_len = line_end - colon - 1;
            if (ob_http_header_name_eq_ci(response + line_start, name_len, "Content-Type"))
                ob_http_copy_trimmed(headers->content_type, sizeof(headers->content_type), value, value_len);
            else if (ob_http_header_name_eq_ci(response + line_start, name_len, "Content-Length"))
                ob_http_copy_trimmed(headers->content_length, sizeof(headers->content_length), value, value_len);
            else if (ob_http_header_name_eq_ci(response + line_start, name_len, "Location"))
                ob_http_copy_trimmed(headers->location, sizeof(headers->location), value, value_len);
        }
        while (response[pos] == '\r' || response[pos] == '\n') ++pos;
    }
    return headers->status_line[0] ? 0 : -1;
}

static int ob_http_content_is_renderable_html(const char *content_type)
{
    if (!content_type || !content_type[0]) return 1;
    return ob_ascii_match_ci(content_type, "text/html") || ob_ascii_match_ci(content_type, "application/xhtml") ||
           ob_ascii_match_ci(content_type, "text/plain");
}


#endif /* OPENOS_USER_BROWSER_ENGINE_H */
