/* ============================================================
 * openos - 虚拟内存管理实现
 * ============================================================ */

#include "include/vmm.h"
#include "include/pmm.h"

/* 外部函数 */
extern void flush_tlb(void);

/* 页表基址 */
static uint64_t kernel_pml4_phys = 0;
static pte_t *kernel_pml4 = NULL;

/* VGA */
#define VGA_BASE ((volatile uint16_t *)0xB8000)
static int vga_col = 0, vga_row = 1;

static void vga_putc(char c, uint8_t fg, uint8_t bg)
{
    if (c == '\n') {
        vga_col = 0;
        vga_row++;
    } else {
        VGA_BASE[vga_row * 80 + vga_col] = (uint16_t)((bg << 12) | (fg << 8) | c);
        vga_col++;
        if (vga_col >= 80) {
            vga_col = 0;
            vga_row++;
        }
    }
    (void)fg; (void)bg;
}

/* 获取/设置 CR3 */
static inline uint64_t get_cr3(void)
{
    uint64_t val;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(val));
    return val;
}

static inline void set_cr3(uint64_t cr3)
{
    __asm__ volatile ("mov %0, %%cr3" : : "r"(cr3));
}

/* ============================================================
 * 初始化虚拟内存
 * ============================================================ */
void vmm_init(void)
{
    vga_putc('[', 0x0A); vga_putc('V', 0x0A); vga_putc('M', 0x0A); vga_putc('M', 0x0A);
    vga_putc(']', 0x0A); vga_putc(' ', 0x0A);
    vga_putc('i', 0x0A); vga_putc('n', 0x0A); vga_putc('i', 0x0A); vga_putc('t', 0x0A);
    vga_putc('i', 0x0A); vga_putc('a', 0x0A); vga_putc('l', 0x0A); vga_putc('i', 0x0A);
    vga_putc('z', 0x0A); vga_putc('i', 0x0A); vga_putc('n', 0x0A); vga_putc('g', 0x0A);
    vga_putc('.', 0x0A); vga_putc('.', 0x0A); vga_putc('\n', 0x0A);

    /* 分配 PML4 */
    kernel_pml4 = (pte_t *)pmm_alloc_page();
    if (!kernel_pml4) {
        vga_putc('[', 0x04); vga_putc('E', 0x04); vga_putc('R', 0x04); vga_putc('R', 0x04);
        vga_putc(']', 0x04); vga_putc(' ', 0x04);
        vga_putc('n', 0x04); vga_putc('o', 0x04); vga_putc(' ', 0x04);
        vga_putc('m', 0x04); vga_putc('e', 0x04); vga_putc('m', 0x04); vga_putc('o', 0x04);
        vga_putc('r', 0x04); vga_putc('y', 0x04); vga_putc('\n', 0x04);
        while(1) __asm__ volatile("hlt");
        return;
    }

    kernel_pml4_phys = (uint64_t)kernel_pml4 & 0xFFFFFFFFFFFFF000ULL;

    /* 清空 PML4 */
    for (int i = 0; i < 512; i++)
        kernel_pml4[i].val = 0;

    /* Identity-map 前 4GB (0x00000000 - 0xFFFFFFFF) */
    vga_putc(' ', 0x0A); vga_putc('i', 0x0A); vga_putc('d', 0x0A);
    vga_putc('e', 0x0A); vga_putc('n', 0x0A); vga_putc('t', 0x0A);
    vga_putc('i', 0x0A); vga_putc('t', 0x0A); vga_putc('y', 0x0A);
    vga_putc(' ', 0x0A); vga_putc('m', 0x0A); vga_putc('a', 0x0A);
    vga_putc('p', 0x0A); vga_putc(' ', 0x0A);
    vga_putc('0', 0x0A); vga_putc('-', 0x0A); vga_putc('4', 0x0A);
    vga_putc('G', 0x0A); vga_putc('B', 0x0A); vga_putc(' ', 0x0A);

    uint64_t pml4_idx = 0;  /* PML4[0] -> 前4GB */
    uint64_t pdpt_phys = (uint64_t)pmm_alloc_page() & 0xFFFFFFFFFFFFF000ULL;
    kernel_pml4[pml4_idx].val = pdpt_phys | PTE_PRESENT | PTE_RW | PTE_GLOBAL;

    pte_t *pdpt = (pte_t *)(pdpt_phys);
    for (int pdpt_i = 0; pdpt_i < 4; pdpt_i++) {
        uint64_t pd_phys = (uint64_t)pmm_alloc_page() & 0xFFFFFFFFFFFFF000ULL;
        pdpt[pdpt_i].val = pd_phys | PTE_PRESENT | PTE_RW | PTE_GLOBAL;

        pte_t *pd = (pte_t *)pd_phys;
        for (int pd_i = 0; pd_i < 512; pd_i++) {
            uint64_t pt_phys = (uint64_t)pmm_alloc_page() & 0xFFFFFFFFFFFFF000ULL;
            pd[pd_i].val = pt_phys | PTE_PRESENT | PTE_RW | PTE_GLOBAL;

            pte_t *pt = (pte_t *)pt_phys;
            for (int pt_i = 0; pt_i < 512; pt_i++) {
                uint64_t page_base = (pdpt_i * 512 + pd_i) * 512 + pt_i;
                uint64_t phys_addr = page_base * PAGE_SIZE;
                pt[pt_i].val = phys_addr | PTE_PRESENT | PTE_RW | PTE_GLOBAL;
            }
        }
    }

    vga_putc('[', 0x02); vga_putc('O', 0x02); vga_putc('K', 0x02); vga_putc(']', 0x02);
    vga_putc('\n', 0x02);

    /* 加载 CR3 */
    set_cr3(kernel_pml4_phys);
    vga_putc('[', 0x0A); vga_putc('V', 0x0A); vga_putc('M', 0x0A); vga_putc('M', 0x0A);
    vga_putc(']', 0x0A); vga_putc(' ', 0x0A); vga_putc('C', 0x0A);
    vga_putc('R', 0x0A); vga_putc('3', 0x0A); vga_putc(' ', 0x0A);
    vga_putc('l', 0x0A); vga_putc('o', 0x0A); vga_putc('a', 0x0A);
    vga_putc('d', 0x0A); vga_putc('e', 0x0A); vga_putc('d', 0x0A);
    vga_putc('\n', 0x0A);
}

