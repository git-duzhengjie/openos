#include "html_lexer.h"
#include <string.h>
#include <ctype.h>

void html_lexer_init(html_lexer_t* lexer, const char* input, size_t len) {
    memset(lexer, 0, sizeof(html_lexer_t));
    lexer->input = input;
    lexer->input_len = len;
    lexer->pos = 0;
    lexer->line = 1;
    lexer->column = 1;
}

static char html_lexer_peek(html_lexer_t* lexer, int offset) {
    size_t p = lexer->pos + offset;
    if (p >= lexer->input_len) return '\0';
    return lexer->input[p];
}

static char html_lexer_consume(html_lexer_t* lexer) {
    char c = lexer->input[lexer->pos];
    if (c == '\n') {
        lexer->line++;
        lexer->column = 1;
    } else {
        lexer->column++;
    }
    lexer->pos++;
    return c;
}

static int html_lexer_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static void html_lexer_skip_spaces(html_lexer_t* lexer) {
    while (html_lexer_is_space(html_lexer_peek(lexer, 0))) {
        html_lexer_consume(lexer);
    }
}

static int html_lexer_match(html_lexer_t* lexer, const char* str, int case_insensitive) {
    size_t len = strlen(str);
    if (lexer->pos + len > lexer->input_len) return 0;
    
    for (size_t i = 0; i < len; i++) {
        char a = lexer->input[lexer->pos + i];
        char b = str[i];
        
        if (case_insensitive) {
            a = tolower(a);
            b = tolower(b);
        }
        
        if (a != b) return 0;
    }
    
    return 1;
}

static void html_lexer_start_token(html_lexer_t* lexer, html_token_t* token, html_token_type_t type) {
    token->type = type;
    token->start = lexer->input + lexer->pos;
    token->length = 0;
    token->line = lexer->line;
    token->column = lexer->column;
}

static void html_lexer_end_token(html_lexer_t* lexer, html_token_t* token) {
    token->length = (lexer->input + lexer->pos) - token->start;
}

void html_lexer_rewind(html_lexer_t* lexer) {
    if (lexer->pos > 0) {
        lexer->pos--;
        if (lexer->input[lexer->pos] == '\n') {
            lexer->line--;
        }
    }
}

size_t html_token_copy(const html_token_t* token, char* buf, size_t buf_size) {
    if (!token || !buf || buf_size == 0) return 0;
    
    size_t copy_len = token->length;
    if (copy_len >= buf_size) copy_len = buf_size - 1;
    
    memcpy(buf, token->start, copy_len);
    buf[copy_len] = '\0';
    
    return copy_len;
}

static int html_lexer_read_comment(html_lexer_t* lexer, html_token_t* token) {
    html_lexer_start_token(lexer, token, HTML_TOKEN_COMMENT);
    
    // 跳过 <!--
    for (int i = 0; i < 4; i++) html_lexer_consume(lexer);
    
    // 读取直到 -->
    while (1) {
        if (html_lexer_peek(lexer, 0) == '\0') break;
        
        if (html_lexer_match(lexer, "-->", 0)) {
            for (int i = 0; i < 3; i++) html_lexer_consume(lexer);
            break;
        }
        
        html_lexer_consume(lexer);
    }
    
    html_lexer_end_token(lexer, token);
    return 0;
}

static int html_lexer_read_doctype(html_lexer_t* lexer, html_token_t* token) {
    html_lexer_start_token(lexer, token, HTML_TOKEN_DOCTYPE);
    
    // 跳过 <!DOCTYPE
    while (html_lexer_peek(lexer, 0) != '>' && html_lexer_peek(lexer, 0) != '\0') {
        html_lexer_consume(lexer);
    }
    
    if (html_lexer_peek(lexer, 0) == '>') {
        html_lexer_consume(lexer);
    }
    
    html_lexer_end_token(lexer, token);
    return 0;
}

