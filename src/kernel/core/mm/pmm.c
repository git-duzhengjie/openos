/* ============================================================
 * openos - 物理内存管理实现
 * ============================================================ */

#include "../include/pmm.h"
#include "../include/gdt.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

/* 全局 PMM 状态 */
static pmm_info_t pmm = {0};

#define PMM_REF_MAX_PAGES 8192u
static uint16_t pmm_refcounts[PMM_REF_MAX_PAGES];

/* VGA 输出宏 */
#define VGA_BASE  ((volatile uint16_t *)0xB8000)
#define VGA_WIDTH 80
static int vga_pos = 0;

static void vga_put(char c, uint8_t color)
{
    if (c == '\n') {
        vga_pos = (vga_pos / VGA_WIDTH + 1) * VGA_WIDTH;
        return;
    }
    VGA_BASE[vga_pos++] = (uint16_t)((color << 8) | c);
}

static void vga_write_str(const char *s, uint8_t color)
{
    while (*s) {
        vga_put(*s++, color);
    }
}

static void vga_write_hex(uint32_t val, uint8_t color)
{
    const char *hex = "0123456789ABCDEF";
    vga_write_str("0x", color);
    for (int i = 28; i >= 0; i -= 4)
        vga_put(hex[(val >> i) & 0xF], color);
}

static uint32_t pmm_page_index(void *page)
{
    return ((uint32_t)page) / PAGE_SIZE;
}

static int pmm_page_tracked(uint32_t page_num)
{
    return page_num < PMM_REF_MAX_PAGES;
}

/* ============================================================
 * 初始化 (基础版本，无 GRUB mmap)
 * ============================================================ */
void pmm_init(uint32_t kernel_end)
{
    /* 假设 32MB 可用内存 (典型 QEMU 环境) */
    uint32_t memory_end = 0x2000000;  /* 32MB */
    uint32_t bitmap_pages = (memory_end / PAGE_SIZE) / 32;
    uint32_t bitmap_bytes = bitmap_pages * 4;

    /* 位图放在内核之后 */
    uint32_t bitmap_addr = (kernel_end + 0xFFF) & ~0xFFF;  /* 4KB 对齐 */
    pmm.bitmap = (uint32_t *)bitmap_addr;
    pmm.bitmap_size = bitmap_bytes;
    pmm.total_pages = memory_end / PAGE_SIZE;
    pmm.free_pages = pmm.total_pages - (bitmap_addr / PAGE_SIZE) - 8;
    pmm.used_pages = 0;
    pmm.memory_end = memory_end;

    /* 清空位图，全部标记为已占用 */
    for (uint32_t i = 0; i < bitmap_pages; i++)
        pmm.bitmap[i] = 0xFFFFFFFF;

    /* 标记已用页: 位图自身 + 内核 + 前8页保留 */
    uint32_t bitmap_page = bitmap_addr / PAGE_SIZE;
    for (uint32_t i = 0; i <= bitmap_page + 8; i++) {
        uint32_t word = i / 32;
        uint32_t bit  = i % 32;
        pmm.bitmap[word] &= ~(1U << bit);
    }

    /* 标记可用页 */
    uint32_t start = bitmap_page + 8 + 1;
    uint32_t end   = pmm.total_pages;
    pmm_set_bitmap_range(start, end - start, 0);

    vga_write_str("[PMM] init: ", 0x0A);
    vga_write_str("total=", 0x0A);
    /* 数字打印通过hex近似 */
    vga_write_hex(pmm.total_pages, 0x0A);
    vga_write_str(" pages\n", 0x0A);
}

/* ============================================================
 * 设置位图范围
 * ============================================================ */
void pmm_set_bitmap_range(uint32_t start_page, uint32_t count, int used)
{
    for (uint32_t i = 0; i < count; i++) {
        uint32_t page = start_page + i;
        uint32_t word = page / 32;
        uint32_t bit  = page % 32;

        if (word >= pmm.bitmap_size / 4)
            break;

        if (used)
            pmm.bitmap[word] &= ~(1U << bit);
        else
            pmm.bitmap[word] |=  (1U << bit);
    }
}

/* ============================================================
 * 分配一个物理页
 * ============================================================ */
