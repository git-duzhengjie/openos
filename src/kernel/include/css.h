#ifndef OPENOS_CSS_H
#define OPENOS_CSS_H

#include <stdint.h>
#include <stddef.h>

// CSS 值类型
typedef enum {
    CSS_VALUE_NONE,
    CSS_VALUE_KEYWORD,
    CSS_VALUE_LENGTH,
    CSS_VALUE_PERCENTAGE,
    CSS_VALUE_COLOR,
    CSS_VALUE_STRING,
    CSS_VALUE_NUMBER,
} css_value_type_t;

// CSS 单位
typedef enum {
    CSS_UNIT_PX,
    CSS_UNIT_EM,
    CSS_UNIT_REM,
    CSS_UNIT_PERCENT,
    CSS_UNIT_PT,
    CSS_UNIT_VW,
    CSS_UNIT_VH,
    CSS_UNIT_NONE,
} css_unit_t;

// CSS 颜色
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} css_color_t;

// CSS 值
typedef struct css_value {
    css_value_type_t type;
    union {
        char* keyword;
        struct {
            float value;
            css_unit_t unit;
        } length;
        float percentage;
        css_color_t color;
        char* string;
        float number;
    } data;
    struct css_value* next;  // 用于多值属性（如 border）
} css_value_t;

// CSS 声明（属性: 值）
typedef struct css_declaration {
    char* property;
    css_value_t* value;
    int important;
    struct css_declaration* next;
} css_declaration_t;

// CSS 选择器类型
typedef enum {
    CSS_SELECTOR_UNIVERSAL,  // *
    CSS_SELECTOR_TAG,        // div
    CSS_SELECTOR_CLASS,      // .class
    CSS_SELECTOR_ID,         // #id
    CSS_SELECTOR_ATTR,       // [attr]
    CSS_SELECTOR_COMBINATOR, // 组合子
} css_selector_type_t;

// CSS 选择器
typedef struct css_selector {
    css_selector_type_t type;
    char* value;
    
    // 属性选择器
    char* attr_name;
    char* attr_value;
    char attr_match;  // =, ~=, |=, ^=, $=, *=
    
    struct css_selector* next;  // 组合选择器
} css_selector_t;

// CSS 规则集
typedef struct css_rule {
    css_selector_t* selectors;
    css_declaration_t* declarations;
    struct css_rule* next;
    
    // 特异性计算
    uint32_t specificity;
} css_rule_t;

// CSS 样式表
typedef struct {
    css_rule_t* rules;
    int rule_count;
} css_stylesheet_t;

// 解析函数
css_stylesheet_t* css_parse(const char* css, size_t len);
void css_stylesheet_destroy(css_stylesheet_t* sheet);

// 值解析工具
css_value_t* css_value_parse(const char* str);
void css_value_destroy(css_value_t* value);

// 颜色解析
int css_color_parse(const char* str, css_color_t* color);

#endif
