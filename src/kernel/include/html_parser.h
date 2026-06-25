#ifndef OPENOS_HTML_PARSER_H
#define OPENOS_HTML_PARSER_H

#include "dom.h"
#include "html_lexer.h"

// HTML 解析器状态
typedef struct {
    html_lexer_t lexer;
    dom_document_t* document;
    dom_node_t* current_node;
    
    // 解析错误
    int error_count;
    char last_error[256];
    
    // 标签栈（用于处理嵌套）
    dom_node_t** tag_stack;
    size_t tag_stack_size;
    size_t tag_stack_capacity;
} html_parser_t;

// 初始化解析器
int html_parser_init(html_parser_t* parser, const char* html, size_t len);

// 解析HTML文档
dom_document_t* html_parser_parse(html_parser_t* parser);

// 释放解析器资源
void html_parser_destroy(html_parser_t* parser);

// 便捷函数：直接解析HTML字符串到DOM
dom_document_t* html_parse(const char* html, size_t len);

#endif