void *pmm_alloc_page(void)
{
    /* 从高地址向低地址搜索第一个空闲位 */
    for (int32_t word = (pmm.bitmap_size / 4) - 1; word >= 0; word--) {
        if (pmm.bitmap[word] == 0)
            continue;

        /* 找到有空闲位的 word */
        for (int bit = 31; bit >= 0; bit--) {
            if (pmm.bitmap[word] & (1U << bit)) {
                /* 分配 */
                pmm.bitmap[word] &= ~(1U << bit);
                uint32_t page = word * 32 + bit;
                pmm.used_pages++;
                pmm.free_pages--;
                if (pmm_page_tracked(page))
                    pmm_refcounts[page] = 1;
                return (void *)(page * PAGE_SIZE);
            }
        }
    }
    return NULL;  /* 没有空闲页 */
}

/* ============================================================
 * Allocate contiguous physical pages
 * ============================================================ */
void *pmm_alloc_pages(uint32_t count)
{
    if (count == 0)
        count = 1;
    if (count > pmm.free_pages)
        return NULL;

    for (int32_t start = (int32_t)(pmm.total_pages - count); start >= 0; start--) {
        int ok = 1;
        for (uint32_t i = 0; i < count; i++) {
            uint32_t page = (uint32_t)start + i;
            uint32_t word = page / 32;
            uint32_t bit  = page % 32;
            if (word >= pmm.bitmap_size / 4 || !(pmm.bitmap[word] & (1U << bit))) {
                ok = 0;
                break;
            }
        }
        if (!ok)
            continue;

        for (uint32_t i = 0; i < count; i++) {
            uint32_t page = (uint32_t)start + i;
            uint32_t word = page / 32;
            uint32_t bit  = page % 32;
            pmm.bitmap[word] &= ~(1U << bit);
        }
        pmm.used_pages += count;
        pmm.free_pages -= count;
        for (uint32_t i = 0; i < count; i++) {
            uint32_t page = (uint32_t)start + i;
            if (pmm_page_tracked(page))
                pmm_refcounts[page] = 1;
        }
        return (void *)((uint32_t)start * PAGE_SIZE);
    }

    return NULL;
}

/* ============================================================
 * 释放一个物理页
 * ============================================================ */
void pmm_free_page(void *page)
{
    uint32_t page_num = pmm_page_index(page);
    uint32_t word = page_num / 32;
    uint32_t bit  = page_num % 32;

    if (word >= pmm.bitmap_size / 4)
        return;

    if (pmm_page_tracked(page_num)) {
        if (pmm_refcounts[page_num] > 1) {
            pmm_refcounts[page_num]--;
            return;
        }
        if (pmm_refcounts[page_num] == 0)
            return;
        pmm_refcounts[page_num] = 0;
    }

    pmm.bitmap[word] |= (1U << bit);
    if (pmm.used_pages > 0)
        pmm.used_pages--;
    pmm.free_pages++;
}

void pmm_ref_page(void *page)
{
    uint32_t page_num = pmm_page_index(page);
    if (!pmm_page_tracked(page_num))
        return;
    if (pmm_refcounts[page_num] == 0)
        pmm_refcounts[page_num] = 1;
    else
        pmm_refcounts[page_num]++;
}

uint32_t pmm_page_refcount(void *page)
{
    uint32_t page_num = pmm_page_index(page);
    if (!pmm_page_tracked(page_num))
        return 1;
    return pmm_refcounts[page_num];
}

/* ============================================================
 * 获取空闲页数
 * ============================================================ */
uint32_t pmm_get_free_pages(void)
{
    return pmm.free_pages;
}

/* ============================================================
 * 从 GRUB mmap 初始化
 * ============================================================ */
void pmm_init_from_mmap(mmap_entry_t *mmap, uint32_t count)
{
    uint32_t usable_end  = 0;

    for (uint32_t i = 0; i < count; i++) {
        if (mmap[i].type == MMAP_USABLE) {
            if (usable_end < mmap[i].base_addr + mmap[i].length)
                usable_end = (uint32_t)(mmap[i].base_addr + mmap[i].length);
        }
    }

    /* 重新初始化 */
    pmm.total_pages = usable_end / PAGE_SIZE;
    pmm.free_pages = pmm.total_pages;
    pmm.used_pages = 0;
    pmm.memory_end = usable_end;

    vga_write_str("[PMM] mmap init: ", 0x0A);
    vga_write_hex(usable_end, 0x0A);
    vga_write_str(" bytes usable\n", 0x0A);
}

/* ============================================================
 * 打印状态
 * ============================================================ */
void pmm_print_status(void)
{
    vga_write_str("[PMM] free=", 0x0A);
    vga_write_hex(pmm.free_pages, 0x0A);
    vga_write_str(" used=", 0x0A);
    vga_write_hex(pmm.used_pages, 0x0A);
    vga_write_str("\n", 0x0A);
}