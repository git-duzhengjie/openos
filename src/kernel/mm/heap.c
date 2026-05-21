/* ============================================================
 * openos - 内核堆内存分配器
 * 基于物理页分配 + VMM 映射的简单分配器
 * ============================================================ */

#include "../include/heap.h"
#include "../include/pmm.h"
#include "../include/vmm.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

/* 堆区域配置 */
#define KERNEL_HEAP_START   0xC0000000   /* 3GB 虚拟地址起始 */
#define KERNEL_HEAP_END     0xD0000000   /* 4GB 虚拟地址结束 */
#define HEAP_CHUNK_SIZE     4096         /* 最小分配粒度: 1页 */

/* 当前堆顶（已映射区域的最高地址） */
static uint32_t heap_current = KERNEL_HEAP_START;

/* 分配一个堆页（用于 kmalloc 的底层） */
static void *kmalloc_page(void) {
    void *phys = pmm_alloc_page();
    if (!phys) return NULL;
    
    /* 映射到虚拟地址 */
    vmm_map_page(heap_current, (uint32_t)phys, VMM_RW | VMM_USER);
    heap_current += PAGE_SIZE;
    
    return (void *)((uint32_t)phys);
}

/* 获取当前堆顶的虚拟地址（已映射） */
uint32_t heap_get_current(void) {
    return heap_current;
}

/* 扩展堆（分配物理页并映射到虚拟地址） */
static void *expand_heap(uint32_t size) {
    /* 对齐到页边界 */
    size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    
    uint32_t old_heap = heap_current;
    heap_current += size;
    
    /* 检查是否超出堆上限 */
    if (heap_current > KERNEL_HEAP_END) {
        heap_current = old_heap;
        return NULL;
    }
    
    /* 分配物理页并映射 */
    uint32_t vaddr = old_heap;
    while (size > 0) {
        void *phys = pmm_alloc_page();
        if (!phys) {
            /* 失败，回滚 */
            heap_current = old_heap;
            return NULL;
        }
        vmm_map_page(vaddr, (uint32_t)phys, VMM_RW | VMM_USER);
        vaddr += PAGE_SIZE;
        size -= PAGE_SIZE;
    }
    
    return (void *)old_heap;
}

/* ============================================================
 * kmalloc - 分配内核内存
 * ============================================================ */
void *kmalloc(uint32_t size) {
    if (size == 0) return NULL;
    
    /* 最小分配粒度: 1页 */
    uint32_t actual = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    
    return expand_heap(actual);
}

/* ============================================================
 * kfree - 释放内核内存（当前实现为 no-op）
 * ============================================================ */
void kfree(void *ptr) {
    (void)ptr;
    /* 简化实现：目前不释放物理页
     * 后续可实现页帧链表或更复杂的释放机制 */
}

/* ============================================================
 * 堆初始化
 * ============================================================ */
void heap_init(void) {
    heap_current = KERNEL_HEAP_START;
    
    /* 确保起始区域已映射 */
    for (uint32_t v = KERNEL_HEAP_START; v < KERNEL_HEAP_START + PAGE_SIZE; v += PAGE_SIZE) {
        void *phys = pmm_alloc_page();
        if (phys) {
            vmm_map_page(v, (uint32_t)phys, VMM_RW | VMM_USER);
        }
    }
    heap_current = KERNEL_HEAP_START + PAGE_SIZE;
}