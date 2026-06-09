/* ============================================================
 * openos - 虚拟内存管理 (VMM) 实现
 * 32位保护模式，2级页表
 * 使用 PMM 动态分配页表 + 恒等映射 0-256MB
 * v10: 修复 vmm_map_page 动态分配缺失的页表
 * ============================================================ */

#include "../include/vmm.h"
#include "../include/pmm.h"
#include "../include/serial.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

/* 页目录（链接器放置在 0x1CAE0） */
static uint32_t kernel_pgd[1024] __attribute__((aligned(4096)));

/* 恒等映射范围：0 - 256MB（支持用户栈在 0xBF000000） */
#define IDENTITY_END  (256 * 1024 * 1024)  /* 256MB */

/* 页目录物理基址 */
static uint32_t kernel_pgd_phys;

/* ============================================================
 * 初始化虚拟内存
 * ============================================================ */
void vmm_init(void) {
    serial_write("[VMM] Init start\n");
    
    /* 清空页目录 */
    for (int i = 0; i < 1024; i++) {
        kernel_pgd[i] = 0;
    }
    
    /* 计算并保存页目录物理地址 */
    /* kernel_pgd 在 .bss 段，位于 identity 区域 0-256MB 内 */
    __asm__ volatile ("mov %%cr3, %0" : : "r"(kernel_pgd_phys));
    /* 由于还没启用分页，kernel_pgd 虚拟地址 = 物理地址 */
    kernel_pgd_phys = (uint32_t)kernel_pgd;
    serial_write("[VMM] kernel_pgd_phys=");
    serial_write_hex(kernel_pgd_phys);
    serial_write("\n");
    
    /* 计算需要多少个页表（每个覆盖 4MB） */
    int num_pt = IDENTITY_END / (4 * 1024 * 1024);  /* 256MB / 4MB = 64 */
    
    /* 分配页表并建立恒等映射 */
    for (int pgd_idx = 0; pgd_idx < num_pt; pgd_idx++) {
        uint32_t pt_phys = (uint32_t)pmm_alloc_page();
        if (!pt_phys) {
            serial_write("[VMM] FAILED to alloc page table!\n");
            return;
        }
        
        /* 设置 PDE，恒等映射 */
        kernel_pgd[pgd_idx] = pt_phys | 3;
        
        /* 初始化 PTE */
        uint32_t *pt = (uint32_t *)pt_phys;
        for (int pte_idx = 0; pte_idx < 1024; pte_idx++) {
            uint32_t phys = (pgd_idx << 22) | (pte_idx << 12);
            pt[pte_idx] = phys | 3;
        }
    }
    
    /* 递归映射：PDE[1023] 指向页目录自身 */
    kernel_pgd[1023] = (uint32_t)kernel_pgd | 3;
    
    /* 加载 CR3 */
    __asm__ volatile ("mov %0, %%cr3" : : "r"((uint32_t)kernel_pgd));
    
    /* 启用分页 */
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0));
    
    serial_write("[VMM] Paging enabled!\n");
    
    /* 验证递归映射 */
    uint32_t *rec = (uint32_t *)0xFFFFF000;
    serial_write("[VMM] RecVerify=");
    serial_write_hex(rec[1023]);
    serial_write(" expect=");
    serial_write_hex(kernel_pgd[1023]);
    serial_write("\n");
    serial_write("[VMM] Done!\n");
}

/* ============================================================
 * 映射一个页（核心函数，v10 修复：动态分配缺失的 PDE）
 * ============================================================ */
void vmm_map_page(uint32_t vaddr, uint32_t paddr, uint32_t flags) {
    vaddr &= ~0xFFF;
    paddr &= ~0xFFF;
    
    uint32_t pgd_idx = vaddr >> 22;
    uint32_t pte_idx = (vaddr >> 12) & 0x3FF;
    
    /* 如果 PDE 不存在，动态分配页表 */
    if ((kernel_pgd[pgd_idx] & 1) == 0) {
        serial_write("[VMM] PDE missing for pgd_idx=");
        serial_write_hex(pgd_idx);
        serial_write(", allocating PT...\n");
        
        /* 分配新的页表 */
        uint32_t pt_phys = (uint32_t)pmm_alloc_page();
        if (!pt_phys) {
            serial_write("[VMM] FAILED: cannot alloc page table!\n");
            return;
        }
        
        /* 清空页表 */
        uint32_t *pt = (uint32_t *)pt_phys;
        for (int i = 0; i < 1024; i++) {
            pt[i] = 0;
        }
        
        /* 通过递归映射设置 PDE */
        uint32_t *pgd_rec = (uint32_t *)(0xFFFFF000 + (pgd_idx << 2));
        *pgd_rec = pt_phys | (flags & 0x07) | 1;  /* inherit U/S from flags */
        
        serial_write("[VMM] PDE[");
        serial_write_hex(pgd_idx);
        serial_write("] = ");
        serial_write_hex(*pgd_rec);
        serial_write("\n");
    } else {
        /* PDE 已存在，确保权限包含用户位 */
        if (flags & PTE_USER) {
            uint32_t *pgd_rec = (uint32_t *)(0xFFFFF000 + (pgd_idx << 2));
            *pgd_rec |= PTE_USER;  /* 设置 U/S 位 */
        }
    }
    
    /* 通过递归映射访问页表并设置 PTE */
    uint32_t *pte = (uint32_t *)(0xFFC00000 + (pgd_idx << 12));
    pte[pte_idx] = (paddr & ~0xFFF) | (flags & 0xFFF);
    
    /* 调试：打印 PTE 值 */
    serial_write("[VMM] Mapped: virt=");
    serial_write_hex(vaddr);
    serial_write(" phys=");
    serial_write_hex(paddr);
    serial_write(" pte=");
    serial_write_hex(pte[pte_idx]);
    serial_write("\n");
    
    __asm__ volatile ("invlpg (%0)" : : "r"(vaddr));
}

/* ============================================================
 * 取消映射
 * ============================================================ */
void vmm_unmap_page(uint32_t vaddr) {
    vaddr &= ~0xFFF;
    uint32_t pgd_idx = vaddr >> 22;
    uint32_t pte_idx = (vaddr >> 12) & 0x3FF;
    if ((kernel_pgd[pgd_idx] & 1) == 0) return;
    uint32_t *pte = (uint32_t *)(0xFFC00000 + (pgd_idx << 12));
    pte[pte_idx] = 0;
    __asm__ volatile ("invlpg (%0)" : : "r"(vaddr));
}

/* ============================================================
 * 获取/设置 CR3
 * ============================================================ */
uint32_t vmm_get_cr3(void) {
    uint32_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

void vmm_load_cr3(uint32_t cr3) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(cr3));
}

/* ============================================================
 * 分配虚拟页
 * ============================================================ */
void *vmm_alloc_page(void) {
    uint32_t phys = (uint32_t)pmm_alloc_page();
    if (!phys) return NULL;
    static uint32_t next_virt = 0xC0000000;
    uint32_t virt = next_virt;
    next_virt += 4096;
    vmm_map_page(virt, phys, VMM_RW);
    return (void *)virt;
}

uint32_t vmm_alloc_page_table(void) {
    return (uint32_t)pmm_alloc_page();
}
