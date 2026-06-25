/* ============================================================
 * openos - 内核堆内存分配器
 * 显式空闲链表 + 首次适应 + 相邻合并
 * ============================================================ */

#include "../include/heap.h"
#include "../include/pmm.h"
#include "../include/vmm.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

/* 堆区域：恒等映射内核堆。
 * 不能固定从 4MB 开始：随着内核镜像/BSS 增长，固定地址可能覆盖
 * 静态全局区（例如 VFS dentry/inode 池），导致启动期随机崩溃。
 */
#define HEAP_MIN_START      0x00400000UL
#define HEAP_BOOT_GUARD     (10UL * 4096UL)
#define HEAP_END            0x00C00000UL   /* 12MB处 */

extern uint32_t __kernel_end;

/* 分配头部（4字节）：块总大小，低2位保留 */
typedef struct alloc_header {
    uint32_t size;   /* 块总大小（含头部），低2位保留 */
} alloc_header_t;

/* 空闲块（复用同一片内存，分配时低2位=0） */
typedef struct free_block {
    uint32_t        size;   /* 同 alloc_header_t.size */
    struct free_block *next; /* 下一个空闲块 */
} free_block_t;

/* size 字段的 flag 位 */
#define BLK_ALLOC   0x1UL  /* 已分配 */
#define BLK_SIZE(v) ((v) & ~0x3UL)

/* 全局状态 */
static free_block_t *g_free_list = NULL;
static uint32_t      g_heap_top = HEAP_MIN_START;

static uint32_t heap_align_up(uint32_t value, uint32_t align)
{
    return (value + align - 1) & ~(align - 1);
}

/* ---- 内部：扩展堆，返回新的虚拟地址起点 ---- */
static uint32_t heap_expand(uint32_t nbytes)
{
    uint32_t pages = (nbytes + 4095) / 4096;
    uint32_t vaddr = g_heap_top;

    for (uint32_t i = 0; i < pages; i++) {
        if (g_heap_top >= (uint32_t)HEAP_END)
            return 0;
        void *phys = pmm_alloc_page();
        if (!phys)
            return 0;
        vmm_map_page(g_heap_top, (uint32_t)phys, VMM_RW);
        g_heap_top += 4096;
    }
    return vaddr;
}

/* ---- 公共：返回当前堆顶虚拟地址 ---- */
uint32_t heap_get_current(void)
{
    return g_heap_top;
}

/* ---- 初始化堆 ---- */
void heap_init(void)
{
    /* PMM places its page bitmap immediately after __kernel_end and keeps a
     * small bootstrap guard there.  The heap uses virtual addresses that are
     * remapped to freshly allocated physical pages, so starting exactly at
     * __kernel_end would hide the PMM bitmap and break all later page
     * allocations.  Start after that bootstrap bookkeeping area.
     */
    g_heap_top = heap_align_up((uint32_t)&__kernel_end + HEAP_BOOT_GUARD, 4096);
    if (g_heap_top < HEAP_MIN_START)
        g_heap_top = HEAP_MIN_START;
    g_free_list = NULL;

    /* 预分配 16KB */
    uint32_t start = heap_expand(16 * 1024);
    if (!start)
        return;

    free_block_t *fb = (free_block_t *)start;
    fb->size = 16 * 1024;
    fb->next = NULL;
    g_free_list = fb;
}

/* ============================================================
 * kmalloc - 分配内核内存
 * ============================================================ */
void *kmalloc(uint32_t size)
{
    if (size == 0)
        return NULL;

    /* 对齐到 8 字节 + 头部 */
    uint32_t total = (size + 7 + sizeof(alloc_header_t)) & ~7UL;
    if (total < sizeof(free_block_t))
        total = sizeof(free_block_t);

    /* ---- 搜索空闲链表（首次适应）---- */
    free_block_t *prev = NULL;
    free_block_t *cur  = g_free_list;

    while (cur) {
        if (BLK_SIZE(cur->size) >= total) {
            /* 可以分配 */
            uint32_t old_size = BLK_SIZE(cur->size);

            if (old_size >= total + sizeof(free_block_t)) {
                /* 分裂：剩余部分形成新的空闲块 */
                free_block_t *remain =
                    (free_block_t *)((uint32_t)cur + total);
                remain->size = old_size - total;
                remain->next = cur->next;

                if (prev)
                    prev->next = remain;
                else
                    g_free_list = remain;

                cur->size = total | BLK_ALLOC;
            } else {
                /* 整块分配 */
                if (prev)
                    prev->next = cur->next;
                else
                    g_free_list = cur->next;

                cur->size = old_size | BLK_ALLOC;
            }

            /* 返回用户指针（跳过头部）*/
            alloc_header_t *hdr = (alloc_header_t *)cur;
            return (void *)(hdr + 1);
        }
        prev = cur;
        cur  = cur->next;
    }

    /* ---- 没找到，扩展堆 ---- */
    uint32_t needed_pages = (total + 4095) / 4096;
    uint32_t new_vaddr    = heap_expand(needed_pages * 4096);
    if (!new_vaddr)
        return NULL;

    /* 把新页面作为空闲块插入链表头部 */
    free_block_t *nb = (free_block_t *)new_vaddr;
    nb->size = needed_pages * 4096;
    nb->next = g_free_list;
    g_free_list = nb;

    /* 递归分配（这次一定能分到）*/
    return kmalloc(size);
}

/* ============================================================
 * kfree - 释放内核内存
 * ========================================================== */
void kfree(void *ptr)
{
    if (!ptr)
        return;

    alloc_header_t *hdr = (alloc_header_t *)ptr - 1;
    if (!(hdr->size & BLK_ALLOC))
        return; /* 重复释放，忽略 */

    /* 转成空闲块 */
    free_block_t *blk = (free_block_t *)hdr;
    blk->size = BLK_SIZE(hdr->size);   /* 清除 flag */
    /* blk->next 会在下面设置 */

    /* ---- 按地址顺序插入空闲链表（便于合并）---- */
    free_block_t *prev = NULL;
    free_block_t *cur  = g_free_list;

    while (cur && cur < blk) {
        prev = cur;
        cur  = cur->next;
    }

    blk->next = cur;
    if (prev)
        prev->next = blk;
    else
        g_free_list = blk;

    /* ---- 合并后面的块 ---- */
    if (blk->next &&
        (uint32_t)blk + BLK_SIZE(blk->size) ==
        (uint32_t)blk->next) {
        blk->size += BLK_SIZE(blk->next->size);
        blk->next  = blk->next->next;
        cur = blk->next;  /* 更新 cur 供后面用 */
    }

    /* ---- 合并前面的块 ---- */
    if (prev &&
        (uint32_t)prev + BLK_SIZE(prev->size) ==
        (uint32_t)blk) {
        prev->size += BLK_SIZE(blk->size);
        prev->next  = blk->next;
    }
}
