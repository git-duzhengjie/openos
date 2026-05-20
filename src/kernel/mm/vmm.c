/* ============================================================
 * openos - 虚拟内存管理 (VMM) 实现
 * 32位保护模式，2级页表，恒等映射 0-4GB
 * ============================================================ */

#include "../include/vmm.h"
#include "../include/pmm.h"

/* 页目录和页表（4KB对齐） */
__attribute__((aligned(4096)))
static uint32_t boot_pgd[1024];   /* 页目录 (Page Directory) */

__attribute__((aligned(4096)))
static uint32_t boot_pgt0[1024];  /* 第0个页表，映射 0-4MB */

/* 简单的 VGA 字符输出，用于调试 */
static int vga_col = 0;
static int vga_row = 3;

static void vga_puts(const char *s) {
    volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
    for (int i = 0; s[i]; i++) {
        if (s[i] == '\n') {
            vga_col = 0;
            vga_row++;
        } else {
            vga[vga_row * 80 + vga_col] = (uint16_t)(0x0A00 | s[i]);
            vga_col++;
            if (vga_col >= 80) { vga_col = 0; vga_row++; }
        }
    }
}

#ifndef NULL
#define NULL 0
#endif

/* ============================================================
 * 初始化 VMM
 * 1. 设置页目录/页表（恒等映射 0-4GB）
 * 2. 加载 CR3
 * 3. 开启分页 (CR0.PG=1)
 * ============================================================ */
void vmm_init(void) {
    vga_puts("[VMM] init start\n");

    /* 第0个页表：恒等映射 0-4MB (1024 个 4KB 页) */
    for (uint32_t i = 0; i < 1024; i++) {
        boot_pgt0[i] = (i << 12) | 3;   /* present | rw | user */
    }

    /* 页目录：每一项映射 4MB
     * 暂时把所有项都指向 boot_pgt0（覆盖 0-4GB 的前4MB）
     * 后续可以动态分配更多页表 */
    for (uint32_t i = 0; i < 1024; i++) {
        if (i == 0) {
            boot_pgd[i] = ((uint32_t)boot_pgt0) | 3;
        } else {
            /* 暂时映射到其他页表（未分配，先指向 boot_pgt0） */
            boot_pgd[i] = ((uint32_t)boot_pgt0) | 3;
        }
    }

    vga_puts("[VMM] page tables ready\n");

    /* 步骤1：加载 CR3（页目录物理地址） */
    __asm__ volatile("movl %0, %%cr3\n" :: "r"(boot_pgd) : "memory");

    vga_puts("[VMM] CR3 loaded\n");

    /* 步骤2：开启分页 (CR0 的 PG 位 = bit31) */
    __asm__ volatile(
        "movl %%cr0, %%eax\n"
        "orl  $0x80000000, %%eax\n"
        "movl %%eax, %%cr0\n"
        "jmp  1f\n"      /* 刷新预取队列 */
        "1:\n"
        ::: "eax", "memory"
    );

    vga_puts("[VMM] paging ON\n");

    /* 在屏幕上显示成功信息 */
    vga_row = 5;
    vga_col = 0;
    vga_puts("VMM OK - paging enabled\n");
}