static int html_lexer_read_tag_name(html_lexer_t* lexer, html_token_t* token) {
    html_lexer_start_token(lexer, token, HTML_TOKEN_TAG_OPEN);
    
    // 读取标签名
    while (isalnum(html_lexer_peek(lexer, 0)) || 
           html_lexer_peek(lexer, 0) == '-' ||
           html_lexer_peek(lexer, 0) == '_') {
        html_lexer_consume(lexer);
    }
    
    html_lexer_end_token(lexer, token);
    
    // 保存当前标签名
    size_t len = token->length < sizeof(lexer->current_tag) - 1 ? 
                 token->length : sizeof(lexer->current_tag) - 1;
    memcpy(lexer->current_tag, token->start, len);
    lexer->current_tag[len] = '\0';
    
    // 检查是否是script或style标签
    if (strcasecmp(lexer->current_tag, "script") == 0) {
        lexer->in_script = 1;
    } else if (strcasecmp(lexer->current_tag, "style") == 0) {
        lexer->in_style = 1;
    }
    
    return 0;
}

static int html_lexer_read_closing_tag(html_lexer_t* lexer, html_token_t* token) {
    html_lexer_start_token(lexer, token, HTML_TOKEN_TAG_CLOSE);
    
    // 跳过 </
    html_lexer_consume(lexer);
    html_lexer_consume(lexer);
    
    html_lexer_skip_spaces(lexer);
    
    // 读取标签名
    token->start = lexer->input + lexer->pos;
    while (isalnum(html_lexer_peek(lexer, 0)) || 
           html_lexer_peek(lexer, 0) == '-' ||
           html_lexer_peek(lexer, 0) == '_') {
        html_lexer_consume(lexer);
    }
    
    html_lexer_end_token(lexer, token);
    lexer->in_tag = 1;
    return 0;
}

static int html_lexer_read_attr_name(html_lexer_t* lexer, html_token_t* token) {
    html_lexer_start_token(lexer, token, HTML_TOKEN_ATTR_NAME);
    
    while (isalnum(html_lexer_peek(lexer, 0)) ||
           html_lexer_peek(lexer, 0) == '-' ||
           html_lexer_peek(lexer, 0) == '_' ||
           html_lexer_peek(lexer, 0) == ':') {
        html_lexer_consume(lexer);
    }
    
    html_lexer_end_token(lexer, token);
    return 0;
}

static int html_lexer_read_attr_value(html_lexer_t* lexer, html_token_t* token) {
    html_lexer_consume(lexer); // 跳过 =
    html_lexer_skip_spaces(lexer);
    
    char quote = 0;
    char c = html_lexer_peek(lexer, 0);
    
    if (c == '"' || c == '\'') {
        quote = c;
        html_lexer_consume(lexer);
    }
    
    html_lexer_start_token(lexer, token, HTML_TOKEN_ATTR_VALUE);
    
    if (quote) {
        while (html_lexer_peek(lexer, 0) != quote && html_lexer_peek(lexer, 0) != '\0') {
            html_lexer_consume(lexer);
        }
        html_lexer_end_token(lexer, token);
        if (html_lexer_peek(lexer, 0) == quote) {
            html_lexer_consume(lexer);
        }
    } else {
        while (!html_lexer_is_space(html_lexer_peek(lexer, 0)) &&
               html_lexer_peek(lexer, 0) != '>' &&
               html_lexer_peek(lexer, 0) != '/' &&
               html_lexer_peek(lexer, 0) != '\0') {
            html_lexer_consume(lexer);
        }
        html_lexer_end_token(lexer, token);
    }
    
    return 0;
}

