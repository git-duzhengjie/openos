#ifndef OPENOS_DOM_H
#define OPENOS_DOM_H

#include <stddef.h>
#include <stdint.h>

// DOM 节点类型
typedef enum {
    DOM_ELEMENT_NODE,
    DOM_TEXT_NODE,
    DOM_COMMENT_NODE,
    DOM_DOCUMENT_NODE,
} dom_node_type_t;

// DOM 节点前向声明
typedef struct dom_node dom_node_t;
typedef struct dom_element dom_element_t;
typedef struct dom_document dom_document_t;
typedef struct dom_attr dom_attr_t;

// 属性链表
struct dom_attr {
    char* name;
    char* value;
    dom_attr_t* next;
};

// 元素节点
struct dom_element {
    char* tag_name;
    dom_attr_t* attributes;
    dom_node_t* first_child;
    dom_node_t* last_child;
};

// 文本节点
typedef struct {
    char* data;
    size_t length;
} dom_text_t;

// 注释节点
typedef struct {
    char* data;
    size_t length;
} dom_comment_t;

// 文档节点
struct dom_document {
    dom_node_t* document_element;  // <html>
    dom_node_t* head;
    dom_node_t* body;
    char* title;
};

// DOM 节点（通用结构）
struct dom_node {
    dom_node_type_t type;
    dom_node_t* parent;
    dom_node_t* prev_sibling;
    dom_node_t* next_sibling;
    dom_document_t* owner_document;
    
    union {
        dom_element_t element;
        dom_text_t text;
        dom_comment_t comment;
        dom_document_t document;
    } data;
};

// DOM 文档创建
dom_document_t* dom_document_create(void);
void dom_document_destroy(dom_document_t* doc);

// 节点创建
dom_node_t* dom_element_create(dom_document_t* doc, const char* tag_name);
dom_node_t* dom_text_create(dom_document_t* doc, const char* data);
dom_node_t* dom_comment_create(dom_document_t* doc, const char* data);

// 节点操作
dom_node_t* dom_node_append_child(dom_node_t* parent, dom_node_t* child);
dom_node_t* dom_node_remove_child(dom_node_t* parent, dom_node_t* child);
void dom_node_destroy(dom_node_t* node);

// 属性操作
int dom_element_set_attribute(dom_node_t* element, const char* name, const char* value);
const char* dom_element_get_attribute(dom_node_t* element, const char* name);
void dom_element_remove_attribute(dom_node_t* element, const char* name);

// 遍历工具
dom_node_t* dom_get_element_by_id(dom_node_t* root, const char* id);
void dom_get_elements_by_tag(dom_node_t* root, const char* tag,
                             dom_node_t*** result, size_t* count);

// 调试输出
void dom_dump_tree(dom_node_t* node, int indent);

#endif
