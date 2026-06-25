#include "dom.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

dom_document_t* dom_document_create(void) {
    dom_document_t* doc = malloc(sizeof(dom_document_t));
    if (!doc) return NULL;
    
    memset(doc, 0, sizeof(dom_document_t));
    
    // 创建文档节点
    dom_node_t* doc_node = malloc(sizeof(dom_node_t));
    if (!doc_node) {
        free(doc);
        return NULL;
    }
    
    memset(doc_node, 0, sizeof(dom_node_t));
    doc_node->type = DOM_DOCUMENT_NODE;
    doc_node->owner_document = (dom_document_t*)doc_node;
    
    doc->document_element = NULL;
    doc->head = NULL;
    doc->body = NULL;
    doc->title = NULL;
    
    return doc;
}

void dom_document_destroy(dom_document_t* doc) {
    if (!doc) return;
    
    if (doc->title) free(doc->title);
    
    // 销毁整个文档树
    dom_node_t* doc_node = (dom_node_t*)doc;
    if (doc->document_element) {
        dom_node_destroy(doc->document_element);
    }
    
    free(doc_node);
}

dom_node_t* dom_element_create(dom_document_t* doc, const char* tag_name) {
    if (!doc || !tag_name) return NULL;
    
    dom_node_t* node = malloc(sizeof(dom_node_t));
    if (!node) return NULL;
    
    memset(node, 0, sizeof(dom_node_t));
    node->type = DOM_ELEMENT_NODE;
    node->owner_document = doc;
    node->data.element.tag_name = strdup(tag_name);
    node->data.element.attributes = NULL;
    node->data.element.first_child = NULL;
    node->data.element.last_child = NULL;
    
    return node;
}

dom_node_t* dom_text_create(dom_document_t* doc, const char* data) {
    if (!doc) return NULL;
    
    dom_node_t* node = malloc(sizeof(dom_node_t));
    if (!node) return NULL;
    
    memset(node, 0, sizeof(dom_node_t));
    node->type = DOM_TEXT_NODE;
    node->owner_document = doc;
    
    if (data) {
        node->data.text.data = strdup(data);
        node->data.text.length = strlen(data);
    } else {
        node->data.text.data = NULL;
        node->data.text.length = 0;
    }
    
    return node;
}

dom_node_t* dom_comment_create(dom_document_t* doc, const char* data) {
    if (!doc) return NULL;
    
    dom_node_t* node = malloc(sizeof(dom_node_t));
    if (!node) return NULL;
    
    memset(node, 0, sizeof(dom_node_t));
    node->type = DOM_COMMENT_NODE;
    node->owner_document = doc;
    
    if (data) {
        node->data.comment.data = strdup(data);
        node->data.comment.length = strlen(data);
    } else {
        node->data.comment.data = NULL;
        node->data.comment.length = 0;
    }
    
    return node;
}

dom_node_t* dom_node_append_child(dom_node_t* parent, dom_node_t* child) {
    if (!parent || !child) return NULL;
    
    // 只有元素和文档节点可以有子节点
    if (parent->type != DOM_ELEMENT_NODE && parent->type != DOM_DOCUMENT_NODE) {
        return NULL;
    }
    
    child->parent = parent;
    
    if (parent->type == DOM_ELEMENT_NODE) {
        if (!parent->data.element.first_child) {
            parent->data.element.first_child = child;
            parent->data.element.last_child = child;
        } else {
            child->prev_sibling = parent->data.element.last_child;
            parent->data.element.last_child->next_sibling = child;
            parent->data.element.last_child = child;
        }
    } else {
        // 文档节点
        if (!parent->owner_document->document_element) {
            parent->owner_document->document_element = child;
        }
    }
    
    return child;
}

dom_node_t* dom_node_remove_child(dom_node_t* parent, dom_node_t* child) {
    if (!parent || !child) return NULL;
    
    if (child->prev_sibling) {
        child->prev_sibling->next_sibling = child->next_sibling;
    } else if (parent->type == DOM_ELEMENT_NODE) {
        parent->data.element.first_child = child->next_sibling;
    }
    
    if (child->next_sibling) {
        child->next_sibling->prev_sibling = child->prev_sibling;
    } else if (parent->type == DOM_ELEMENT_NODE) {
        parent->data.element.last_child = child->prev_sibling;
    }
    
    child->parent = NULL;
    child->prev_sibling = NULL;
    child->next_sibling = NULL;
    
    return child;
}

void dom_node_destroy(dom_node_t* node) {
    if (!node) return;
    
    // 先销毁所有子节点
    if (node->type == DOM_ELEMENT_NODE) {
        dom_node_t* child = node->data.element.first_child;
        while (child) {
            dom_node_t* next = child->next_sibling;
            dom_node_destroy(child);
            child = next;
        }
    }
    
    // 根据类型销毁数据
    switch (node->type) {
        case DOM_ELEMENT_NODE:
            if (node->data.element.tag_name) free(node->data.element.tag_name);
            
            dom_attr_t* attr = node->data.element.attributes;
            while (attr) {
                dom_attr_t* next = attr->next;
                free(attr->name);
                free(attr->value);
                free(attr);
                attr = next;
            }
            break;
            
        case DOM_TEXT_NODE:
            if (node->data.text.data) free(node->data.text.data);
            break;
            
        case DOM_COMMENT_NODE:
            if (node->data.comment.data) free(node->data.comment.data);
            break;
            
        default:
            break;
    }
    
    free(node);
}

