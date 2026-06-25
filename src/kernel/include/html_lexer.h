#ifndef OPENOS_HTML_LEXER_H
#define OPENOS_HTML_LEXER_H

#include <stddef.h>
#include <stdint.h>

// HTML 词法单元类型
typedef enum {
    HTML_TOKEN_ERROR = -1,
    HTML_TOKEN_EOF = 0,
    
    // 标签相关
    HTML_TOKEN_TAG_OPEN,         // <tag
    HTML_TOKEN_TAG_CLOSE,        // </tag
    HTML_TOKEN_TAG_SELF_CLOSE,   // />
    HTML_TOKEN_TAG_END,          // >
    
    // 属性
    HTML_TOKEN_ATTR_NAME,
    HTML_TOKEN_ATTR_EQ,
    HTML_TOKEN_ATTR_VALUE,
    
    // 内容
    HTML_TOKEN_TEXT,
    HTML_TOKEN_COMMENT,
    HTML_TOKEN_DOCTYPE,
    
    // 特殊标签内容
    HTML_TOKEN_CDATA,
    HTML_TOKEN_SCRIPT_DATA,
    HTML_TOKEN_STYLE_DATA,
} html_token_type_t;

// HTML 词法单元
typedef struct {
    html_token_type_t type;
    const char* start;
    size_t length;
    int line;
    int column;
} html_token_t;

// HTML 词法分析器状态
typedef struct {
    const char* input;
    size_t input_len;
    size_t pos;
    int line;
    int column;
    
    // 状态标记
    int in_tag;
    int in_comment;
    int in_script;
    int in_style;
    int in_cdata;
    
    // 标签上下文
    char current_tag[64];
} html_lexer_t;

// 初始化词法分析器
void html_lexer_init(html_lexer_t* lexer, const char* input, size_t len);

// 获取下一个词法单元
int html_lexer_next(html_lexer_t* lexer, html_token_t* token);

// 回退一个位置
void html_lexer_rewind(html_lexer_t* lexer);

// 工具：复制token内容到缓冲区
size_t html_token_copy(const html_token_t* token, char* buf, size_t buf_size);

#endif
