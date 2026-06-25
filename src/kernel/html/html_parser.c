#include "html_parser.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

// 自闭合标签列表
static const char* void_tags[] = {
    "area", "base", "br", "col", "embed", "hr", "img", "input",
    "link", "meta", "param", "source", "track", "wbr", NULL
};

static int is_void_tag(const char* tag) {
    for (int i = 0; void_tags[i]; i++) {
        if (strcasecmp(tag, void_tags[i]) == 0) return 1;
    }
    return 0;
}

static void push_tag(html_parser_t* parser, dom_node_t* node) {
    if (parser->tag_stack_size >= parser->tag_stack_capacity) {
        parser->tag_stack_capacity = (parser->tag_stack_capacity == 0) ? 32 : 
                                      parser->tag_stack_capacity * 2;
        parser->tag_stack = realloc(parser->tag_stack, 
                                     parser->tag_stack_capacity * sizeof(dom_node_t*));
    }
    parser->tag_stack[parser->tag_stack_size++] = node;
    parser->current_node = node;
}

static dom_node_t* pop_tag(html_parser_t* parser) {
    if (parser->tag_stack_size == 0) return NULL;
    
    dom_node_t* node = parser->tag_stack[--parser->tag_stack_size];
    parser->current_node = (parser->tag_stack_size > 0) ? 
                           parser->tag_stack[parser->tag_stack_size - 1] :
                           (dom_node_t*)parser->document;
    return node;
}

static dom_node_t* find_open_tag(html_parser_t* parser, const char* tag_name) {
    for (size_t i = parser->tag_stack_size; i > 0; i--) {
        dom_node_t* node = parser->tag_stack[i - 1];
        if (node->type == DOM_ELEMENT_NODE &&
            strcasecmp(node->data.element.tag_name, tag_name) == 0) {
            return node;
        }
    }
    return NULL;
}

static void close_tag(html_parser_t* parser, const char* tag_name) {
    // 弹出直到找到匹配的标签
    while (parser->tag_stack_size > 0) {
        dom_node_t* top = parser->tag_stack[parser->tag_stack_size - 1];
        if (strcasecmp(top->data.element.tag_name, tag_name) == 0) {
            pop_tag(parser);
            return;
        }
        pop_tag(parser);
    }
}

int html_parser_init(html_parser_t* parser, const char* html, size_t len) {
    memset(parser, 0, sizeof(html_parser_t));
    
    html_lexer_init(&parser->lexer, html, len);
    
    parser->document = dom_document_create();
    if (!parser->document) return -1;
    
    parser->current_node = (dom_node_t*)parser->document;
    parser->tag_stack = NULL;
    parser->tag_stack_size = 0;
    parser->tag_stack_capacity = 0;
    
    return 0;
}

void html_parser_destroy(html_parser_t* parser) {
    if (!parser) return;
    
    if (parser->tag_stack) free(parser->tag_stack);
    memset(parser, 0, sizeof(html_parser_t));
}

static void parse_start_tag(html_parser_t* parser, html_token_t* token) {
    char tag_name[64];
    html_token_copy(token, tag_name, sizeof(tag_name));
    
    dom_node_t* element = dom_element_create(parser->document, tag_name);
    if (!element) return;
    
    // 特殊处理 html/head/body 标签
    if (strcasecmp(tag_name, "html") == 0) {
        parser->document->document_element = element;
    } else if (strcasecmp(tag_name, "head") == 0) {
        parser->document->head = element;
    } else if (strcasecmp(tag_name, "body") == 0) {
        parser->document->body = element;
    }
    
    dom_node_append_child(parser->current_node, element);
    
    if (!is_void_tag(tag_name)) {
        push_tag(parser, element);
    }
}

static void parse_end_tag(html_parser_t* parser, html_token_t* token) {
    char tag_name[64];
    html_token_copy(token, tag_name, sizeof(tag_name));
    
    // 特殊处理：p 标签会隐式关闭前面的 p 标签
    if (strcasecmp(tag_name, "p") == 0) {
        close_tag(parser, "p");
        return;
    }
    
    close_tag(parser, tag_name);
}