int dom_element_set_attribute(dom_node_t* element, const char* name, const char* value) {
    if (!element || element->type != DOM_ELEMENT_NODE || !name) {
        return -1;
    }
    
    // 先查找是否已存在
    dom_attr_t* attr = element->data.element.attributes;
    while (attr) {
        if (strcmp(attr->name, name) == 0) {
            free(attr->value);
            attr->value = value ? strdup(value) : NULL;
            return 0;
        }
        attr = attr->next;
    }
    
    // 创建新属性
    attr = malloc(sizeof(dom_attr_t));
    if (!attr) return -1;
    
    attr->name = strdup(name);
    attr->value = value ? strdup(value) : NULL;
    attr->next = element->data.element.attributes;
    element->data.element.attributes = attr;
    
    return 0;
}

const char* dom_element_get_attribute(dom_node_t* element, const char* name) {
    if (!element || element->type != DOM_ELEMENT_NODE || !name) {
        return NULL;
    }
    
    dom_attr_t* attr = element->data.element.attributes;
    while (attr) {
        if (strcmp(attr->name, name) == 0) {
            return attr->value;
        }
        attr = attr->next;
    }
    
    return NULL;
}

void dom_element_remove_attribute(dom_node_t* element, const char* name) {
    if (!element || element->type != DOM_ELEMENT_NODE || !name) {
        return;
    }
    
    dom_attr_t* prev = NULL;
    dom_attr_t* attr = element->data.element.attributes;
    
    while (attr) {
        if (strcmp(attr->name, name) == 0) {
            if (prev) {
                prev->next = attr->next;
            } else {
                element->data.element.attributes = attr->next;
            }
            
            free(attr->name);
            free(attr->value);
            free(attr);
            return;
        }
        
        prev = attr;
        attr = attr->next;
    }
}

static dom_node_t* dom_get_element_by_id_recursive(dom_node_t* node, const char* id) {
    if (!node) return NULL;
    
    if (node->type == DOM_ELEMENT_NODE) {
        const char* node_id = dom_element_get_attribute(node, "id");
        if (node_id && strcmp(node_id, id) == 0) {
            return node;
        }
        
        // 递归子节点
        dom_node_t* child = node->data.element.first_child;
        while (child) {
            dom_node_t* found = dom_get_element_by_id_recursive(child, id);
            if (found) return found;
            child = child->next_sibling;
        }
    }
    
    return NULL;
}

dom_node_t* dom_get_element_by_id(dom_node_t* root, const char* id) {
    return dom_get_element_by_id_recursive(root, id);
}

static void dom_get_elements_by_tag_recursive(dom_node_t* node, const char* tag,
                                              dom_node_t*** result, size_t* count, size_t* capacity) {
    if (!node) return;
    
    if (node->type == DOM_ELEMENT_NODE) {
        if (strcmp(node->data.element.tag_name, tag) == 0) {
            // 扩展数组
            if (*count >= *capacity) {
                *capacity = (*capacity == 0) ? 16 : *capacity * 2;
                *result = realloc(*result, *capacity * sizeof(dom_node_t*));
            }
            (*result)[(*count)++] = node;
        }
        
        // 递归子节点
        dom_node_t* child = node->data.element.first_child;
        while (child) {
            dom_get_elements_by_tag_recursive(child, tag, result, count, capacity);
            child = child->next_sibling;
        }
    }
}

void dom_get_elements_by_tag(dom_node_t* root, const char* tag,
                             dom_node_t*** result, size_t* count) {
    if (!root || !tag || !result || !count) return;
    
    *result = NULL;
    *count = 0;
    size_t capacity = 0;
    
    dom_get_elements_by_tag_recursive(root, tag, result, count, &capacity);
}

void dom_dump_tree(dom_node_t* node, int indent) {
    if (!node) return;
    
    for (int i = 0; i < indent; i++) printf("  ");
    
    switch (node->type) {
        case DOM_ELEMENT_NODE:
            printf("<%s>", node->data.element.tag_name);
            
            dom_attr_t* attr = node->data.element.attributes;
            while (attr) {
                printf(" %s=\"%s\"", attr->name, attr->value ? attr->value : "");
                attr = attr->next;
            }
            printf("\n");
            
            // 递归子节点
            dom_node_t* child = node->data.element.first_child;
            while (child) {
                dom_dump_tree(child, indent + 1);
                child = child->next_sibling;
            }
            break;
            
        case DOM_TEXT_NODE:
            printf("\"%s\"\n", node->data.text.data);
            break;
            
        case DOM_COMMENT_NODE:
            printf("<!-- %s -->\n", node->data.comment.data);
            break;
            
        case DOM_DOCUMENT_NODE:
            printf("#document\n");
            break;
    }
}
