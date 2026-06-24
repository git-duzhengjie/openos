/* ============================================================
 * openos - 虚拟内存管理 (32位保护模式)
 * ============================================================ */

#ifndef KERNEL_VMM_H
#define KERNEL_VMM_H

#include <stdint.h>
#include "idt.h"

/* 页大小和偏移 */
#define PAGE_SIZE    4096
#define PAGE_SHIFT   12
#define PAGE_MASK    (~(PAGE_SIZE - 1))

/* 页表条目标志 */
#define PTE_PRESENT    0x001  /* 存在位 */
#define PTE_RW         0x002  /* 读/写 */
#define PTE_USER       0x004  /* 用户可访问 */
#define PTE_ACCESSED   0x020  /* 已访问 */
#define PTE_DIRTY      0x040  /* 已修改 */
#define PTE_GLOBAL     0x100  /* 全局 (TLB不刷新) */
#define PTE_COW        0x200  /* 软件位: copy-on-write */

/* 标志快捷方式 */
#define VMM_RW    (PTE_PRESENT | PTE_RW)
#define VMM_RO    (PTE_PRESENT)
#define VMM_USER  (PTE_PRESENT | PTE_RW | PTE_USER)
#define VMM_KRN   (PTE_PRESENT | PTE_RW | PTE_GLOBAL)

/* ============================================================
 * 函数声明
 * ============================================================ */
void vmm_init(void);                              /* 初始化虚拟内存 */
void vmm_map_page(uint32_t vaddr, uint32_t paddr, uint32_t flags);  /* 映射单个页 */
int  vmm_map_page_checked(uint32_t vaddr, uint32_t paddr, uint32_t flags); /* 映射单个页，失败返回 0 */
void vmm_map_range(uint32_t vaddr, uint32_t paddr, uint32_t size, uint32_t flags);  /* 静默映射连续物理区域 */
uint32_t vmm_get_mapping(uint32_t vaddr);        /* 查询当前地址空间 PTE */
void vmm_update_page_flags(uint32_t vaddr, uint32_t flags);
void vmm_unmap_page(uint32_t vaddr);             /* 取消映射 */
uint32_t vmm_get_cr3(void);                      /* 获取当前 CR3 */
void vmm_load_cr3(uint32_t cr3);                 /* 加载新 CR3 */
void *vmm_alloc_page(void);                      /* 分配虚拟+物理页并映射 */
uint32_t vmm_alloc_page_table(void);             /* 分配一个新的页表(4KB对齐) */
uint32_t vmm_kernel_cr3(void);                   /* 获取内核页目录 CR3 */
uint32_t vmm_create_user_address_space(void);    /* 创建独立用户地址空间页目录 */

/* 页错误处理 (由 IDT ISR 14 调用) */
void page_fault_handler(registers_t *regs);

#endif /* KERNEL_VMM_H */