static void parse_attributes(html_parser_t* parser, dom_node_t* element) {
    html_token_t token;
    char attr_name[128];
    char attr_value[1024];
    
    while (1) {
        int ret = html_lexer_next(&parser->lexer, &token);
        
        if (token.type == HTML_TOKEN_TAG_END || 
            token.type == HTML_TOKEN_TAG_SELF_CLOSE) {
            break;
        }
        
        if (token.type == HTML_TOKEN_ATTR_NAME) {
            html_token_copy(&token, attr_name, sizeof(attr_name));
            
            // 检查是否有值
            ret = html_lexer_next(&parser->lexer, &token);
            if (token.type == HTML_TOKEN_ATTR_VALUE) {
                html_token_copy(&token, attr_value, sizeof(attr_value));
                dom_element_set_attribute(element, attr_name, attr_value);
            } else {
                // 无值属性
                dom_element_set_attribute(element, attr_name, "");
                // 回退
                html_lexer_rewind(&parser->lexer);
            }
        }
    }
}

dom_document_t* html_parser_parse(html_parser_t* parser) {
    if (!parser || !parser->document) return NULL;
    
    html_token_t token;
    int ret;
    
    while ((ret = html_lexer_next(&parser->lexer, &token)) >= 0) {
        switch (token.type) {
            case HTML_TOKEN_TAG_OPEN: {
                char tag_name[64];
                html_token_copy(&token, tag_name, sizeof(tag_name));
                
                dom_node_t* element = dom_element_create(parser->document, tag_name);
                if (element) {
                    // 特殊处理 html/head/body
                    if (strcasecmp(tag_name, "html") == 0) {
                        parser->document->document_element = element;
                    } else if (strcasecmp(tag_name, "head") == 0) {
                        parser->document->head = element;
                    } else if (strcasecmp(tag_name, "body") == 0) {
                        parser->document->body = element;
                    }
                    
                    dom_node_append_child(parser->current_node, element);
                    
                    // 解析属性
                    parse_attributes(parser, element);
                    
                    // 检查是否自闭合
                    if (!is_void_tag(tag_name)) {
                        push_tag(parser, element);
                    }
                }
                break;
            }
            
            case HTML_TOKEN_TAG_CLOSE: {
                char tag_name[64];
                html_token_copy(&token, tag_name, sizeof(tag_name));
                close_tag(parser, tag_name);
                
                // 跳过剩余部分直到 >
                while (1) {
                    ret = html_lexer_next(&parser->lexer, &token);
                    if (token.type == HTML_TOKEN_TAG_END || ret < 0) break;
                }
                break;
            }
            
            case HTML_TOKEN_TEXT: {
                if (token.length > 0) {
                    char text[4096];
                    html_token_copy(&token, text, sizeof(text));
                    
                    // 简单的实体解码
                    // TODO: 完整的实体解码支持
                    char* p = text;
                    while (*p) {
                        if (*p == '\r' || *p == '\n' || *p == '\t') {
                            *p = ' ';
                        }
                        p++;
                    }
                    
                    // 压缩空格
                    char* dest = text;
                    int in_space = 0;
                    for (p = text; *p; p++) {
                        if (*p == ' ') {
                            if (!in_space) {
                                *dest++ = *p;
                                in_space = 1;
                            }
                        } else {
                            *dest++ = *p;
                            in_space = 0;
                        }
                    }
                    *dest = '\0';
                    
                    if (strlen(text) > 0) {
                        dom_node_t* text_node = dom_text_create(parser->document, text);
                        if (text_node) {
                            dom_node_append_child(parser->current_node, text_node);
                        }
                    }
                }
                break;
            }
            
            case HTML_TOKEN_COMMENT:
            case HTML_TOKEN_DOCTYPE:
                // 忽略注释和DOCTYPE
                break;
                
            default:
                break;
        }
    }
    
    return parser->document;
}

dom_document_t* html_parse(const char* html, size_t len) {
    html_parser_t parser;
    if (html_parser_init(&parser, html, len) != 0) {
        return NULL;
    }
    
    dom_document_t* doc = html_parser_parse(&parser);
    html_parser_destroy(&parser);
    
    return doc;
}