static int html_lexer_read_raw_text(html_lexer_t* lexer, html_token_t* token, const char* end_tag) {
    html_lexer_start_token(lexer, token, HTML_TOKEN_TEXT);
    
    size_t end_len = strlen(end_tag);
    
    while (1) {
        if (html_lexer_peek(lexer, 0) == '\0') break;
        
        if (html_lexer_peek(lexer, 0) == '<') {
            // 检查是否是结束标签
            int match = 1;
            for (size_t i = 0; i < end_len && match; i++) {
                char a = tolower(html_lexer_peek(lexer, 1 + i));
                char b = tolower(end_tag[i]);
                if (a != b) match = 0;
            }
            
            if (match) {
                char after = html_lexer_peek(lexer, 1 + end_len);
                if (after == '>' || after == ' ' || after == '\t' || after == '\n') {
                    break;
                }
            }
        }
        
        html_lexer_consume(lexer);
    }
    
    html_lexer_end_token(lexer, token);
    
    // 退出特殊模式
    lexer->in_script = 0;
    lexer->in_style = 0;
    
    return token->length > 0 ? 0 : -1;
}

static int html_lexer_read_text(html_lexer_t* lexer, html_token_t* token) {
    html_lexer_start_token(lexer, token, HTML_TOKEN_TEXT);
    
    while (html_lexer_peek(lexer, 0) != '<' && html_lexer_peek(lexer, 0) != '\0') {
        html_lexer_consume(lexer);
    }
    
    html_lexer_end_token(lexer, token);
    return token->length > 0 ? 0 : -1;
}

int html_lexer_next(html_lexer_t* lexer, html_token_t* token) {
    memset(token, 0, sizeof(html_token_t));
    
    if (lexer->pos >= lexer->input_len) {
        token->type = HTML_TOKEN_EOF;
        return -1;
    }
    
    char c = html_lexer_peek(lexer, 0);
    
    // 处理 script/style 原始内容
    if (lexer->in_script) {
        return html_lexer_read_raw_text(lexer, token, "</script");
    }
    if (lexer->in_style) {
        return html_lexer_read_raw_text(lexer, token, "</style");
    }
    
    // 处理标签内的内容
    if (lexer->in_tag) {
        html_lexer_skip_spaces(lexer);
        c = html_lexer_peek(lexer, 0);
        
        if (c == '/') {
            html_lexer_consume(lexer);
            if (html_lexer_peek(lexer, 0) == '>') {
                html_lexer_consume(lexer);
                token->type = HTML_TOKEN_TAG_SELF_CLOSE;
                lexer->in_tag = 0;
                return 0;
            }
            // 不应该到这里
            return -1;
        }
        
        if (c == '>') {
            html_lexer_consume(lexer);
            token->type = HTML_TOKEN_TAG_END;
            lexer->in_tag = 0;
            return 0;
        }
        
        if (isalnum(c)) {
            html_lexer_read_attr_name(lexer, token);
            
            html_lexer_skip_spaces(lexer);
            if (html_lexer_peek(lexer, 0) == '=') {
                html_lexer_consume(lexer);
                html_lexer_rewind(lexer); // 让调用者知道有=
            }
            return 0;
        }
        
        if (c == '=') {
            return html_lexer_read_attr_value(lexer, token);
        }
        
        // 未知字符，跳过
        html_lexer_consume(lexer);
        return html_lexer_next(lexer, token);
    }
    
    // 普通内容
    if (c == '<') {
        // 检查注释
        if (html_lexer_match(lexer, "<!--", 0)) {
            return html_lexer_read_comment(lexer, token);
        }
        
        // 检查DOCTYPE
        if (html_lexer_match(lexer, "<!", 0)) {
            return html_lexer_read_doctype(lexer, token);
        }
        
        // 检查结束标签
        if (html_lexer_peek(lexer, 1) == '/') {
            return html_lexer_read_closing_tag(lexer, token);
        }
        
        // 开始标签
        html_lexer_consume(lexer); // 跳过 <
        lexer->in_tag = 1;
        return html_lexer_read_tag_name(lexer, token);
    }
    
    // 普通文本
    return html_lexer_read_text(lexer, token);
}
