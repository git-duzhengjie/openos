/* ============================================================
 * openos - 内核堆内存管理
 * ============================================================ */

#ifndef KERNEL_HEAP_H
#define KERNEL_HEAP_H

#include <stdint.h>

/* 堆区域配置 */
#define KERNEL_HEAP_START   0xC0000000   /* 3GB 虚拟地址 */
#define KERNEL_HEAP_END     0xD0000000   /* 4GB 虚拟地址 */
#define HEAP_CHUNK_SIZE     4096         /* 最小分配粒度: 1页 */

/* 分配内存 */
void *kmalloc(uint32_t size);

/* 释放内存（简化实现） */
void kfree(void *ptr);

/* 堆初始化 */
void heap_init(void);

/* 获取当前堆顶虚拟地址 */
uint32_t heap_get_current(void);

#endif /* KERNEL_HEAP_H */