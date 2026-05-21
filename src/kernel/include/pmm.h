/* ============================================================
 * openos - 物理内存管理
 * 基于位图的物理页分配器
 * ============================================================ */

#ifndef KERNEL_PMM_H
#define KERNEL_PMM_H

#include <stdint.h>

/* 页大小 (4KB) */
#define PAGE_SIZE      4096
#define PAGE_SHIFT     12

/* 内存区域类型 */
#define MMAP_FREE      0x00
#define MMAP_RESERVED   0x01
#define MMAP_USABLE     0x02
#define MMAP_ACPI_RECL  0x03
#define MMAP_ACPI_NVS   0x04

/* 内存区域描述符 (来自 GRUB) */
typedef struct {
    uint32_t size;
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
} __attribute__((packed)) mmap_entry_t;

/* ============================================================
 * 物理内存管理器
 * ============================================================ */
typedef struct {
    uint32_t total_pages;   /* 总页数 */
    uint32_t used_pages;    /* 已使用页数 */
    uint32_t free_pages;     /* 空闲页数 */
    uint32_t bitmap_size;    /* 位图大小(字节) */
    uint32_t *bitmap;        /* 位图起始地址 */
    uint32_t memory_end;     /* 可用内存结束地址 */
} pmm_info_t;

/* ============================================================
 * 函数声明
 * ============================================================ */
void pmm_init(uint32_t kernel_end);           /* 初始化物理内存管理 */
void *pmm_alloc_page(void);                  /* 分配一个物理页 */
void  pmm_free_page(void *page);             /* 释放一个物理页 */
uint32_t pmm_get_free_pages(void);           /* 获取空闲页数 */
void pmm_init_from_mmap(mmap_entry_t *mmap, uint32_t count);  /* 从 GRUB mmap 初始化 */
void pmm_set_bitmap_range(uint32_t start_page, uint32_t count, int used); /* 设置范围状态 */

/* 调试 */
void pmm_print_status(void);

#endif /* KERNEL_PMM_H */