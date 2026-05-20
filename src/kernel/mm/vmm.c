/* ============================================================
 * openos - 虚拟内存管理 (VMM) 实现
 * 使用 4 级页表 (PML4 → PDPT → PD → PT)
 * ============================================================ */

#include "../include/vmm.h"
#include "../include/pmm.h"

extern void flush_tlb(void);

/* 页表基址 */
static uint32_t kernel_pml4_phys = 0;
static uint32_t *kernel_pml4 = 0;

/* VGA 调试输出 */
static int dbg_col = 0;
static int dbg_row = 3;

static void vga_putc(char c, uint8_t fg, uint8_t bg) {
    volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
    if (c == '\n') { dbg_col = 0; dbg_row++; }
    else { vga[dbg_row * 80 + dbg_col] = (uint16_t)((bg << 12) | (fg << 8) | c); dbg_col++; if (dbg_col >= 80) { dbg_col = 0; dbg_row++; } }
}

#ifndef NULL
#define NULL 0
#endif

/* 启用分页 (CR3) */
static void enable_paging(uint32_t pml4_phys) {
    __asm__ volatile(
        "movl %0, %%eax\n"
        "movl %%eax, %%cr3\n"
        "movl %%cr4, %%eax\n"
        "orl $0x00000010, %%eax\n"
        "movl %%eax, %%cr4\n"
        "movl %%cr0, %%eax\n"
        "orl $0x80000000, %%eax\n"
        "movl %%eax, %%cr0\n"
        : : "r"(pml4_phys) : "eax");
}

/* 初始化 VMM */
void vmm_init(void) {
    /* 分配 PML4 */
    kernel_pml4 = (uint32_t *)pmm_alloc_page();
    if (!kernel_pml4) { while(1); }
    for (uint32_t i = 0; i < 1024; i++) kernel_pml4[i] = 0;
    kernel_pml4_phys = ((uint32_t)kernel_pml4) & 0xFFFFF000;

    /* 分配 PDPT (0x0 ~ 0xFFFF FFFF) */
    uint32_t *pdpt = (uint32_t *)pmm_alloc_page();
    if (!pdpt) { while(1); }
    for (uint32_t i = 0; i < 1024; i++) pdpt[i] = 0;
    kernel_pml4[0] = ((uint32_t)pdpt) | 3; /* present, rw */

    /* 分配 PD，每个 PD 项映射 4MB */
    uint32_t *pd = (uint32_t *)pmm_alloc_page();
    if (!pd) { while(1); }
    for (uint32_t i = 0; i < 1024; i++) pd[i] = (i * 0x400000U) | 3; /* 4MB each */
    pdpt[0] = ((uint32_t)pd) | 3;

    /* 启用分页 */
    enable_paging(kernel_pml4_phys);
    flush_tlb();

    /* 调试输出 (字符打印) */
    dbg_row = 3; dbg_col = 0;
    const char *msg = "[VMM] initialized. paging 0-4GB\n";
    for (int i = 0; msg[i]; i++) vga_putc(msg[i], 0x0A, 0);
}

/* 刷新 TLB */
void flush_tlb(void) {
    __asm__ volatile("movl %%cr3, %%eax\nmovl %%eax, %%cr3\n" : : : "eax");
}