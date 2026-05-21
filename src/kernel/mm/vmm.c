/* ============================================================
 * openos - 虚拟内存管理 (VMM) 实现
 * 32位保护模式，2级页表，恒等映射
 * ============================================================ */

#include "../include/vmm.h"
#include "../include/pmm.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

/* 硬编码页目录 - 固定在 .bss 中 */
__attribute__((aligned(4096)))
static uint32_t kernel_pgd[1024];

__attribute__((aligned(4096)))
static uint32_t kernel_pgt0[1024];   /* 0-4MB */

/* 当前 CR3 值 */
static uint32_t current_cr3 = 0;

/* VGA 调试输出 */
static int vga_col = 0;
static int vga_row = 0;

static void vga_putc(char c) {
    volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
    if (c == '\n') {
        vga_col = 0;
        vga_row++;
        return;
    }
    vga[vga_row * 80 + vga_col] = (uint16_t)(0x0A00 | c);
    vga_col++;
    if (vga_col >= 80) { vga_col = 0; vga_row++; }
}

static void vga_puts(const char *s) {
    while (*s) vga_putc(*s++);
}

static void vga_hex(uint32_t v) {
    const char *hex = "0123456789ABCDEF";
    vga_puts("0x");
    for (int i = 28; i >= 0; i -= 4) vga_putc(hex[(v >> i) & 0xF]);
}

/* ============================================================
 * 查找/分配页表
 * ============================================================ */
static uint32_t *get_or_create_pte(uint32_t vaddr, int alloc) {
    uint32_t pgd_idx = vaddr >> 22;
    uint32_t pte_idx = (vaddr >> 12) & 0x3FF;
    
    if (!(kernel_pgd[pgd_idx] & 1)) {
        if (!alloc) return NULL;
        /* 分配一个新页表 */
        void *pt = pmm_alloc_page();
        if (!pt) return NULL;
        /* 清零并映射 */
        for (int i = 0; i < 1024; i++) ((uint32_t *)pt)[i] = 0;
        kernel_pgd[pgd_idx] = ((uint32_t)pt) | 3;  /* present + rw */
    }
    
    uint32_t pt_phys = kernel_pgd[pgd_idx] & ~0xFFF;
    return (uint32_t *)(pt_phys | 0x80000000);  /* 恒等映射: 0x80000000 映射 0 */
}

/* ============================================================
 * 映射一个页
 * ============================================================ */
void vmm_map_page(uint32_t vaddr, uint32_t paddr, uint32_t flags) {
    /* 4KB 对齐 */
    vaddr &= ~0xFFF;
    paddr &= ~0xFFF;
    
    uint32_t *pte = get_or_create_pte(vaddr, 1);
    if (!pte) return;
    
    uint32_t pgd_idx = vaddr >> 22;
    uint32_t pte_idx = (vaddr >> 12) & 0x3FF;
    
    pte[pte_idx] = paddr | (flags & ~0xFFF) | 1;
    
    /* 刷新 TLB */
    __asm__ volatile ("invlpg (%0)" : : "r"(vaddr) : "memory");
}

/* ============================================================
 * 取消映射一个页
 * ============================================================ */
void vmm_unmap_page(uint32_t vaddr) {
    vaddr &= ~0xFFF;
    
    uint32_t *pte = get_or_create_pte(vaddr, 0);
    if (!pte) return;
    
    uint32_t pgd_idx = vaddr >> 22;
    uint32_t pte_idx = (vaddr >> 12) & 0x3FF;
    
    pte[pte_idx] = 0;
    __asm__ volatile ("invlpg (%0)" : : "r"(vaddr) : "memory");
}

/* ============================================================
 * 获取当前 CR3
 * ============================================================ */
uint32_t vmm_get_cr3(void) {
    uint32_t cr3;
    __asm__ volatile ("movl %%cr3, %0" : "=r"(cr3));
    return cr3;
}

/* ============================================================
 * 加载新的 CR3
 * ============================================================ */
void vmm_load_cr3(uint32_t cr3) {
    __asm__ volatile ("movl %0, %%cr3" : : "r"(cr3) : "memory");
    current_cr3 = cr3;
}

/* ============================================================
 * 分配一个页表（4KB对齐）
 * ============================================================ */
