/* ============================================================
 * openos - 虚拟内存管理 (四级页表)
 * ============================================================ */

#ifndef KERNEL_VMM_H
#define KERNEL_VMM_H

#include <stdint.h>

/* 页大小和偏移 */
#define PAGE_SIZE    4096
#define PAGE_SHIFT   12
#define PAGE_MASK    (~(PAGE_SIZE - 1))

/* 地址分解 */
#define PML4_INDEX(addr) (((addr) >> 39) & 0x1FF)
#define PDPT_INDEX(addr) (((addr) >> 30) & 0x1FF)
#define PD_INDEX(addr)   (((addr) >> 21) & 0x1FF)
#define PT_INDEX(addr)   (((addr) >> 12) & 0x1FF)

/* 页表条目标志 */
#define PTE_PRESENT    0x001  /* 存在位 */
#define PTE_RW         0x002  /* 读/写 */
#define PTE_USER       0x004  /* 用户可访问 */
#define PTE_PWT        0x008  /* 写穿 */
#define PTE_PCD        0x010  /* 禁用缓存 */
#define PTE_ACCESSED   0x020  /* 已访问 */
#define PTE_DIRTY      0x040  /* 已修改 */
#define PTE_PAT        0x080  /* PAT */
#define PTE_GLOBAL     0x100  /* 全局 (TLB不刷新) */
#define PTE_FRAME      0xFFFFFFFFFF000 /* 物理页框号 (40位) */

/* 页目录/页表条目结构 */
typedef struct {
    uint64_t val;
} pte_t;

/* ============================================================
 * 虚拟地址区域类型
 * ============================================================ */
#define VMM_KERNEL  0xFFFF800000000000  /* 内核线性映射起始 */
#define VMM_HIGH_BASE VMM_KERNEL        /* 高半核起始 */

/* ============================================================
 * 函数声明
 * ============================================================ */
void vmm_init(void);                              /* 初始化虚拟内存 */
void vmm_map_page(uint64_t vaddr, uint64_t paddr, uint64_t flags);  /* 映射单个页 */
void vmm_unmap_page(uint64_t vaddr);             /* 取消映射 */
uint64_t vmm_get_cr3(void);                       /* 获取当前 CR3 */
void vmm_load_cr3(uint64_t pml4);                /* 加载新 CR3 */
void *vmm_alloc_page(void);                      /* 分配虚拟+物理页并映射 */
void vmm_switch_pml4(uint64_t pml4_phys);       /* 切换到新的页表 */
uint64_t vmm_alloc_page_table(void);             /* 分配一个新的页表(4KB对齐) */

/* 标志快捷方式 */
#define VMM_RW    (PTE_PRESENT | PTE_RW)
#define VMM_RWX   (PTE_PRESENT | PTE_RW | PTE_GLOBAL)
#define VMM_RO    (PTE_PRESENT)
#define VMM_USER  (PTE_PRESENT | PTE_RW | PTE_USER)
#define VMM_KRN   (PTE_PRESENT | PTE_RW | PTE_GLOBAL)

#endif /* KERNEL_VMM_H */