/* ============================================================
 * 获取 CR3
 * ============================================================ */
uint64_t vmm_get_cr3(void)
{
    return get_cr3();
}

/* ============================================================
 * 加载 CR3
 * ============================================================ */
void vmm_load_cr3(uint64_t pml4)
{
    set_cr3(pml4);
    __asm__ volatile ("mov %%cr4, %%eax; mov %%eax, %%cr4" : : : "eax");
}

/* ============================================================
 * 切换到新的页表
 * ============================================================ */
void vmm_switch_pml4(uint64_t pml4_phys)
{
    set_cr3(pml4_phys);
}

/* ============================================================
 * 映射单个页
 * ============================================================ */
void vmm_map_page(uint64_t vaddr, uint64_t paddr, uint64_t flags)
{
    (void)vaddr; (void)paddr; (void)flags;
    /* 占位，后续完善 */
}

/* ============================================================
 * 取消映射
 * ============================================================ */
void vmm_unmap_page(uint64_t vaddr)
{
    (void)vaddr;
}

/* ============================================================
 * 分配新页表
 * ============================================================ */
uint64_t vmm_alloc_page_table(void)
{
    void *page = pmm_alloc_page();
    if (!page) return 0;
    return (uint64_t)page & 0xFFFFFFFFFFFFF000ULL;
}

/* ============================================================
 * 分配虚拟+物理页
 * ============================================================ */
void *vmm_alloc_page(void)
{
    void *phys = pmm_alloc_page();
    if (!phys) return NULL;
    return phys;
}