uint32_t vmm_alloc_page_table(void) {
    void *pt = pmm_alloc_page();
    if (!pt) return 0;
    for (int i = 0; i < 1024; i++) ((uint32_t *)pt)[i] = 0;
    return (uint32_t)pt;
}

/* ============================================================
 * 分配虚拟页（分配物理页并映射）
 * ============================================================ */
void *vmm_alloc_page(void) {
    void *phys = pmm_alloc_page();
    if (!phys) return NULL;
    /* 用 vmm_map_page 恒等映射 */
    vmm_map_page((uint32_t)phys, (uint32_t)phys, VMM_RW | VMM_USER);
    return phys;
}

/* ============================================================
 * 页错误处理
 * ============================================================ */
/* ============================================================
 * 页错误处理 (ISR 14)
 * 通过 isr_install_handler(14, page_fault_handler) 注册
 *
 * Page fault error code bits:
 *   bit 0 (0x01): P    - 0=page not present, 1=protection violation
 *   bit 1 (0x02): W/R  - 0=read, 1=write
 *   bit 2 (0x04): U/S  - 0=supervisor, 1=user
 * ============================================================
 */
void page_fault_handler(registers_t *regs) {
    uint32_t fault_addr;
    __asm__ volatile ("movl %%cr2, %0" : "=r"(fault_addr));
    uint32_t err = regs->err_code;

    if (err & 0x01) {
        vga_puts("\n[PF] protection fault!");
        goto pf_halt;
    }

    void *phys = pmm_alloc_page();
    if (!phys) {
        vga_puts("\n[PF] no physical memory!");
        goto pf_halt;
    }

    uint32_t vaddr = fault_addr & ~0xFFF;
    vmm_map_page(vaddr, (uint32_t)phys, VMM_RW);

    vga_puts("\n[PF] mapped v=0x");
    vga_hex(fault_addr);
    vga_puts(" -> p=0x");
    vga_hex((uint32_t)phys);
    vga_puts("\n");
    return;

pf_halt:
    vga_puts(" [PF] addr=0x");
    vga_hex(fault_addr);
    vga_puts(" err=0x");
    vga_hex(err);
    vga_puts("\nHALT\n");
    __asm__ volatile ("cli; hlt");
}

/* ============================================================
 * VMM 初始化 - 恒等映射全部物理内存
 * ============================================================ */
void vmm_init(void) {
    vga_puts("[VMM] init\n");
    
    /* 清空页目录和第一个页表 */
    for (int i = 0; i < 1024; i++) {
        kernel_pgd[i] = 0;
        kernel_pgt0[i] = 0;
    }
    
    /* 恒等映射 0-4MB（第一个页表已静态分配）*/
    for (int i = 0; i < 1024; i++) {
        kernel_pgt0[i] = (i << 12) | 3;
    }
    kernel_pgd[0] = ((uint32_t)kernel_pgt0) | 3;
    
    /* 恒等映射 4MB-512MB：动态分配页表 */
    /* QEMU -m 512M → 512MB = 128个页目录项 */
    for (int pgd_idx = 1; pgd_idx < 128; pgd_idx++) {
        void *pt = pmm_alloc_page();
        if (!pt) break;
        uint32_t *pt_virt = (uint32_t *)pt;  /* 分页未开启，物理地址=虚拟地址 */
        for (int i = 0; i < 1024; i++) {
            uint32_t phys = (pgd_idx << 22) | (i << 12);
            pt_virt[i] = phys | 3;
        }
        kernel_pgd[pgd_idx] = ((uint32_t)pt) | 3;
    }
    
    vga_puts("[VMM] mapping 0-512MB done\n");
    
    /* 加载 CR3 并开启分页 */
    __asm__ volatile ("movl %0, %%cr3" : : "r"(kernel_pgd) : "memory");
    current_cr3 = (uint32_t)kernel_pgd;
    
    __asm__ volatile (
        "movl %%cr0, %%eax\n"
        "orl  $0x80000000, %%eax\n"
        "movl %%eax, %%cr0\n"
        ::: "eax"
    );
    
    vga_puts("[VMM] paging ON\n");
    
    /* 注册页错误处理 */
    extern void isr_install_handler(uint8_t num, isr_t handler);
    extern void page_fault_handler(registers_t *regs);
    isr_install_handler(14, page_fault_handler);
    vga_puts("[VMM] PF handler registered\n");
}